/*
// Copyright (c) 2022 Intel Corporation
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

namespace host_error_monitor::cpu_presence_monitor
{
class CPUPresenceMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
    int cpuPresent;
    size_t cpuNum;
    const static constexpr uint8_t beepCPUMIssing = 3;

    void logEvent()
    {
        std::string msg = "CPU " + std::to_string(cpuNum) + " missing";

        log_message(LOG_INFO, msg, "OpenBMC.0.1.CPUError", msg);
    }

    void CPUPresenceAssertHandler(
        std::shared_ptr<sdbusplus::asio::connection> conn)
    {
        std::cerr << signalName << " asserted\n";
        // raising beep alert for base cpu missing
        beep(conn, beepCPUMIssing);
        logEvent();
    }

    bool getCPUPresence(const std::string& cpuPresenceName)
    {
        // Find the GPIO line
        gpiod::line cpuPresenceLine = gpiod::find_line(cpuPresenceName);
        if (!cpuPresenceLine)
        {
            std::cerr << "Failed to find the " << cpuPresenceName << " line.\n";
            return false;
        }

        // Request GPIO input
        try
        {
            cpuPresenceLine.request({"host-error-monitor",
                                     gpiod::line_request::DIRECTION_INPUT,
                                     gpiod::line_request::FLAG_ACTIVE_LOW});
        }
        catch (const std::exception&)
        {
            std::cerr << "Failed to request " << cpuPresenceName << " input\n";
            return false;
        }

        cpuPresent = cpuPresenceLine.get_value();

        return true;
    }

    void checkCPUPresence(std::shared_ptr<sdbusplus::asio::connection> conn)
    {
        // Ignore this if the CPU present
        if (!cpuPresent)
        {
            CPUPresenceAssertHandler(conn);
        }
    }

  public:
    CPUPresenceMonitor(boost::asio::io_context& io,
                       std::shared_ptr<sdbusplus::asio::connection> conn,
                       const std::string& signalName, const size_t cpuNum) :
        BaseMonitor(io, conn, signalName),
        cpuNum(cpuNum)
    {
        if (!getCPUPresence(signalName))
        {
            return;
        }
        checkCPUPresence(conn);
        valid = true;
    }
    void hostOn() override
    {
        checkCPUPresence(conn);
    }
};
} // namespace host_error_monitor::cpu_presence_monitor
