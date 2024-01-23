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
#include <error_monitors/base_gpio_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace host_error_monitor::pch_thermtrip_monitor
{
class PCHThermtripMonitor :
    public host_error_monitor::base_gpio_monitor::BaseGPIOMonitor
{
    const static host_error_monitor::base_gpio_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_monitor::AssertValue::lowAssert;

    std::shared_ptr<sdbusplus::asio::dbus_interface> associationPCHThermtrip;
    static const constexpr char* callbackMgrPath =
        "/xyz/openbmc_project/CallbackManager";

    void logEvent() override
    {
        log_message(LOG_INFO, "SSB thermal trip", "OpenBMC.0.1.SsbThermalTrip",
                    "");
    }

    void assertHandler() override
    {
        host_error_monitor::base_gpio_monitor::BaseGPIOMonitor::assertHandler();

        setLED();
    }

    void deassertHandler() override
    {
        host_error_monitor::base_gpio_monitor::BaseGPIOMonitor::
            deassertHandler();

        unsetLED();
    }

    void setLED()
    {
        std::vector<Association> associations;

        associations.emplace_back(
            "", "critical",
            "/xyz/openbmc_project/host_error_monitor/ssb_thermal_trip");
        associations.emplace_back("", "critical", callbackMgrPath);

        associationPCHThermtrip->set_property("Associations", associations);
    }

    void unsetLED()
    {
        std::vector<Association> associations;

        associations.emplace_back("", "", "");

        associationPCHThermtrip->set_property("Associations", associations);
    }

  public:
    PCHThermtripMonitor(boost::asio::io_context& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& signalName) :
        BaseGPIOMonitor(io, conn, signalName, assertValue)
    {
        // Associations interface for led status
        std::vector<host_error_monitor::Association> associations;
        associations.emplace_back("", "", "");

        sdbusplus::asio::object_server server =
            sdbusplus::asio::object_server(conn);
        associationPCHThermtrip = server.add_interface(
            "/xyz/openbmc_project/host_error_monitor/ssb_thermal_trip",
            "xyz.openbmc_project.Association.Definitions");
        associationPCHThermtrip->register_property("Associations",
                                                   associations);
        associationPCHThermtrip->initialize();

        if (valid)
        {
            startMonitoring();
        }
    }
};
} // namespace host_error_monitor::pch_thermtrip_monitor
