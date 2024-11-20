/*
// Copyright (c) 2024 9elements GmbH
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
#include <boost/asio/steady_timer.hpp>

#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <bitset>

namespace host_error_monitor::err_pin_sample_monitor
{
static constexpr bool debug = false;

class CatErrMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::lowAssert;
    const size_t cpuNum;
    const bool cpuNumValid;

    void catErrLog(void)
    {
        std::string msg;

        if (cpuNumValid) {
            msg = "CATERR on CPU" + std::to_string(cpuNum);
        } else {
            msg = "CATERR on one of the CPUs";
        }

        std::cerr << "CatErrMonitor " << msg << "\n";
        log_message(LOG_ERR, msg, "OpenBMC.0.1.CPUError", msg);
    }

    void assertHandler() override
    {
        catErrLog();
    }

    void deassertHandler() override
    {
    }

  public:
    CatErrMonitor(boost::asio::io_context& io,
                  std::shared_ptr<sdbusplus::asio::connection> conn,
                  const std::string& signalName) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), cpuNum(0), cpuNumValid(false)
    {
        if (valid)
        {
            startMonitoring();
        }
    }

    CatErrMonitor(boost::asio::io_context& io,
                  std::shared_ptr<sdbusplus::asio::connection> conn,
                  const std::string& signalName,
                  const size_t cpuNum) :
        BaseGPIOMonitor(io, conn, signalName, assertValue),cpuNum(cpuNum), cpuNumValid(true)
    {
        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::err_pin_sample_monitor
