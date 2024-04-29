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
#include <xyz/openbmc_project/Logging/Entry/common.hpp>

#include <iostream>

namespace host_error_monitor::base_monitor
{

class BaseMonitor
{
  public:
    bool valid;
    boost::asio::io_context& io;
    std::shared_ptr<sdbusplus::asio::connection> conn;

    std::string signalName;

    BaseMonitor(boost::asio::io_context& io,
                std::shared_ptr<sdbusplus::asio::connection> conn,
                const std::string& signalName) :
        valid(false), io(io), conn(conn), signalName(signalName)

    {
        std::cerr << "Initializing " << signalName << " Monitor\n";
    }

    virtual void hostOn() {}

    bool isValid()
    {
        return valid;
    }

  protected:
    void log_message(int priority, const std::string& msg,
                     const std::string& redfish_id,
                     const std::string& redfish_msg)
    {
#ifdef SEND_TO_LOGGING_SERVICE
        (void)redfish_id;
        (void)redfish_msg;
        using namespace sdbusplus::common::xyz::openbmc_project::logging;
        sdbusplus::message_t newLogEntry = conn->new_method_call(
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "xyz.openbmc_project.Logging.Create", "Create");
        const std::string logLevel =
            Entry::convertLevelToString(static_cast<Entry::Level>(priority));
        newLogEntry.append(msg, std::move(logLevel),
                           std::map<std::string, std::string>{});
        conn->call(newLogEntry);
#else
        sd_journal_send("MESSAGE=HostError: %s", msg.c_str(), "PRIORITY=%i",
                        priority, "REDFISH_MESSAGE_ID=%s", redfish_id.c_str(),
                        "REDFISH_MESSAGE_ARGS=%s", redfish_msg.c_str(), NULL);
#endif
    }
};
} // namespace host_error_monitor::base_monitor
