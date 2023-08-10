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
#ifdef LIBPECI
#include <peci.h>
#else
#define MAX_CPUS 8
#endif

#include <sdbusplus/asio/object_server.hpp>

#include <bitset>
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

enum class RecoveryType
{
    noRecovery,
    powerCycle,
    warmReset,
};

static inline void
    handleRecovery(RecoveryType recovery,
                   std::shared_ptr<sdbusplus::asio::connection> conn)
{
    switch (recovery)
    {
        case RecoveryType::noRecovery:
            std::cerr << "Recovery is disabled. Leaving the system "
                         "in the failed state.\n";
            break;
        case RecoveryType::powerCycle:
            std::cerr << "Recovering the system with a power cycle\n";
            startPowerCycle(conn);
            break;
        case RecoveryType::warmReset:
            std::cerr << "Recovering the system with a warm reset\n";
            startWarmReset(conn);
            break;
    }
}

void startCrashdumpAndRecovery(
    std::shared_ptr<sdbusplus::asio::connection> conn,
    RecoveryType requestedRecovery, const std::string& triggerType)
{
#ifdef CRASHDUMP
    static RecoveryType recovery;
    recovery = requestedRecovery;
    std::cerr << "Starting crashdump\n";
    static std::shared_ptr<sdbusplus::bus::match::match> crashdumpCompleteMatch;

    if (!crashdumpCompleteMatch)
    {
        crashdumpCompleteMatch = std::make_shared<sdbusplus::bus::match::match>(
            *conn,
            "type='signal',interface='com.intel.crashdump',member='"
            "CrashdumpComplete'",
            [conn](sdbusplus::message::message& msg) {
                std::cerr << "Crashdump completed\n";
                handleRecovery(recovery, conn);
                crashdumpCompleteMatch.reset();
            });
    }

    conn->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                if (ec.value() == boost::system::errc::device_or_resource_busy)
                {
                    std::cerr << "Crashdump already in progress. Waiting for "
                                 "completion signal\n";
                    return;
                }

                std::cerr << "failed to start Crashdump\n";
            }
        },
        "com.intel.crashdump", "/com/intel/crashdump",
        "com.intel.crashdump.Stored", "GenerateStoredLog", triggerType);
#endif
}

#ifdef LIBPECI
static inline bool peciError(EPECIStatus peciStatus, uint8_t cc)
{
    return (
        peciStatus != PECI_CC_SUCCESS ||
        (cc != PECI_DEV_CC_SUCCESS && cc != PECI_DEV_CC_FATAL_MCA_DETECTED));
}

static void printPECIError(const std::string& reg, const size_t addr,
                           const EPECIStatus peciStatus, const size_t cc)
{
    std::cerr << "Failed to read " << reg << " on CPU address " << std::dec
              << addr << ". Error: " << peciStatus << ": cc: 0x" << std::hex
              << cc << "\n";
}
#endif

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

static void checkErrPinCPUs(const size_t errPin,
                            std::bitset<MAX_CPUS>& errPinCPUs)
{
    errPinCPUs.reset();
#ifdef LIBPECI
    for (size_t cpu = 0, addr = MIN_CLIENT_ADDR; addr <= MAX_CLIENT_ADDR;
         cpu++, addr++)
    {
        EPECIStatus peciStatus = PECI_CC_SUCCESS;
        uint8_t cc = 0;
        CPUModel model{};
        uint8_t stepping = 0;
        peciStatus = peci_GetCPUID(addr, &model, &stepping, &cc);
        if (peciStatus != PECI_CC_SUCCESS)
        {
            if (peciStatus != PECI_CC_CPU_NOT_PRESENT)
            {
                printPECIError("CPUID", addr, peciStatus, cc);
            }
            continue;
        }

        switch (model)
        {
            case skx:
            {
                // Check the ERRPINSTS to see if this is the CPU that
                // caused the ERRx (B(0) D8 F0 offset 210h)
                uint32_t errpinsts = 0;
                peciStatus = peci_RdPCIConfigLocal(addr, 0, 8, 0, 0x210,
                                                   sizeof(uint32_t),
                                                   (uint8_t*)&errpinsts, &cc);
                if (peciError(peciStatus, cc))
                {
                    printPECIError("ERRPINSTS", addr, peciStatus, cc);
                    continue;
                }

                errPinCPUs[cpu] = (errpinsts & (1 << errPin)) != 0;
                break;
            }
            case icx:
            {
                // Check the ERRPINSTS to see if this is the CPU that
                // caused the ERRx (B(30) D0 F3 offset 274h) (Note: Bus
                // 30 is accessed on PECI as bus 13)
                uint32_t errpinsts = 0;
                peciStatus = peci_RdEndPointConfigPciLocal(
                    addr, 0, 13, 0, 3, 0x274, sizeof(uint32_t),
                    (uint8_t*)&errpinsts, &cc);
                if (peciError(peciStatus, cc))
                {
                    printPECIError("ERRPINSTS", addr, peciStatus, cc);
                    continue;
                }

                errPinCPUs[cpu] = (errpinsts & (1 << errPin)) != 0;
                break;
            }
            default:
            {
                std::cerr << "Unsupported CPU Model: 0x" << std::hex
                          << static_cast<int>(model) << std::dec << "\n";
            }
        }
    }
#endif
}

} // namespace host_error_monitor
