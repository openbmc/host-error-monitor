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

#include <error_monitors/base_monitor.hpp>
#include <gpiod.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::cpu_mismatch_monitor
{
static constexpr bool debug = false;

class CPUMismatchMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
    size_t cpuNum;
    gpiod::line cpuMismatchLine;

    void cpuMismatchLog()
    {
        const std::string cpuS = std::to_string(cpuNum);

        log_message(LOG_ERR, "CPU " + cpuS + " mismatch",
                    "OpenBMC.0.1.CPUMismatch", cpuS);
    }

    bool requestCPUMismatchInput()
    {
        // Find the GPIO line
        cpuMismatchLine = gpiod::find_line(signalName);
        if (!cpuMismatchLine)
        {
            std::cerr << "Failed to find the " << signalName << " line.\n";
            return false;
        }

        // Request GPIO input
        try
        {
            cpuMismatchLine.request({"host-error-monitor",
                                     gpiod::line_request::DIRECTION_INPUT,
                                     0}); // 0 indicates ACTIVE_HIGH
        }
        catch (std::exception&)
        {
            std::cerr << "Failed to request " << signalName << " input\n";
            return false;
        }

        return true;
    }

    bool cpuMismatchAsserted()
    {
        if constexpr (debug)
        {
            std::cerr << "Checking " << signalName << " state\n";
        }

        return (cpuMismatchLine.get_value());
    }

    void cpuMismatchAssertHandler()
    {
        std::cerr << signalName << " asserted\n";
        cpuMismatchLog();
    }

    void checkCPUMismatch()
    {
        if (cpuMismatchAsserted())
        {
            cpuMismatchAssertHandler();
        }
    }

  public:
    CPUMismatchMonitor(boost::asio::io_context& io,
                       std::shared_ptr<sdbusplus::asio::connection> conn,
                       const std::string& signalName, const size_t cpuNum) :
        BaseMonitor(io, conn, signalName), cpuNum(cpuNum)
    {
        // Request GPIO input
        if (!requestCPUMismatchInput())
        {
            return;
        }
        checkCPUMismatch();
        valid = true;
    }

    void hostOn() override
    {
        checkCPUMismatch();
    }
};
} // namespace host_error_monitor::cpu_mismatch_monitor
