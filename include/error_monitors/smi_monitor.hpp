/*
// Copyright (c) 2021 Intel Corporation
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

#include <error_monitors/base_gpio_poll_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::smi_monitor
{
static constexpr bool debug = false;

class SMIMonitor :
    public host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor
{
    const static host_error_monitor::base_gpio_poll_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_poll_monitor::AssertValue::lowAssert;
    const static constexpr size_t smiPollingTimeMs = 1000;
    const static constexpr size_t smiTimeoutMs = 90000;

    void logEvent() override
    {
        sd_journal_send("MESSAGE=HostError: SMI Timeout", "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        "SMI Timeout", NULL);
    }

    void assertHandler() override
    {
        BaseGPIOPollMonitor::assertHandler();

        conn->async_method_call(
            [this](boost::system::error_code ec,
                   const std::variant<bool>& property) {
                // Default to no reset after Crashdump
                bool reset = false;
                if (!ec)
                {
                    const bool* resetPtr = std::get_if<bool>(&property);
                    if (resetPtr == nullptr)
                    {
                        std::cerr << "Unable to read reset on "
                                  << signalName << " value\n";
                    }
                    else
                    {
                        reset = *resetPtr;
                    }
                }
#ifdef HOST_ERROR_CRASHDUMP_ON_SMI_TIMEOUT
                startCrashdumpAndRecovery(conn, reset, "SMI Timeout");
#else
                if (reset)
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

  public:
    SMIMonitor(boost::asio::io_service& io,
               std::shared_ptr<sdbusplus::asio::connection> conn,
               const std::string& signalName) :
        BaseGPIOPollMonitor(io, conn, signalName, assertValue, smiPollingTimeMs,
                            smiTimeoutMs)

    {
        if (valid)
        {
            startPolling();
        }
    }
};
} // namespace host_error_monitor::smi_monitor
