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

namespace host_error_monitor::cpu1_mismatch_monitor
{
static constexpr bool debug = false;

class CPU1MismatchMonitor
{
    boost::asio::io_service& io;
    std::shared_ptr<sdbusplus::asio::connection> conn;

    const static constexpr char* signalName = "CPU1_MISMATCH";
    gpiod::line cpu1MismatchLine;

    void cpu1MismatchLog()
    {
        sd_journal_send("MESSAGE=HostError: CPU1 mismatch", "PRIORITY=%i",
                        LOG_ERR, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUMismatch", "REDFISH_MESSAGE_ARGS=%d", 1,
                        NULL);
    }

    bool requestCPU1MismatchInput()
    {
        // Find the GPIO line
        cpu1MismatchLine = gpiod::find_line(signalName);
        if (!cpu1MismatchLine)
        {
            std::cerr << "Failed to find the " << signalName << " line.\n";
            return false;
        }

        // Request GPIO input
        try
        {
            cpu1MismatchLine.request(
                {"host-error-monitor", gpiod::line_request::DIRECTION_INPUT});
        }
        catch (std::exception&)
        {
            std::cerr << "Failed to request " << signalName << " input\n";
            return false;
        }

        return true;
    }

    bool cpu1MismatchAsserted()
    {
        if constexpr (debug)
        {
            std::cerr << "Checking " << signalName << " state\n";
        }

        return (cpu1MismatchLine.get_value() == 1);
    }

    void cpu1MismatchAssertHandler()
    {
        std::cerr << signalName << " asserted\n";
        cpu1MismatchLog();
    }

    void checkCPU1Mismatch()
    {
        if (cpu1MismatchAsserted())
        {
            cpu1MismatchAssertHandler();
        }
    }

  public:
    CPU1MismatchMonitor(boost::asio::io_service& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn) :
        io(io),
        conn(conn)
    {
        if constexpr (debug)
        {
            std::cerr << "Initializing " << signalName << " Monitor\n";
        }

        // Request SMI GPIO events
        if (!requestCPU1MismatchInput())
        {
            return;
        }
        checkCPU1Mismatch();
    }

    void hostOn()
    {
        checkCPU1Mismatch();
    }
};
} // namespace host_error_monitor::cpu1_mismatch_monitor
