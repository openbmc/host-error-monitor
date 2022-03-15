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

#include <fstream>

#include <error_monitors/base_poll_monitor.hpp>
#include <host_error_monitor.hpp>
#include <nlohmann/json.hpp>
#include <peci.h>
#include <sdbusplus/asio/object_server.hpp>

namespace host_error_monitor::register_monitor
{
static constexpr bool debug = false;

class RegisterMonitor :
    public host_error_monitor::base_poll_monitor::BasePollMonitor
{
  public:
    RegisterMonitor(boost::asio::io_service& io,
                  std::shared_ptr<sdbusplus::asio::connection> conn,
                  const std::string& signalName, size_t pollingTimeMs,
                  size_t timeoutMs, const std::string& configFilepath) :
        BasePollMonitor(io, conn, signalName, pollingTimeMs, timeoutMs)
    {
        if (!init(configFilepath))
        {
            return;
        }
        valid = true;
    }

  private:
    void parseJSON(const std::string& path) {
        std::ifstream jsonFile(path);
        if (!jsonFile.is_open())
        {
            throw std::invalid_argument("Unable to open JSON file.");
        }

        auto data = nlohmann::json::parse(jsonFile, nullptr, false, true);
        if (data.is_discarded())
        {
            throw std::invalid_argument("Data parse failed.");
        }

        const auto machineConfig = data["machineConfig"];

        addressType = machineConfig["addressType"];
        baseTarget = machineConfig["baseTarget"];
        segment = machineConfig["segment"];
        baseBus = machineConfig["baseBus"];
        baseDevice = machineConfig["baseDevice"];
        function = machineConfig["function"];
        bar = machineConfig["bar"];
        dataLength = machineConfig["dataLength"];
        socketOffset = machineConfig["socketOffset"];
        channelOffset = machineConfig["channelOffset"];
        numSockets = machineConfig["numSockets"];
        numIMCs = machineConfig["numIMCs"];
        numChannels = machineConfig["numChannels"];
        registersToPoll =
            static_cast<std::unordered_map<std::string, uint32_t>>(
                data["registers"]);
    }

    bool init(const std::string& configFilename)
    {
        try {
            parseJSON(configFilename);
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "JSON parsing failed for file " << configFilename
                      << " with message: \"" << e.what() << "\"." << std::endl;
            return false;
        }

        return true;
    }

    std::tuple<uint8_t, uint8_t, uint8_t, uint64_t> translateToPeciAddress(
        uint8_t socket, uint8_t imc, uint8_t channel, uint32_t registerOffset)
    {
        uint8_t currentSocket = baseTarget + socket;
        uint8_t currentBus = baseBus + socket * socketOffset;
        uint8_t currentDevice = baseDevice + imc;
        uint64_t currentOffset =
            registerOffset + channel * channelOffset;

        return std::make_tuple(currentSocket, currentBus, currentDevice,
                               currentOffset);
    }

    void createDump(
        std::map<std::string, std::variant<std::string, uint64_t>> params)
    {
        conn->async_method_call(
            [](boost::system::error_code ec) {
                if (ec)
                {
                    std::cerr << "Error creating dump. ec: " << ec << std::endl;
                    return;
                }
            },
            "xyz.openbmc_project.Dump.Manager",
            "/xyz/openbmc_project/dump/faultlog",
            "xyz.openbmc_project.Dump.Create", "CreateDump", params
        );
    }

    void readRetryRdRegisters()
    {
        std::map<std::string, std::variant<std::string, uint64_t>> regValues;

        for (const auto& reg : registersToPoll)
            {
            for (uint8_t socket = 0; socket < numSockets; socket++)
            {
                for (uint8_t imc = 0; imc < numIMCs; imc++)
                {
                    for (uint8_t channel = 0; channel < numChannels; channel++)
                    {
                        const auto [currentSocket, currentBus, currentDevice,
                                    currentOffset] =
                            translateToPeciAddress(socket, imc, channel,
                                                   reg.second);

                        EPECIStatus peciStatus;
                        uint32_t readValue = 0;
                        uint8_t cc = 0;
                        peciStatus = peci_RdEndPointConfigMmio(
                            currentSocket, segment, currentBus, currentDevice,
                            function, bar, addressType, currentOffset,
                            dataLength, (uint8_t*)&readValue, &cc);

                        if (peciError(peciStatus, cc))
                        {
                            printPECIError(
                                reg.first, currentOffset, peciStatus, cc);
                            continue;
                        }

                        std::string keyString = "Socket " +
                            std::to_string(socket) + ", IMC " +
                            std::to_string(imc) + ", Channel " +
                            std::to_string(channel) + ", Reg Name " +
                            reg.first + ", Offset " +
                            std::to_string(reg.second);
                        regValues[keyString] = std::to_string(readValue);
                    }
                }
            }
        }

        createDump(regValues);
    }

    void resetRetryRdRegisters()
    {
        std::unordered_map<std::string, uint64_t> resetValues = {
                // Sets EN and NOOVER fields.
                {"RETRY_RD_ERR_LOG", 0x0000c000},
                // Sets EN, NOOVER and EN_PATSPR fields.
                {"RETRY_RD_ERR_SET2_LOG", 0x0000e000}
            };

        for (const auto& value : resetValues)
        {
            if (registersToPoll.find(value.first) == registersToPoll.end() )
            {
                std::cerr << "Offset for " << value.first << " not found."
                          << std::endl;
                continue;
            }

            const auto& registerOffset = registersToPoll[value.first];

            for (uint8_t socket = 0; socket < numSockets; socket++)
            {
                for (uint8_t imc = 0; imc < numIMCs; imc++)
                {
                    for (uint8_t channel = 0; channel < numChannels; channel++)
                    {
                        const auto [currentSocket, currentBus, currentDevice,
                                    currentOffset] =
                            translateToPeciAddress(socket, imc, channel,
                                                   registerOffset);

                        EPECIStatus peciStatus;
                        uint8_t cc = 0;
                        peciStatus = peci_WrEndPointConfigMmio(
                            currentSocket, segment, currentBus, currentDevice,
                            function, bar, addressType, currentOffset,
                            dataLength, value.second, &cc);


                        if (peciError(peciStatus, cc))
                        {
                            printPECIError(
                                value.first, currentOffset, peciStatus, cc);
                        }
                    }
                }
            }
        }
    }

    void readTemperatures()
    {
        const std::string dbusService = "xyz.openbmc_project.HwmonTempSensor";
        std::map<std::string, std::variant<std::string, uint64_t>> tempValues;
        std::vector<std::string> dbusPaths{
            "/xyz/openbmc_project/sensors/temperature/exhaust_left",
            "/xyz/openbmc_project/sensors/temperature/exhaust_mid",
            "/xyz/openbmc_project/sensors/temperature/exhaust_right",
            "/xyz/openbmc_project/sensors/temperature/inlet"
        };

        for (const auto& path : dbusPaths) {
            conn->async_method_call(
                [&tempValues, path](boost::system::error_code ec,
                                    std::variant<double>& ret) {
                    if (ec)
                    {
                        std::cerr << "Error getting temp value. ec: " << ec
                                << std::endl;
                        return;
                    }
                    if (const double* value = std::get_if<double>(&ret))
                    {
                        tempValues[path] = std::to_string(*value);
                        return;
                    }
                },
                dbusService, path, "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Sensor.Value", "Value"
            );
        }

        createDump(tempValues);
    }

    void pollingActions() override
    {
        readRetryRdRegisters();
        readTemperatures();
        resetRetryRdRegisters();
    }

    void handleTimeout() override
    {
        // Check if host is responding to PECI commands.
        EPECIStatus peciStatus;
        for (uint8_t socket = 0; socket < numSockets; socket++)
        {
            peciStatus = peci_Ping(baseTarget + socket);

            if (peciError(peciStatus, /*Don't care*/ 0))
            {
                std::cerr << "Socket " << +socket << " not responding."
                          << std::endl;
            }
        }
    }

    uint8_t addressType;
    uint8_t baseTarget;
    uint8_t segment;
    uint8_t baseBus;
    uint8_t baseDevice;
    uint8_t function;
    uint8_t bar;
    uint8_t dataLength;
    uint8_t socketOffset;
    uint16_t channelOffset;
    uint8_t numSockets;
    uint8_t numIMCs;
    uint8_t numChannels;
    std::unordered_map<std::string, uint32_t> registersToPoll;
};
} // namespace host_error_monitor::register_monitor
