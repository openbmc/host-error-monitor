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
#include <error_monitors/err_pin_monitor.hpp>
#include <host_error_monitor.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>

namespace host_error_monitor::err2_monitor
{
static constexpr bool debug = false;

class Err2Monitor : public host_error_monitor::err_pin_monitor::ErrPinMonitor
{
    const static constexpr uint8_t beepCPUErr2 = 5;

    void assertHandler() override
    {
        host_error_monitor::err_pin_monitor::ErrPinMonitor::assertHandler();

        beep(conn, beepCPUErr2);

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
                    std::cerr << "Unable to read reset on ERR2 value\n";
                    return;
                }
                startCrashdumpAndRecovery(conn, *reset, "ERR2 Timeout");
            },
            "xyz.openbmc_project.Settings",
            "/xyz/openbmc_project/control/processor_error_config",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Control.Processor.ErrConfig", "ResetOnERR2");
    }

  public:
    Err2Monitor(boost::asio::io_service& io,
                std::shared_ptr<sdbusplus::asio::connection> conn,
                const std::string& signalName) :
        host_error_monitor::err_pin_monitor::ErrPinMonitor(io, conn, signalName,
                                                           2)
    {}
};
} // namespace host_error_monitor::err2_monitor
