/*
// Copyright (c) 2021-2022 Intel Corporation
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

namespace host_error_monitor::cpu_thermtrip_monitor
{
class CPUThermtripMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::lowAssert;
    size_t cpuNum;

    void logEvent() override
    {
        cpuThermTripLog();
    }

    void cpuThermTripLog()
    {
        std::string msg = "CPU " + std::to_string(cpuNum) + " thermal trip";

        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUThermalTrip", "REDFISH_MESSAGE_ARGS=%d",
                        cpuNum, NULL);
    }

  public:
    CPUThermtripMonitor(boost::asio::io_service& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& signalName, const size_t cpuNum) :
        BaseGPIOMonitor(io, conn, signalName, assertValue),
        cpuNum(cpuNum)
    {
        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::cpu_thermtrip_monitor
