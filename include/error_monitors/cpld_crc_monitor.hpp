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

#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace host_error_monitor::cpld_crc_monitor
{
class CPLDCRCMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::highAssert;
    size_t cpuNum;
    bool cpuPresent;

    void logEvent() override
    {
        std::string cpuNumber = "CPU " + std::to_string(cpuNum);
        std::string msg = cpuNumber + " CPLD CRC error.";

        log_message(LOG_INFO, msg, "OpenBMC.0.1.CPUError", msg);
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
            cpuPresenceLine.request(
                {"host-error-monitor", gpiod::line_request::DIRECTION_INPUT,
                 gpiod::line_request::FLAG_ACTIVE_LOW});
        }
        catch (std::exception&)
        {
            std::cerr << "Failed to request " << cpuPresenceName << " input\n";
            return false;
        }

        cpuPresent = cpuPresenceLine.get_value();

        return true;
    }

    void assertHandler() override
    {
        // Ignore this if the CPU is not present
        if (cpuPresent)
        {
            host_error_monitor::base_gpio_monitor::BaseGPIOMonitor::
                assertHandler();
        }
    }

    /** @brief Constructor to create a CPLD CRC signal monitor
     *  @param[in] io - ASIO io_context
     *  @param[in] conn - ASIO connection
     *  @param[in] signalName - GPIO name of the signal to monitor
     *  @param[in] cpuNum - CPU number associated with the signal
     *  @param[in] cpuPresenceName - Name of the GPIO that can be read to check
     *                               if the CPU is present
     */
  public:
    CPLDCRCMonitor(boost::asio::io_context& io,
                   std::shared_ptr<sdbusplus::asio::connection> conn,
                   const std::string& signalName, const size_t cpuNum,
                   const std::string& cpuPresenceName) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), cpuNum(cpuNum)
    {
        if (!getCPUPresence(cpuPresenceName))
        {
            valid = false;
        }

        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::cpld_crc_monitor
