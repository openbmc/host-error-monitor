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
#include <error_monitors/base_monitor.hpp>
#include <gpiod.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::base_gpio_monitor
{
static constexpr bool debug = false;

enum class AssertValue
{
    lowAssert = 0,
    highAssert = 1,
};

class BaseGPIOMonitor : public host_error_monitor::base_monitor::BaseMonitor
{
    AssertValue assertValue;

    gpiod::line line;
    boost::asio::posix::stream_descriptor event;

    virtual void logEvent()
    {}

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

        return (line.get_value());
    }

    void checkEvent(bool assertEvent)
    {
        if (assertEvent)
        {
            if constexpr (debug)
            {
                std::cerr << signalName << " asserted\n";
            }

            assertHandler();
        }
        else
        {
            if constexpr (debug)
            {
                std::cerr << signalName << " deasserted\n";
            }

            deassertHandler();
        }
    }

  public:
    virtual void assertHandler()
    {
        std::cerr << signalName << " asserted\n";
        logEvent();
    }

    virtual void deassertHandler()
    {}

  private:
    void waitForEvent()
    {
        if constexpr (debug)
        {
            std::cerr << "Wait for " << signalName << "\n";
        }

        event.async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [this](const boost::system::error_code ec) {
                if (ec)
                {
                    // operation_aborted is expected if wait is canceled.
                    if (ec != boost::asio::error::operation_aborted)
                    {
                        std::cerr << signalName
                                  << " wait error: " << ec.message() << "\n";
                    }
                    return;
                }

                if constexpr (debug)
                {
                    std::cerr << signalName << " event ready\n";
                }

                gpiod::line_event gpioLineEvent = line.event_read();

                // With FLAG_ACTIVE_LOW enabled, both active-high and active-low
                // signals have a RISING_EDGE event when asserted
                checkEvent(gpioLineEvent.event_type ==
                           gpiod::line_event::RISING_EDGE);
                waitForEvent();
            });
    }

  public:
    void startMonitoring()
    {
        if constexpr (debug)
        {
            std::cerr << "Monitoring " << signalName << "\n";
        }

        checkEvent(asserted());
        waitForEvent();
    }

    BaseGPIOMonitor(boost::asio::io_context& io,
                    std::shared_ptr<sdbusplus::asio::connection> conn,
                    const std::string& signalName, AssertValue assertValue) :
        BaseMonitor(io, conn, signalName),
        event(io), assertValue(assertValue)
    {
        if (!requestEvents())
        {
            return;
        }
        valid = true;
    }
};
} // namespace host_error_monitor::base_gpio_monitor
