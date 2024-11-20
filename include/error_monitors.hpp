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
#include <sdbusplus/asio/object_server.hpp>
#include <error_monitors/caterr_monitor.hpp>
#include <error_monitors/mcerr_monitor.hpp>
#include <error_monitors/err_pin_monitor.hpp>

// #include <error_monitors/smi_monitor.hpp>

#include <memory>

namespace host_error_monitor::error_monitors
{
// Error signals to monitor
// static std::unique_ptr<host_error_monitor::smi_monitor::SMIMonitor>
// smiMonitor;
static std::unique_ptr<host_error_monitor::err_pin_sample_monitor::CatErrMonitor>
caterrMonitor;
static std::unique_ptr<host_error_monitor::mcerr_monitor::MCERRMonitor>
mceerrMonitor;
static std::unique_ptr<host_error_monitor::err_pin_monitor::ErrPinMonitor>
errPin0Monitor;
static std::unique_ptr<host_error_monitor::err_pin_monitor::ErrPinMonitor>
errPin1Monitor;
static std::unique_ptr<host_error_monitor::err_pin_monitor::ErrPinMonitor>
errPin2Monitor;

// Check if all the signal monitors started successfully
bool checkMonitors()
{
    bool ret = true;

    // ret &= smiMonitor->isValid();
    ret &= caterrMonitor->isValid();
    ret &= mceerrMonitor->isValid();
    ret &= errPin0Monitor->isValid();
    ret &= errPin1Monitor->isValid();
    ret &= errPin2Monitor->isValid();

    return ret;
}

// Start the signal monitors
bool startMonitors(
    [[maybe_unused]] boost::asio::io_context& io,
    [[maybe_unused]] std::shared_ptr<sdbusplus::asio::connection> conn)
{
    caterrMonitor = std::make_unique<host_error_monitor::err_pin_sample_monitor::CatErrMonitor>(
         io, conn, "FM_CPU_CATERR_LVT3_N");

    mceerrMonitor = std::make_unique<host_error_monitor::mcerr_monitor::MCERRMonitor>(
         io, conn, "FM_CPU_RMCA_LVT3_N", host_error_monitor::base_gpio_monitor::AssertValue::lowAssert);

    errPin0Monitor = std::make_unique<host_error_monitor::err_pin_monitor::ErrPinMonitor>(
         io, conn, "FM_CPU_ERR0_LVT3_N", 0);

    errPin1Monitor = std::make_unique<host_error_monitor::err_pin_monitor::ErrPinMonitor>(
         io, conn, "FM_CPU_ERR1_LVT3_N", 1);

    errPin2Monitor = std::make_unique<host_error_monitor::err_pin_monitor::ErrPinMonitor>(
         io, conn, "FM_CPU_ERR2_LVT3_N", 2);

    // std::make_unique<host_error_monitor::smi_monitor::SMIMonitor>(
    //     io, conn, "SMI");

    return checkMonitors();
}

// Notify the signal monitors of host on event
void sendHostOn()
{
    // smiMonitor->hostOn();
    caterrMonitor->hostOn();
    mceerrMonitor->hostOn();
    errPin0Monitor->hostOn();
    errPin1Monitor->hostOn();
    errPin2Monitor->hostOn();
}

} // namespace host_error_monitor::error_monitors
