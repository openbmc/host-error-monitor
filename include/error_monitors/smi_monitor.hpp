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
#include "utils.hpp"
#include <systemd/sd-journal.h>

#include <error_monitors/base_gpio_poll_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <boost/asio.hpp>

#include <iostream>

namespace host_error_monitor::smi_monitor
{
static constexpr bool debug = false;
static constexpr size_t eSpiMessageSize = 1;

class SMIMonitor :
    public host_error_monitor::base_gpio_poll_monitor::BaseGPIOPollMonitor
{
    const static host_error_monitor::base_gpio_poll_monitor::AssertValue
        assertValue =
            host_error_monitor::base_gpio_poll_monitor::AssertValue::lowAssert;
    const static constexpr size_t smiPollingTimeMs = 1000;
    const static constexpr size_t smiTimeoutMs = 90000;
	std::array<uint8_t, eSpiMessageSize> eSpiBuffer = {0};
	bool smiIsRecieved = false;
	int eSpiFd = -1;
	boost::asio::steady_timer pollingTimer;
    std::chrono::steady_clock::time_point timeoutTime;
	std::unique_ptr<boost::asio::posix::stream_descriptor> eSpiDev = nullptr;
	
	
	void startPolling() override
	{
		constexpr const char* eSpidevName = "/dev/espi-smi"; 
		eSpiFd = open(eSpidevName, O_RDONLY | O_NONBLOCK);
		if (eSpiFd < 0)
		{
			throw std::runtime_error("Couldn't open eSPI dev");
		}
		eSpiDev = std::make_unique<boost::asio::posix::stream_descriptor>(io, eSpiFd);

        if constexpr (debug)
        {
            std::cerr << "Polling " << signalName << "\n";
        } 
		asyncReadeSpi();
	}
	
	void asyncReadeSpi(void)
	{
		std::cerr << "Testing SMIInterrupt... " << "\n";
		boost::asio::async_read(
            *eSpiDev, boost::asio::buffer(eSpiBuffer, eSpiBuffer.size()),
        boost::asio::transfer_exactly(1),
            [this](const boost::system::error_code& ec, size_t rlen) {
                if (ec || rlen < 1)
                {
                    channelAbort(io, "Failed to read eSPI", ec);
                    return;
                }
               uint8_t eSpiVal = std::get<0>(eSpiBuffer);
			   if (eSpiVal == '1')
			   {
				   std::cerr << "SMIInterrupt " << "\n";
				   timeoutTime = std::chrono::steady_clock::now() +
                      std::chrono::duration<int, std::milli>(smiTimeoutMs);
				   while(eSpiVal == '1' && timeoutTime > std::chrono::steady_clock::now())
				   {
					   boost::asio::read(*eSpiDev, boost::asio::buffer(eSpiBuffer, eSpiBuffer.size()));
					   eSpiVal = std::get<0>(eSpiBuffer);
					   pollingTimer.expires_after(std::chrono::milliseconds(smiPollingTimeMs));
					   pollingTimer.async_wait([this](const boost::system::error_code ec) {
            if (ec)
            {
                // operation_aborted is expected if timer is canceled before
                // completion.
                if (ec != boost::asio::error::operation_aborted)
                {
                    std::cerr << signalName
                              << " polling async_wait failed: " << ec.message()
                              << "\n";
                }
                return;
            }
            //poll();
        });
				   }
				if(timeoutTime < std::chrono::steady_clock::now())
				{
					assertHandler();
				}
	               
			   }
			   
			   else
			   {
				   std::cerr << "!SMIInterrupt " << "\n";
			   }
                asyncReadeSpi();
            });
		
	}

    void logEvent() override
    {
        sd_journal_send("MESSAGE=HostError: SMI Timeout", "PRIORITY=%i",
                        LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                        "OpenBMC.0.1.CPUError", "REDFISH_MESSAGE_ARGS=%s",
                        "SMI Timeout", NULL);
    }

    void assertHandler() override
    {
        BaseGPIOPollMonitor::assertHandler();

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
                    std::cerr << "Unable to read reset on " << signalName
                              << " value\n";
                    return;
                }
#ifdef HOST_ERROR_CRASHDUMP_ON_SMI_TIMEOUT
                startCrashdumpAndRecovery(io, conn, *reset, "SMI Timeout");
#else
                if (*reset)
                {
                    std::cout << "Recovering the system\n";
                    startWarmReset(conn);
                }
#endif
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/bmc_reset_disables",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.ResetDisables", "ResetOnSMI");
    }

  public:
    SMIMonitor(boost::asio::io_service& io,
               std::shared_ptr<sdbusplus::asio::connection> conn,
               const std::string& signalName) :pollingTimer(io),
        BaseGPIOPollMonitor(io, conn, signalName, assertValue, smiPollingTimeMs,
                            smiTimeoutMs)

    {
        if (valid)
        {
            startPolling();
        }
    }
};
} // namespace host_error_monitor::smi_monitor
