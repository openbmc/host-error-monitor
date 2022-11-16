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

namespace host_error_monitor::ierr_monitor
{
static constexpr bool debug = true;

class IERRMonitor :
    public host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor
{
    const static host_error_monitor::base_gpio_poll_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_poll_monitor::AssertValue::lowAssert;
    std::shared_ptr<sdbusplus::asio::dbus_interface> assertIERR;
    const static constexpr size_t ierrPollingTimeMs = 100;
    const static constexpr size_t ierrTimeoutMs = 2000;
    const static constexpr size_t ierrTimeoutMsMax =
        600000; // 10 minutes maximum

    const static constexpr uint8_t beepCPUIERR = 4;

    std::shared_ptr<sdbusplus::asio::dbus_interface> associationIERR;
    std::shared_ptr<sdbusplus::asio::dbus_interface> hostErrorTimeoutIface;

    static const constexpr char* callbackMgrPath =
        "/xyz/openbmc_project/CallbackManager";
    static const constexpr char* assertPath =
        "/xyz/openbmc_project/host_error_monitor/processor/IERR";

    void logEvent()
    {
        if (!checkIERRCPUs())
        {
            cpuIERRLog();
        }
    }

    void cpuIERRLog()
    {
        sd_journal_send("MESSAGE=HostError: IERR", "PRIORITY=%i", LOG_INFO,
                        "REDFISH_MESSAGE_ID=%s", "OpenBMC.0.1.CPUError",
                        "REDFISH_MESSAGE_ARGS=%s", "IERR", NULL);
    }

    void cpuIERRLog(const int cpuNum)
    {
        std::string msg = "IERR on CPU " + std::to_string(cpuNum + 1);

        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        msg.c_str(), NULL);
    }

