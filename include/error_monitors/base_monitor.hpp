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

#include <iostream>

namespace host_error_monitor::base_monitor
{

class BaseMonitor
{
  public:
    bool valid;
    boost::asio::io_service& io;
    std::shared_ptr<sdbusplus::asio::connection> conn;

    std::string signalName;

    BaseMonitor(boost::asio::io_service& io,
                std::shared_ptr<sdbusplus::asio::connection> conn,
                const std::string& signalName) :
        valid(false),
        io(io), conn(conn), signalName(signalName)

    {
        std::cerr << "Initializing " << signalName << " Monitor\n";
    }

    virtual void hostOn()
    {}

    bool isValid()
    {
        return valid;
    }
};
} // namespace host_error_monitor::base_monitor
