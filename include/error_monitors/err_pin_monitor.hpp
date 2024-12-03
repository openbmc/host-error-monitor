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

#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <bitset>

namespace host_error_monitor::err_pin_monitor
{
static constexpr bool debug = false;

class ErrPinMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    size_t errPin;
    std::bitset<MAX_CPUS> errPinCPUs;
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::lowAssert;

    void logEvent()
    {
        checkErrPinCPUs(errPin, errPinCPUs);
        if (errPinCPUs.none())
        {
            return errPinLog();
        }

        for (size_t i = 0; i < errPinCPUs.size(); i++)
        {
            if (errPinCPUs[i])
            {
                errPinLog(i);
            }
        }
    }

    void errPinLog()
    {
        std::string msg = "ERR" + std::to_string(errPin) + " asserted on one of the CPUs";

        log_message(LOG_ERR, msg, "OpenBMC.0.1.CPUError", msg);
    }

    void errPinLog(const int cpuNum)
    {
        std::string msg = "ERR" + std::to_string(errPin) + " asserted on CPU " +
                          std::to_string(cpuNum + 1);

        log_message(LOG_ERR, msg, "OpenBMC.0.1.CPUError", msg);
    }

  public:
    ErrPinMonitor(boost::asio::io_context& io,
                  std::shared_ptr<sdbusplus::asio::connection> conn,
                  const std::string& signalName, const size_t errPin) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), errPin(errPin)
    {
        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::err_pin_monitor
