/*
// Copyright (c) 2020 2021 Intel Corporation
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
// #include <error_monitors/smi_monitor.hpp>

#include <memory>

namespace host_error_monitor::error_monitors
{
// Error signals to monitor
// static std::unique_ptr<host_error_monitor::smi_monitor::SMIMonitor>
// smiMonitor;

// Check if all the signal monitors started successfully
bool checkMonitors()
{
    bool ret = true;

    // ret &= smiMonitor->isValid();

    return ret;
}

// Start the signal monitors
bool startMonitors(boost::asio::io_service& io,
                   std::shared_ptr<sdbusplus::asio::connection> conn)
{
    // smiMonitor =
    // std::make_unique<host_error_monitor::smi_monitor::SMIMonitor>(
    //     io, conn, "SMI");

    return checkMonitors();
}

// Notify the signal monitors of host on event
void sendHostOn()
{
    // smiMonitor->hostOn();
}

} // namespace host_error_monitor::error_monitors
