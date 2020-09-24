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
#include <error_monitors/cpu1_mismatch_monitor.hpp>
#include <error_monitors/cpu2_mismatch_monitor.hpp>
#include <error_monitors/smi_monitor.hpp>

#include <memory>

namespace host_error_monitor::error_monitors
{
// Error signals to monitor
static std::unique_ptr<host_error_monitor::smi_monitor::SMIMonitor> smiMonitor;
static std::unique_ptr<
    host_error_monitor::cpu1_mismatch_monitor::CPU1MismatchMonitor>
    cpu1MismatchMonitor;
static std::unique_ptr<
    host_error_monitor::cpu2_mismatch_monitor::CPU2MismatchMonitor>
    cpu2MismatchMonitor;

// Start the signal monitors
void startMonitors(boost::asio::io_service& io,
                   std::shared_ptr<sdbusplus::asio::connection> conn)
{
    smiMonitor =
        std::make_unique<host_error_monitor::smi_monitor::SMIMonitor>(io, conn);
    cpu1MismatchMonitor = std::make_unique<
        host_error_monitor::cpu1_mismatch_monitor::CPU1MismatchMonitor>(io,
                                                                        conn);
    cpu2MismatchMonitor = std::make_unique<
        host_error_monitor::cpu2_mismatch_monitor::CPU2MismatchMonitor>(io,
                                                                        conn);
}

// Notify the signal monitors of host on event
void sendHostOn()
{
    smiMonitor->hostOn();
    cpu1MismatchMonitor->hostOn();
    cpu2MismatchMonitor->hostOn();
}

} // namespace host_error_monitor::error_monitors
