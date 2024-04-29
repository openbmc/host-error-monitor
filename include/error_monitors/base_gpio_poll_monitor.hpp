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
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <error_monitors/base_monitor.hpp>
#include <gpiod.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::base_gpio_poll_monitor
{
static constexpr bool debug = false;

enum class AssertValue
{
    lowAssert = 0,
    highAssert = 1,
};

class BaseGPIOPollMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
    boost::asio::steady_timer pollingTimer;
    std::chrono::steady_clock::time_point timeoutTime;

    gpiod::line line;
    boost::asio::posix::stream_descriptor event;

    AssertValue assertValue;
    size_t pollingTimeMs;
    size_t timeoutMs;

    virtual void logEvent() {}

    bool requestEvents()
    {
        line = gpiod::find_line(signalName);
        if (!line)
        {
            std::cerr << "Failed to find the " << signalName << " line\n";
            return false;
        }

        try
        {
            line.request({"host-error-monitor",
                          gpiod::line_request::EVENT_BOTH_EDGES,
                          assertValue == AssertValue::highAssert
                              ? 0
                              : gpiod::line_request::FLAG_ACTIVE_LOW});
        }
        catch (std::exception&)
        {
            std::cerr << "Failed to request events for " << signalName << "\n";
            return false;
        }

        int lineFd = line.event_get_fd();
        if (lineFd < 0)
        {
            std::cerr << "Failed to get " << signalName << " fd\n";
            return false;
        }

        event.assign(lineFd);

        return true;
    }

    bool asserted()
    {
        if constexpr (debug)
        {
            std::cerr << "Checking " << signalName << " state\n";
        }

        if (hostIsOff())
        {
            if constexpr (debug)
            {
                std::cerr << "Host is off\n";
            }
            return false;
        }

        return (line.get_value());
    }

  public:
    virtual void assertHandler()
    {
        std::cerr << signalName << " asserted for " << std::to_string(timeoutMs)
                  << " ms\n";
        logEvent();
    }

    virtual void deassertHandler() {}

  private:
    void flushEvents()
    {
        if constexpr (debug)
        {
            std::cerr << "Flushing " << signalName << " events\n";
        }

        while (true)
        {
            try
            {
                line.event_read();
            }
            catch (std::system_error&)
            {
                break;
            }
        }
    }

    void waitForEvent()
    {
        if constexpr (debug)
        {
            std::cerr << "Wait for " << signalName << "\n";
        }

        event.async_wait(boost::asio::posix::stream_descriptor::wait_read,
                         [this](const boost::system::error_code ec) {
            if (ec)
            {
                // operation_aborted is expected if wait is canceled.
                if (ec != boost::asio::error::operation_aborted)
                {
                    std::cerr << signalName << " wait error: " << ec.message()
                              << "\n";
                }
                return;
            }

            if constexpr (debug)
            {
                std::cerr << signalName << " event ready\n";
            }

            startPolling();
        });
    }

  public:
    virtual void startPolling()
    {
        timeoutTime = std::chrono::steady_clock::now() +
                      std::chrono::duration<int, std::milli>(timeoutMs);
        poll();
    }

  private:
    void poll()
    {
        if constexpr (debug)
        {
            std::cerr << "Polling " << signalName << "\n";
        }

        flushEvents();

        if (!asserted())
        {
            if constexpr (debug)
            {
                std::cerr << signalName << " not asserted\n";
            }

            deassertHandler();
            waitForEvent();
            return;
        }
        if constexpr (debug)
        {
            std::cerr << signalName << " asserted\n";
        }

        if (std::chrono::steady_clock::now() > timeoutTime)
        {
            assertHandler();
            waitForEvent();
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
            poll();
        });
    }

  public:
    BaseGPIOPollMonitor(boost::asio::io_context& io,
                        std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& signalName, AssertValue assertValue,
                        size_t pollingTimeMs, size_t timeoutMs) :
        BaseMonitor(io, conn, signalName), pollingTimer(io), event(io),
        assertValue(assertValue), pollingTimeMs(pollingTimeMs),
        timeoutMs(timeoutMs)
    {
        if (!requestEvents())
        {
            return;
        }
        event.non_blocking(true);
        valid = true;
    }

    void hostOn() override
    {
        event.cancel();
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
};
} // namespace host_error_monitor::base_gpio_poll_monitor
