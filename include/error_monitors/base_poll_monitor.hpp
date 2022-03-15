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
#include <boost/asio/steady_timer.hpp>
#include <error_monitors/base_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::base_poll_monitor
{
static constexpr bool debug = false;


class BasePollMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
  public:
    BasePollMonitor(boost::asio::io_service& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& signalName, size_t pollingTimeMs,
                        size_t timeoutMs) :
        BaseMonitor(io, conn, signalName), pollingTimer(io),
        pollingTimeMs(pollingTimeMs), timeoutMs(timeoutMs)
    {}

    virtual void startPolling()
    {
        timeoutTime = std::chrono::steady_clock::now() +
                      std::chrono::duration<int, std::milli>(timeoutMs);
        poll();
    }

    void hostOn() override
    {
        startPolling();
    }

    size_t getTimeoutMs()
    {
        return timeoutMs;
    }

    void setTimeoutMs(size_t newTimeoutMs)
    {
        timeoutMs = newTimeoutMs;
    }

  private:
    virtual void pollingActions()
    {}

    virtual void handleTimeout()
    {}

    void poll()
    {
        if constexpr (debug)
        {
            std::cerr << "Polling " << signalName << "\n";
        }

        pollingActions();

        if (std::chrono::steady_clock::now() > timeoutTime)
        {
            if constexpr (debug)
            {
                std::cerr << "Polling " << signalName << " timed out\n";
            }
            handleTimeout();
            return;
        }

        pollingTimer.expires_after(std::chrono::milliseconds(pollingTimeMs));
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
            startPolling();
        });
    }

    const size_t pollingTimeMs;
    size_t timeoutMs;
    boost::asio::steady_timer pollingTimer;
    std::chrono::steady_clock::time_point timeoutTime;
};
} // namespace host_error_monitor::base_poll_monitor
