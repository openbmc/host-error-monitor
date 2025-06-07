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

#include <bitset>

namespace host_error_monitor::err_pin_timeout_monitor
{
static constexpr bool debug = false;

class ErrPinTimeoutMonitor :
    public host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor
{
    size_t errPin;
    std::bitset<MAX_CPUS> errPinCPUs;
    const static host_error_monitor::base_gpio_poll_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_poll_monitor::AssertValue::lowAssert;
    const static constexpr size_t errPinPollingTimeMs = 1000;
    const static constexpr size_t errPinTimeoutMs = 90000;

    void logEvent() override
    {
        if (errPinCPUs.none())
        {
            return errPinTimeoutLog();
        }

        for (size_t i = 0; i < errPinCPUs.size(); i++)
        {
            if (errPinCPUs[i])
            {
                errPinTimeoutLog(i);
            }
        }
    }

    void errPinTimeoutLog()
    {
        std::string msg = "ERR" + std::to_string(errPin) + " Timeout";

        log_message(LOG_INFO, msg, "OpenBMC.0.1.CPUError", msg);
    }

    void errPinTimeoutLog(const int cpuNum)
    {
        std::string msg = "ERR" + std::to_string(errPin) + " Timeout on CPU " +
                          std::to_string(cpuNum);

        log_message(LOG_INFO, msg, "OpenBMC.0.1.CPUError", msg);
    }

    void startPolling() override
    {
        checkErrPinCPUs(errPin, errPinCPUs);
        host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor::
            startPolling();
    }

  public:
    ErrPinTimeoutMonitor(boost::asio::io_context& io,
                         std::shared_ptr<sdbusplus::asio::connection> conn,
                         const std::string& signalName, const size_t errPin) :
        BaseGPIOPollMonitor(io, conn, signalName, assertValue,
                            errPinPollingTimeMs, errPinTimeoutMs),
        errPin(errPin)
    {
        if (valid)
        {
            startPolling();
        }
    }
};
} // namespace host_error_monitor::err_pin_timeout_monitor
