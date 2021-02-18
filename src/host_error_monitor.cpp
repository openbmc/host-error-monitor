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
#include <peci.h>
#include <systemd/sd-journal.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <error_monitors.hpp>
#include <gpiod.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <bitset>
#include <iostream>
#include <variant>

namespace host_error_monitor
{
static boost::asio::io_service io;
static std::shared_ptr<sdbusplus::asio::connection> conn;

static std::shared_ptr<sdbusplus::asio::dbus_interface> associationSSBThermTrip;

static const constexpr char* rootPath = "/xyz/openbmc_project/CallbackManager";

static bool hostOff = true;
bool hostIsOff()
{
    return hostOff;
}

// GPIO Lines and Event Descriptors
//----------------------------------
// PCH_BMC_THERMTRIP function related definition
//----------------------------------
static gpiod::line pchThermtripLine;
static boost::asio::posix::stream_descriptor pchThermtripEvent(io);

static void ssbThermTripLog()
{
    sd_journal_send("MESSAGE=HostError: SSB thermal trip", "PRIORITY=%i",
                    LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                    "OpenBMC.0.1.SsbThermalTrip", NULL);
}

static void initializeErrorState();
static void init()
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
            // If the system is on, initialize the error state
            if (!hostOff)
            {
                initializeErrorState();
            }

            // Now we have the host state, start the signal monitors
            if (!error_monitors::startMonitors(io, conn))
            {
                throw std::runtime_error("Failed to start signal monitors");
            }
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

            if (!hostOff)
            {
                // Handle any initial errors when the host turns on
                initializeErrorState();
                error_monitors::sendHostOn();
            }
        });
}

static bool requestGPIOEvents(
    const std::string& name, const std::function<void()>& handler,
    gpiod::line& gpioLine,
    boost::asio::posix::stream_descriptor& gpioEventDescriptor)
{
    // Find the GPIO line
    gpioLine = gpiod::find_line(name);
    if (!gpioLine)
    {
        std::cerr << "Failed to find the " << name << " line\n";
        return false;
    }

    try
    {
        gpioLine.request(
            {"host-error-monitor", gpiod::line_request::EVENT_BOTH_EDGES});
    }
    catch (std::exception&)
    {
        std::cerr << "Failed to request events for " << name << "\n";
        return false;
    }

    int gpioLineFd = gpioLine.event_get_fd();
    if (gpioLineFd < 0)
    {
        std::cerr << "Failed to get " << name << " fd\n";
        return false;
    }

    gpioEventDescriptor.assign(gpioLineFd);

    gpioEventDescriptor.async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [&name, handler](const boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << name << " fd handler error: " << ec.message()
                          << "\n";
                return;
            }
            handler();
        });
    return true;
}

static bool requestGPIOInput(const std::string& name, gpiod::line& gpioLine)
{
    // Find the GPIO line
    gpioLine = gpiod::find_line(name);
    if (!gpioLine)
    {
        std::cerr << "Failed to find the " << name << " line.\n";
        return false;
    }

    // Request GPIO input
    try
    {
        gpioLine.request({__FUNCTION__, gpiod::line_request::DIRECTION_INPUT});
    }
    catch (std::exception&)
    {
        std::cerr << "Failed to request " << name << " input\n";
        return false;
    }

    return true;
}

static void pchThermtripHandler()
{
    std::vector<Association> associations;

    gpiod::line_event gpioLineEvent = pchThermtripLine.event_read();

    bool pchThermtrip =
        gpioLineEvent.event_type == gpiod::line_event::FALLING_EDGE;
    if (pchThermtrip)
    {
        ssbThermTripLog();
        associations.emplace_back(
            "", "critical",
            "/xyz/openbmc_project/host_error_monitor/ssb_thermal_trip");
        associations.emplace_back("", "critical", host_error_monitor::rootPath);
    }
    else
    {
        associations.emplace_back("", "", "");
    }
    host_error_monitor::associationSSBThermTrip->set_property("Associations",
                                                              associations);

    pchThermtripEvent.async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        [](const boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "PCH Thermal trip handler error: " << ec.message()
                          << "\n";
                return;
            }
            pchThermtripHandler();
        });
}

static void initializeErrorState()
{
    // Handle PCH_BMC_THERMTRIP if it's asserted now
    if (pchThermtripLine.get_value() == 0)
    {
        ssbThermTripLog();
        std::vector<Association> associations;
        associations.emplace_back(
            "", "critical",
            "/xyz/openbmc_project/host_error_monitor/ssb_thermal_trip");
        associations.emplace_back("", "critical", host_error_monitor::rootPath);
        host_error_monitor::associationSSBThermTrip->set_property(
            "Associations", associations);
    }
}
} // namespace host_error_monitor

int main(int argc, char* argv[])
{
    // setup connection to dbus
    host_error_monitor::conn =
        std::make_shared<sdbusplus::asio::connection>(host_error_monitor::io);

    // Host Error Monitor Service
    host_error_monitor::conn->request_name(
        "xyz.openbmc_project.HostErrorMonitor");
    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(host_error_monitor::conn);

    // Associations interface for led status
    std::vector<host_error_monitor::Association> associations;
    associations.emplace_back("", "", "");
    host_error_monitor::associationSSBThermTrip = server.add_interface(
        "/xyz/openbmc_project/host_error_monitor/ssb_thermal_trip",
        "xyz.openbmc_project.Association.Definitions");
    host_error_monitor::associationSSBThermTrip->register_property(
        "Associations", associations);
    host_error_monitor::associationSSBThermTrip->initialize();

    // Start tracking host state
    std::shared_ptr<sdbusplus::bus::match::match> hostStateMonitor =
        host_error_monitor::startHostStateMonitor();

    // Request PCH_BMC_THERMTRIP GPIO events
    if (!host_error_monitor::requestGPIOEvents(
            "PCH_BMC_THERMTRIP", host_error_monitor::pchThermtripHandler,
            host_error_monitor::pchThermtripLine,
            host_error_monitor::pchThermtripEvent))
    {
        return -1;
    }

    // Initialize the signal monitors
    host_error_monitor::init();

    host_error_monitor::io.run();

    return 0;
}
