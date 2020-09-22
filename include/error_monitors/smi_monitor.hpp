/*
// Copyright (c) 2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once
#include <systemd/sd-journal.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <gpiod.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::smi_monitor
{
static constexpr bool debug = true;

class SMIMonitor
{
    boost::asio::io_service& io;
    std::shared_ptr<sdbusplus::asio::connection> conn;

    const static constexpr size_t smiPollingTimeMs = 1000;
    const static constexpr size_t smiTimeoutMs = 90000;

    // Timer for SMI polling
    boost::asio::steady_timer smiPollingTimer;
    std::chrono::steady_clock::time_point smiTimeoutTime;

    // GPIO Lines and Event Descriptors
    gpiod::line smiLine;
    boost::asio::posix::stream_descriptor smiEvent;

    void smiTimeoutLog()
    {
        sd_journal_send("MESSAGE=HostError: SMI Timeout", "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        "SMI Timeout", NULL);
    }

    bool requestSMIEvents()
    {
        // Find the GPIO line
        smiLine = gpiod::find_line("SMI");
        if (!smiLine)
        {
            std::cerr << "Failed to find the SMI line\n";
            return false;
        }

        try
        {
            smiLine.request(
                {"host-error-monitor", gpiod::line_request::EVENT_BOTH_EDGES});
        }
        catch (std::exception&)
        {
            std::cerr << "Failed to request events for SMI\n";
            return false;
        }

        int smiLineFd = smiLine.event_get_fd();
        if (smiLineFd < 0)
        {
            std::cerr << "Failed to get SMI fd\n";
            return false;
        }

        smiEvent.assign(smiLineFd);

        return true;
    }

    bool smiAsserted()
    {
        if constexpr (debug)
        {
            std::cerr << "Checking SMI state\n";
        }

        if (hostIsOff())
        {
            if constexpr (debug)
            {
                std::cerr << "Host is off\n";
            }
            return false;
        }

        return (smiLine.get_value() == 0);
    }

    void smiAssertHandler()
    {
        std::cerr << "SMI asserted for " << std::to_string(smiTimeoutMs)
                  << " ms\n";
        smiTimeoutLog();
        conn->async_method_call(
            [this](boost::system::error_code ec,
                   const std::variant<bool>& property) {
                if (ec)
                {
                    return;
                }
                const bool* reset = std::get_if<bool>(&property);
                if (reset == nullptr)
                {
                    std::cerr << "Unable to read reset on SMI value\n";
                    return;
                }
#ifdef HOST_ERROR_CRASHDUMP_ON_SMI_TIMEOUT
                startCrashdumpAndRecovery(conn, *reset, "SMI Timeout");
#else
                if (*reset)
                {
                    std::cout << "Recovering the system\n";
                    startWarmReset(conn);
                }
#endif
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/bmc_reset_disables",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.ResetDisables", "ResetOnSMI");
    }

    void flushSMIEvents()
    {
        if constexpr (debug)
        {
            std::cerr << "Flushing SMI events\n";
        }

        while (true)
        {
            try
            {
                smiLine.event_read();
            }
            catch (std::system_error)
            {
                break;
            }
        }
    }

    void waitForSMI()
    {
        if constexpr (debug)
        {
            std::cerr << "Wait for SMI\n";
        }

        smiEvent.async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [this](const boost::system::error_code ec) {
                if (ec)
                {
                    // operation_aborted is expected if wait is canceled.
                    if (ec != boost::asio::error::operation_aborted)
                    {
                        std::cerr << "smi wait error: " << ec.message() << "\n";
                    }
                    return;
                }

                if constexpr (debug)
                {
                    std::cerr << "SMI event ready\n";
                }

                smiTimeoutTime =
                    std::chrono::steady_clock::now() +
                    std::chrono::duration<int, std::milli>(smiTimeoutMs);
                pollSMI();
            });
    }

    void pollSMI()
    {
        if constexpr (debug)
        {
            std::cerr << "Polling SMI\n";
        }

        flushSMIEvents();

        if (!smiAsserted())
        {
            if constexpr (debug)
            {
                std::cerr << "SMI not asserted\n";
            }

            waitForSMI();
            return;
        }
        if constexpr (debug)
        {
            std::cerr << "SMI asserted\n";
        }

        if (std::chrono::steady_clock::now() > smiTimeoutTime)
        {
            smiAssertHandler();
            waitForSMI();
            return;
        }

        smiPollingTimer.expires_after(
            std::chrono::milliseconds(smiPollingTimeMs));
        smiPollingTimer.async_wait([this](const boost::system::error_code ec) {
            if (ec)
            {
                // operation_aborted is expected if timer is canceled before
                // completion.
                if (ec != boost::asio::error::operation_aborted)
                {
                    std::cerr
                        << "smi polling async_wait failed: " << ec.message()
                        << "\n";
                }
                return;
            }
            pollSMI();
        });
    }

  public:
    SMIMonitor(boost::asio::io_service& io,
               std::shared_ptr<sdbusplus::asio::connection> conn) :
        io(io),
        conn(conn), smiPollingTimer(io), smiEvent(io)
    {
        if constexpr (debug)
        {
            std::cerr << "Initializing SMI Monitor\n";
        }

        // Request SMI GPIO events
        if (!requestSMIEvents())
        {
            return;
        }
        smiEvent.non_blocking(true);
        smiTimeoutTime = std::chrono::steady_clock::now() +
                         std::chrono::duration<int, std::milli>(smiTimeoutMs);
        pollSMI();
    }

    void hostOn()
    {
        smiEvent.cancel();
        pollSMI();
    }
};
} // namespace host_error_monitor::smi_monitor
