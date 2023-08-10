/*
// Copyright (c) 2019 Intel Corporation
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
#include <boost/asio/io_context.hpp>
#include <boost/container/flat_map.hpp>
#include <error_monitors.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>
#include <variant>

namespace host_error_monitor
{
static boost::asio::io_context io;
static std::shared_ptr<sdbusplus::asio::connection> conn;

static bool hostOff = true;
bool hostIsOff()
{
    return hostOff;
}

static void init()
{
    static bool initialized = false;

    if (!initialized)
    {
        initialized = true;
        if (!error_monitors::startMonitors(io, conn))
        {
            throw std::runtime_error("Failed to start signal monitors");
        }
    }
}
static void initializeHostState()
{
    // Get the current host state to prepare to start the signal monitors
    conn->async_method_call(
        [](boost::system::error_code ec,
           const std::variant<std::string>& property) {
            if (ec)
            {
                return;
            }
            const std::string* state = std::get_if<std::string>(&property);
            if (state == nullptr)
            {
                std::cerr << "Unable to read host state value\n";
                return;
            }
            hostOff = *state == "xyz.openbmc_project.State.Host.HostState.Off";

            // Now we have the host state, we can init if needed
            init();
        },
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.State.Host", "CurrentHostState");
}

static std::shared_ptr<sdbusplus::bus::match::match> startHostStateMonitor()
{
    return std::make_shared<sdbusplus::bus::match::match>(
        *conn,
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',arg0='xyz.openbmc_project.State.Host'",
        [](sdbusplus::message::message& msg) {
            std::string interfaceName;
            boost::container::flat_map<std::string, std::variant<std::string>>
                propertiesChanged;
            try
            {
                msg.read(interfaceName, propertiesChanged);
            }
            catch (std::exception& e)
            {
                std::cerr << "Unable to read host state\n";
                return;
            }
            // We only want to check for CurrentHostState
            if (propertiesChanged.begin()->first != "CurrentHostState")
            {
                return;
            }
            std::string* state =
                std::get_if<std::string>(&(propertiesChanged.begin()->second));
            if (state == nullptr)
            {
                std::cerr << propertiesChanged.begin()->first
                          << " property invalid\n";
                return;
            }

            hostOff = *state == "xyz.openbmc_project.State.Host.HostState.Off";

            // Now we have the host state, we can init if needed
            init();

            if (!hostOff)
            {
                // Notify error monitors when the host turns on
                error_monitors::sendHostOn();
            }
        });
}
} // namespace host_error_monitor

int main(int /*argc*/, char* /*argv*/[])
{
    // setup connection to dbus
    host_error_monitor::conn =
        std::make_shared<sdbusplus::asio::connection>(host_error_monitor::io);

    // Host Error Monitor Service
    host_error_monitor::conn->request_name(
        "xyz.openbmc_project.HostErrorMonitor");
    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(host_error_monitor::conn);

    // Start tracking host state
    std::shared_ptr<sdbusplus::bus::match::match> hostStateMonitor =
        host_error_monitor::startHostStateMonitor();

    // Initialize the signal monitors
    host_error_monitor::initializeHostState();

    host_error_monitor::io.run();

    return 0;
}
