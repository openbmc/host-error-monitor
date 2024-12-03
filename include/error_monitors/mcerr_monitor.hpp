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
#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace host_error_monitor::mcerr_monitor
{
class MCERRMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const size_t cpuNum;
    const bool cpuNumValid;

    void logEvent() override
    {
        std::string msg;
        if (cpuNumValid) {
            msg = "Machine Check Error on CPU " + std::to_string(cpuNum);
        } else {
            msg = "Machine Check Error";
        }

        log_message(LOG_INFO, msg, "OpenBMC.0.1.CPUError", msg);
    }

  public:
    MCERRMonitor(
        boost::asio::io_context& io,
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::string& signalName,
        const host_error_monitor::base_gpio_monitor::AssertValue assertValue) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), cpuNum(0), cpuNumValid(false)
    {
        if (valid)
        {
            startMonitoring();
        }
    }

    MCERRMonitor(
        boost::asio::io_context& io,
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::string& signalName,
        const host_error_monitor::base_gpio_monitor::AssertValue assertValue,
        const size_t cpuNum) :
        BaseGPIOMonitor(io, conn, signalName, assertValue), cpuNum(cpuNum), cpuNumValid(true)
    {
        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::mcerr_monitor
