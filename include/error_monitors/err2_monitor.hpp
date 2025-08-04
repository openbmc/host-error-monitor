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
#include <error_monitors/err_pin_timeout_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::err2_monitor
{
static constexpr bool debug = false;

class Err2Monitor :
    public host_error_monitor::err_pin_timeout_monitor::ErrPinTimeoutMonitor
{
    const static constexpr uint8_t beepCPUErr2 = 5;

    std::shared_ptr<sdbusplus::asio::dbus_interface> associationERR2;

    static const constexpr char* callbackMgrPath =
        "/xyz/openbmc_project/CallbackManager";

    void assertHandler() override
    {
        host_error_monitor::err_pin_timeout_monitor::ErrPinTimeoutMonitor::
            assertHandler();

        setLED();

        beep(conn, beepCPUErr2);

        conn->async_method_call(
            [this](boost::system::error_code ec,
                   const std::variant<bool>& property) {
                // Default to no reset after Crashdump
                RecoveryType recovery = RecoveryType::noRecovery;
                if (!ec)
                {
                    const bool* resetPtr = std::get_if<bool>(&property);
                    if (resetPtr == nullptr)
                    {
                        std::cerr << "Unable to read reset on ERR2 value\n";
                    }
                    else if (*resetPtr)
                    {
                        recovery = RecoveryType::warmReset;
                    }
                }
                startCrashdumpAndRecovery(conn, recovery, "ERR2_Timeout");
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/processor_error_config",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.Processor.ErrConfig", "ResetOnERR2");
    }

    void deassertHandler() override
    {
        ErrPinTimeoutMonitor::deassertHandler();

        unsetLED();
    }

    void setLED()
    {
        std::vector<Association> associations;

        associations.emplace_back(
            "", "critical", "/xyz/openbmc_project/host_error_monitor/err2");
        associations.emplace_back("", "critical", callbackMgrPath);

        associationERR2->set_property("Associations", associations);
    }

    void unsetLED()
    {
        std::vector<Association> associations;

        associations.emplace_back("", "", "");

        associationERR2->set_property("Associations", associations);
    }

  public:
    Err2Monitor(boost::asio::io_context& io,
                std::shared_ptr<sdbusplus::asio::connection> conn,
                const std::string& signalName) :
        host_error_monitor::err_pin_timeout_monitor::ErrPinTimeoutMonitor(
            io, conn, signalName, 2)
    {
        // Associations interface for led status
        std::vector<host_error_monitor::Association> associations;
        associations.emplace_back("", "", "");

        sdbusplus::asio::object_server server =
            sdbusplus::asio::object_server(conn);
        associationERR2 =
            server.add_interface("/xyz/openbmc_project/host_error_monitor/err2",
                                 "xyz.openbmc_project.Association.Definitions");
        associationERR2->register_property("Associations", associations);
        associationERR2->initialize();
    }
};
} // namespace host_error_monitor::err2_monitor
