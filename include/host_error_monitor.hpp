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
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor
{
using Association = std::tuple<std::string, std::string, std::string>;

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
    static bool recover;
    recover = recoverSystem;
    std::cerr << "Starting crashdump\n";
    static std::shared_ptr<sdbusplus::bus::match::match> crashdumpCompleteMatch;

    if (!crashdumpCompleteMatch)
    {
        crashdumpCompleteMatch = std::make_shared<sdbusplus::bus::match::match>(
            *conn,
            "type='signal',interface='com.intel.crashdump.Stored',member='"
            "CrashdumpComplete'",
            [conn](sdbusplus::message::message& msg) {
                std::cerr << "Crashdump completed\n";
                if (recover)
                {
                    std::cerr << "Recovering the system\n";
                    startWarmReset(conn);
                }
                crashdumpCompleteMatch.reset();
            });
    }

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

static inline bool peciError(EPECIStatus peciStatus, uint8_t cc)
{
    return (
        peciStatus != PECI_CC_SUCCESS ||
        (cc != PECI_DEV_CC_SUCCESS && cc != PECI_DEV_CC_FATAL_MCA_DETECTED));
}

static void printPECIError(const std::string& reg, const size_t addr,
                           const EPECIStatus peciStatus, const size_t cc)
{
    std::cerr << "Failed to read " << reg << " on CPU address " << addr
              << ". Error: " << peciStatus << ": cc: 0x" << std::hex << cc
              << "\n";
}

static void beep(std::shared_ptr<sdbusplus::asio::connection> conn,
                 const uint8_t& beepPriority)
{
    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "beep returned error with "
                             "async_method_call (ec = "
                          << ec << ")\n";
                return;
            }
        },
        "xyz.openbmc_project.BeepCode", "/xyz/openbmc_project/BeepCode",
        "xyz.openbmc_project.BeepCode", "Beep", uint8_t(beepPriority));
}

} // namespace host_error_monitor
