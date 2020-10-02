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
#include <systemd/sd-journal.h>

#include <error_monitors/base_gpio_poll_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <bitset>

namespace host_error_monitor::err_pin_monitor
{
static constexpr bool debug = false;

class ErrPinMonitor :
    public host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor
{
    size_t errPin;
    std::bitset<MAX_CPUS> errPinCPUs;
    const static host_error_monitor::base_gpio_poll_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_poll_monitor::AssertValue::lowAssert;
    const static constexpr size_t errPinPollingTimeMs = 1000;
    const static constexpr size_t errPinTimeoutMs = 90000;

    void logEvent()
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

        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        msg.c_str(), NULL);
    }

    void errPinTimeoutLog(const int cpuNum)
    {
        std::string msg = "ERR" + std::to_string(errPin) + " Timeout on CPU " +
                          std::to_string(cpuNum + 1);

        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        msg.c_str(), NULL);
    }

    void checkErrPinCPUs()
    {
        errPinCPUs.reset();
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
                    peciStatus = peci_RdPCIConfigLocal(
                        addr, 0, 8, 0, 0x210, sizeof(uint32_t),
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
            }
        }
    }

    void startPolling() override
    {
        checkErrPinCPUs();
        host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor::
            startPolling();
    }

  public:
    ErrPinMonitor(boost::asio::io_service& io,
                  std::shared_ptr<sdbusplus::asio::connection> conn,
                  const std::string& signalName, const size_t errPin) :
        BaseGPIOPollMonitor(io, conn, signalName, assertValue,
                            errPinPollingTimeMs, errPinTimeoutMs),
        errPin(errPin)
    {}
};
} // namespace host_error_monitor::err_pin_monitor
