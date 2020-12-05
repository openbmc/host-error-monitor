/*
// Copyright (c) 2020 Intel Corporation
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
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor
{
bool hostIsOff();

void startPowerCycle(std::shared_ptr<sdbusplus::asio::connection> conn)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set Chassis State\n";
            }
        },
        "xyz.openbmc_project.State.Chassis",
        "/xyz/openbmc_project/state/chassis0",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.State.Chassis", "RequestedPowerTransition",
        std::variant<std::string>{
            "xyz.openbmc_project.State.Chassis.Transition.PowerCycle"});
}

void startWarmReset(std::shared_ptr<sdbusplus::asio::connection> conn)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to set Host State\n";
            }
        },
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.State.Host", "RequestedHostTransition",
        std::variant<std::string>{
            "xyz.openbmc_project.State.Host.Transition.ForceWarmReboot"});
}

void startCrashdumpAndRecovery(
    std::shared_ptr<sdbusplus::asio::connection> conn, bool recoverSystem,
    const std::string& triggerType)
{
    std::cerr << "Starting crashdump\n";
    static std::shared_ptr<sdbusplus::bus::match::match> crashdumpCompleteMatch;

    crashdumpCompleteMatch = std::make_shared<sdbusplus::bus::match::match>(
        *conn,
        "type='signal',interface='com.intel.crashdump.Stored',member='"
        "CrashdumpComplete'",
        [conn, recoverSystem](sdbusplus::message::message& msg) {
            std::cerr << "Crashdump completed\n";
            if (recoverSystem)
            {
                std::cerr << "Recovering the system\n";
                startWarmReset(conn);
            }
            crashdumpCompleteMatch.reset();
        });

    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "failed to start Crashdump\n";
            }
        },
        "com.intel.crashdump", "/com/intel/crashdump",
        "com.intel.crashdump.Stored", "GenerateStoredLog", triggerType);
}

} // namespace host_error_monitor