    void cpuIERRLog(const int cpuNum, const std::string& type)
    {
        std::string msg = type + " IERR on CPU " + std::to_string(cpuNum + 1);

        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        msg.c_str(), NULL);
    }

    bool checkIERRCPUs()
    {
        bool cpuIERRFound = false;
        for (size_t cpu = 0, addr = MIN_CLIENT_ADDR; addr <= MAX_CLIENT_ADDR;
             cpu++, addr++)
        {
            EPECIStatus peciStatus = PECI_CC_SUCCESS;
            uint8_t cc = 0;
            CPUModel model{};
            uint8_t stepping = 0;
            if (peci_GetCPUID(addr, &model, &stepping, &cc) != PECI_CC_SUCCESS)
            {
                std::cerr << "Cannot get CPUID!\n";
                continue;
            }

            switch (model)
            {
                case skx:
                {
                    // First check the MCA_ERR_SRC_LOG to see if this is the CPU
                    // that caused the IERR
                    uint32_t mcaErrSrcLog = 0;
                    peciStatus = peci_RdPkgConfig(addr, 0, 5, 4,
                                                  (uint8_t*)&mcaErrSrcLog, &cc);
                    if (peciError(peciStatus, cc))
                    {
                        printPECIError("MCA_ERR_SRC_LOG", addr, peciStatus, cc);
                        continue;
                    }
                    // Check MSMI_INTERNAL (20) and IERR_INTERNAL (27)
                    if ((mcaErrSrcLog & (1 << 20)) ||
                        (mcaErrSrcLog & (1 << 27)))
                    {
                        // TODO: Light the CPU fault LED?
                        cpuIERRFound = true;
                        incrementCPUErrorCount(cpu);
                        // Next check if it's a CPU/VR mismatch by reading the
                        // IA32_MC4_STATUS MSR (0x411)
                        uint64_t mc4Status = 0;
                        peciStatus =
                            peci_RdIAMSR(addr, 0, 0x411, &mc4Status, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("IA32_MC4_STATUS", addr, peciStatus,
                                           cc);
                            continue;
                        }
                        // Check MSEC bits 31:24 for
                        // MCA_SVID_VCCIN_VR_ICC_MAX_FAILURE (0x40),
                        // MCA_SVID_VCCIN_VR_VOUT_FAILURE (0x42), or
                        // MCA_SVID_CPU_VR_CAPABILITY_ERROR (0x43)
                        uint64_t msec = (mc4Status >> 24) & 0xFF;
                        if (msec == 0x40 || msec == 0x42 || msec == 0x43)
                        {
                            cpuIERRLog(cpu, "CPU/VR Mismatch");
                            continue;
                        }

                        // Next check if it's a Core FIVR fault by looking for a
                        // non-zero value of CORE_FIVR_ERR_LOG (B(1) D30 F2
                        // offset 80h)
                        uint32_t coreFIVRErrLog = 0;
                        peciStatus = peci_RdPCIConfigLocal(
                            addr, 1, 30, 2, 0x80, sizeof(uint32_t),
                            (uint8_t*)&coreFIVRErrLog, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("CORE_FIVR_ERR_LOG", addr,
                                           peciStatus, cc);
                            continue;
                        }
                        if (coreFIVRErrLog)
                        {
                            cpuIERRLog(cpu, "Core FIVR Fault");
                            continue;
                        }

                        // Next check if it's an Uncore FIVR fault by looking
                        // for a non-zero value of UNCORE_FIVR_ERR_LOG (B(1) D30
                        // F2 offset 84h)
                        uint32_t uncoreFIVRErrLog = 0;
                        peciStatus = peci_RdPCIConfigLocal(
                            addr, 1, 30, 2, 0x84, sizeof(uint32_t),
                            (uint8_t*)&uncoreFIVRErrLog, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("UNCORE_FIVR_ERR_LOG", addr,
                                           peciStatus, cc);
                            continue;
                        }
                        if (uncoreFIVRErrLog)
                        {
                            cpuIERRLog(cpu, "Uncore FIVR Fault");
                            continue;
                        }

                        // Last if CORE_FIVR_ERR_LOG and UNCORE_FIVR_ERR_LOG are
                        // both zero, but MSEC bits 31:24 have either
                        // MCA_FIVR_CATAS_OVERVOL_FAULT (0x51) or
                        // MCA_FIVR_CATAS_OVERCUR_FAULT (0x52), then log it as
                        // an uncore FIVR fault
                        if (!coreFIVRErrLog && !uncoreFIVRErrLog &&
                            (msec == 0x51 || msec == 0x52))
                        {
                            cpuIERRLog(cpu, "Uncore FIVR Fault");
                            continue;
                        }
                        cpuIERRLog(cpu);
                    }
                    break;
                }
                case icx:
                {
                    // First check the MCA_ERR_SRC_LOG to see if this is the CPU
                    // that caused the IERR
                    uint32_t mcaErrSrcLog = 0;
                    peciStatus = peci_RdPkgConfig(addr, 0, 5, 4,
                                                  (uint8_t*)&mcaErrSrcLog, &cc);
                    if (peciError(peciStatus, cc))
                    {
                        printPECIError("MCA_ERR_SRC_LOG", addr, peciStatus, cc);
                        continue;
                    }
                    // Check MSMI_INTERNAL (20) and IERR_INTERNAL (27)
                    if ((mcaErrSrcLog & (1 << 20)) ||
                        (mcaErrSrcLog & (1 << 27)))
                    {
                        // TODO: Light the CPU fault LED?
                        cpuIERRFound = true;
                        incrementCPUErrorCount(cpu);
                        // Next check if it's a CPU/VR mismatch by reading the
                        // IA32_MC4_STATUS MSR (0x411)
                        uint64_t mc4Status = 0;
                        peciStatus =
                            peci_RdIAMSR(addr, 0, 0x411, &mc4Status, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("IA32_MC4_STATUS", addr, peciStatus,
                                           cc);
                            continue;
                        }
                        // Check MSEC bits 31:24 for
                        // MCA_SVID_VCCIN_VR_ICC_MAX_FAILURE (0x40),
                        // MCA_SVID_VCCIN_VR_VOUT_FAILURE (0x42), or
                        // MCA_SVID_CPU_VR_CAPABILITY_ERROR (0x43)
                        uint64_t msec = (mc4Status >> 24) & 0xFF;
                        if (msec == 0x40 || msec == 0x42 || msec == 0x43)
                        {
                            cpuIERRLog(cpu, "CPU/VR Mismatch");
                            continue;
                        }

                        // Next check if it's a Core FIVR fault by looking for a
                        // non-zero value of CORE_FIVR_ERR_LOG (B(31) D30 F2
                        // offsets C0h and C4h) (Note: Bus 31 is accessed on
                        // PECI as bus 14)
                        uint32_t coreFIVRErrLog0 = 0;
                        uint32_t coreFIVRErrLog1 = 0;
                        peciStatus = peci_RdEndPointConfigPciLocal(
                            addr, 0, 14, 30, 2, 0xC0, sizeof(uint32_t),
                            (uint8_t*)&coreFIVRErrLog0, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("CORE_FIVR_ERR_LOG_0", addr,
                                           peciStatus, cc);
                            continue;
                        }
                        peciStatus = peci_RdEndPointConfigPciLocal(
                            addr, 0, 14, 30, 2, 0xC4, sizeof(uint32_t),
                            (uint8_t*)&coreFIVRErrLog1, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("CORE_FIVR_ERR_LOG_1", addr,
                                           peciStatus, cc);
                            continue;
                        }
                        if (coreFIVRErrLog0 || coreFIVRErrLog1)
                        {
                            cpuIERRLog(cpu, "Core FIVR Fault");
                            continue;
                        }

                        // Next check if it's an Uncore FIVR fault by looking
                        // for a non-zero value of UNCORE_FIVR_ERR_LOG (B(31)
                        // D30 F2 offset 84h) (Note: Bus 31 is accessed on PECI
                        // as bus 14)
                        uint32_t uncoreFIVRErrLog = 0;
                        peciStatus = peci_RdEndPointConfigPciLocal(
                            addr, 0, 14, 30, 2, 0x84, sizeof(uint32_t),
                            (uint8_t*)&uncoreFIVRErrLog, &cc);
                        if (peciError(peciStatus, cc))
                        {
                            printPECIError("UNCORE_FIVR_ERR_LOG", addr,
                                           peciStatus, cc);
                            continue;
                        }
                        if (uncoreFIVRErrLog)
                        {
                            cpuIERRLog(cpu, "Uncore FIVR Fault");
                            continue;
                        }

                        // TODO: Update MSEC/MSCOD_31_24 check
                        // Last if CORE_FIVR_ERR_LOG and UNCORE_FIVR_ERR_LOG are
                        // both zero, but MSEC bits 31:24 have either
                        // MCA_FIVR_CATAS_OVERVOL_FAULT (0x51) or
                        // MCA_FIVR_CATAS_OVERCUR_FAULT (0x52), then log it as
                        // an uncore FIVR fault
                        if (!coreFIVRErrLog0 && !coreFIVRErrLog1 &&
                            !uncoreFIVRErrLog && (msec == 0x51 || msec == 0x52))
                        {
                            cpuIERRLog(cpu, "Uncore FIVR Fault");
                            continue;
                        }
                        cpuIERRLog(cpu);
                    }
                    break;
                }
            }
        }
        return cpuIERRFound;
    }

    void incrementCPUErrorCount(int cpuNum)
    {
        std::string propertyName = "ErrorCountCPU" + std::to_string(cpuNum + 1);

        // Get the current count
        conn->async_method_call(
            [this, propertyName](boost::system::error_code ec,
                                 const std::variant<uint8_t>& property) {
                if (ec)
                {
                    std::cerr << "Failed to read " << propertyName << ": "
                              << ec.message() << "\n";
                    return;
                }
                const uint8_t* errorCountVariant =
                    std::get_if<uint8_t>(&property);
                if (errorCountVariant == nullptr)
                {
                    std::cerr << propertyName << " invalid\n";
                    return;
                }
                uint8_t errorCount = *errorCountVariant;
                if (errorCount == std::numeric_limits<uint8_t>::max())
                {
                    std::cerr << "Maximum error count reached\n";
                    return;
                }
                // Increment the count
                errorCount++;
                conn->async_method_call(
                    [propertyName](boost::system::error_code ec) {
                        if (ec)
                        {
                            std::cerr << "Failed to set " << propertyName
                                      << ": " << ec.message() << "\n";
                        }
                    },
                    "xyz.openbmc_project.Settings",
                    "/xyz/openbmc_project/control/processor_error_config",
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.Control.Processor.ErrConfig",
                    propertyName, std::variant<uint8_t>{errorCount});
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/processor_error_config",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.Processor.ErrConfig", propertyName);
    }

    void assertHandler() override
    {
        host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor::
            assertHandler();

        setLED();
        assertIERR->set_property("Asserted", true);

        beep(conn, beepCPUIERR);

        conn->async_method_call(
            [this](boost::system::error_code ec,
                   const std::variant<bool>& property) {
                if (ec)
                {
                    return;
                }
                const bool* reset = std::get_if<bool>(&property);
                if (reset == nullptr)
                {
                    std::cerr << "Unable to read reset on CATERR value\n";
                    return;
                }
                startCrashdumpAndRecovery(conn, *reset, "IERR");
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/processor_error_config",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.Processor.ErrConfig", "ResetOnCATERR");
    }

    void deassertHandler() override
    {
        unsetLED();
        assertIERR->set_property("Asserted", false);
    }

    void setLED()
    {
        std::vector<Association> associations;

        associations.emplace_back(
            "", "critical", "/xyz/openbmc_project/host_error_monitor/ierr");
        associations.emplace_back("", "critical", callbackMgrPath);

        associationIERR->set_property("Associations", associations);
    }

    void unsetLED()
    {
        std::vector<Association> associations;

        associations.emplace_back("", "", "");

        associationIERR->set_property("Associations", associations);
    }

  public:
    IERRMonitor(boost::asio::io_service& io,
                std::shared_ptr<sdbusplus::asio::connection> conn,
                const std::string& signalName) :
        BaseGPIOPollMonitor(io, conn, signalName, assertValue,
                            ierrPollingTimeMs, ierrTimeoutMs)
    {
        // Associations interface for led status
        std::vector<host_error_monitor::Association> associations;
        associations.emplace_back("", "", "");

        sdbusplus::asio::object_server server =
            sdbusplus::asio::object_server(conn);
        associationIERR =
            server.add_interface("/xyz/openbmc_project/host_error_monitor/ierr",
                                 "xyz.openbmc_project.Association.Definitions");
        associationIERR->register_property("Associations", associations);
        associationIERR->initialize();

        hostErrorTimeoutIface = server.add_interface(
            "/xyz/openbmc_project/host_error_monitor",
            "xyz.openbmc_project.HostErrorMonitor.Timeout");

        hostErrorTimeoutIface->register_property(
            "IERRTimeoutMs", ierrTimeoutMs,
            [this](const std::size_t& requested, std::size_t& resp) {
                if (requested > ierrTimeoutMsMax)
                {
                    std::cerr << "IERRTimeoutMs update to " << requested
                              << "ms rejected. Cannot be greater than "
                              << ierrTimeoutMsMax << "ms.\n";
                    return 0;
                }
                std::cerr << "IERRTimeoutMs updated to " << requested << "ms\n";
                setTimeoutMs(requested);
                resp = requested;
                return 1;
            },
            [this](std::size_t& resp) { return getTimeoutMs(); });
        hostErrorTimeoutIface->initialize();

        assertIERR = server.add_interface(
            assertPath, "xyz.openbmc_project.HostErrorMonitor.Processor.IERR");
        assertIERR->register_property("Asserted", false);
        assertIERR->initialize();

        if (valid)
        {
            startPolling();
        }
    }
};
} // namespace host_error_monitor::ierr_monitor
