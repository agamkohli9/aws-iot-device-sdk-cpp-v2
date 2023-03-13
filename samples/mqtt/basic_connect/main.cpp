/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/TlsOptions.h>

#include <aws/iot/MqttClient.h>

#include <algorithm>
#include <aws/crt/UUID.h>
#include <chrono>
#include <mutex>
#include <time.h>
#include <thread>

#include "../../utils/CommandLineUtils.h"

#include <nlohmann/json.hpp>
#include <iostream> // For printing json stuff (overloaded to std::cout.operator<<())
#include <sstream>

#include <BasicSafetyMessage.h>
#include "worker.h"

using namespace Aws::Crt;

using json = nlohmann::json;

int main(int argc, char *argv[])
{
    /************************ Setup the Lib ****************************/
    /*
     * Do the global initialization for the API.
     */
    ApiHandle apiHandle;
    uint32_t messageCount = 10;

    /*********************** Parse Arguments ***************************/
    Utils::CommandLineUtils cmdUtils = Utils::CommandLineUtils();
    cmdUtils.RegisterProgramName("basic_pub_sub");
    cmdUtils.AddCommonMQTTCommands();
    cmdUtils.RegisterCommand("key", "<path>", "Path to your key in PEM format.");
    cmdUtils.RegisterCommand("cert", "<path>", "Path to your client certificate in PEM format.");
    cmdUtils.AddCommonProxyCommands();
    cmdUtils.AddCommonTopicMessageCommands();
    cmdUtils.RegisterCommand("client_id", "<str>", "Client id to use (optional, default='test-*')");
    cmdUtils.RegisterCommand("count", "<int>", "The number of messages to send (optional, default='10')");
    cmdUtils.RegisterCommand("port_override", "<int>", "The port override to use when connecting (optional)");
    cmdUtils.AddLoggingCommands();
    const char **const_argv = (const char **)argv;
    cmdUtils.SendArguments(const_argv, const_argv + argc);
    cmdUtils.StartLoggingBasedOnCommand(&apiHandle);

    String topic = cmdUtils.GetCommandOrDefault("topic", "test/topic");
    String clientId = cmdUtils.GetCommandOrDefault("client_id", String("test-") + Aws::Crt::UUID().ToString());


    String messagePayload = cmdUtils.GetCommandOrDefault("message", "Hello world!");
    if (cmdUtils.HasCommand("count"))
    {
        int count = atoi(cmdUtils.GetCommand("count").c_str());
        if (count > 0)
        {
            messageCount = count;
        }
    }

    /* Get a MQTT client connection from the command parser */
    auto connection = cmdUtils.BuildMQTTConnection();

    /**
     * NOTE: Probably don't want to enforce synchronous behavior
     */
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;

    /*
     * This will execute when an MQTT connect has completed or failed.
     */
    auto onConnectionCompleted = [&](Mqtt::MqttConnection &, int errorCode, Mqtt::ReturnCode returnCode, bool) {
        if (errorCode)
        {
            fprintf(stdout, "Connection failed with error %s\n", ErrorDebugString(errorCode));
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            fprintf(stdout, "Connection completed with return code %d\n", returnCode);
            connectionCompletedPromise.set_value(true);
        }
    };

    auto onInterrupted = [&](Mqtt::MqttConnection &, int error) {
        fprintf(stdout, "Connection interrupted with error %s\n", ErrorDebugString(error));
    };

    auto onResumed = [&](Mqtt::MqttConnection &, Mqtt::ReturnCode, bool) { fprintf(stdout, "Connection resumed\n"); };

    /*
     * Invoked when a disconnect message has completed.
     */
    auto onDisconnect = [&](Mqtt::MqttConnection &) {
        {
            fprintf(stdout, "Disconnect completed\n");
            connectionClosedPromise.set_value();
        }
    };

    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);
    connection->OnConnectionInterrupted = std::move(onInterrupted);
    connection->OnConnectionResumed = std::move(onResumed);

    /*
     * Actually perform the connect dance.
     */
    fprintf(stdout, "Connecting to P3 Mobility topic \"%s\" using AMAG hardware through AWS IoT MQTT C++ SDK...\n", topic.c_str());

    if (!connection->Connect(clientId.c_str(), false /*cleanSession*/, 1000 /*keepAliveTimeSecs*/))
    {
        fprintf(stderr, "MQTT Connection failed with error %s\n", ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    if (connectionCompletedPromise.get_future().get())
    {
        std::mutex receiveMutex;
        std::condition_variable receiveSignal;
        uint32_t receivedCount = 0;

        /**
         * This is invoked upon the receipt of a Publish on a subscribed topic.
         */
        auto onMessage = [&](Mqtt::MqttConnection &,
                             const String &topic,
                             const ByteBuf &byteBuf,
                             bool /*dup*/,
                             Mqtt::QOS /*qos*/,
                             bool /*retain*/) {
            {
                std::lock_guard<std::mutex> lock(receiveMutex);
                ++receivedCount;
                fprintf(stdout, "Publish #%d received on topic %s\n", receivedCount, topic.c_str());
                fprintf(stdout, "Message: ");

                // Export Message to JSON
                char *msg = (char *) byteBuf.buffer;
                msg[byteBuf.len] = '\0';
                
                json msg_json = json::parse(msg);

                std::cout << msg_json.dump(4) << std::endl;

                // Store msg json as BSM
                for (auto obj : msg_json[0]["objects"]) { 

                    BasicSafetyMessage_t* message = (BasicSafetyMessage_t*) calloc(1, sizeof(BasicSafetyMessage_t) );

                    /**
                     * Populate BSMcoreData 
                     * 
                     * NOTE: Following will be hardware detection hardware specific
                    */

                    // Cursed pointer magic
                    uint8_t* object_id = reinterpret_cast<uint8_t *>(&obj["object_id"].get<std::string>()[0]);

                    message->coreData.msgCnt = receivedCount;
                    message->coreData.id.buf = object_id;
                    message->coreData.id.size = sizeof(object_id);
                    message->coreData.secMark = msg_json[0]["timestamp"];
                    message->coreData.lat = obj["lat"];
                    message->coreData.Long = obj["lon"];
                    message->coreData.elev = 0; /** NOTE: AMAG does not give elevation, so set to 0 for all objects */
                    message->coreData.heading = obj["heading"];


                    /** TODO: */
                    // Send message to ObjectDetectionComparisonPlugin
                } 
            }

            receiveSignal.notify_all();
        };

        /*
         * Subscribe for incoming publish messages on topic.
         */
        std::promise<void> subscribeFinishedPromise;
        auto onSubAck =
            [&](Mqtt::MqttConnection &, uint16_t packetId, const String &topic, Mqtt::QOS QoS, int errorCode) {
                if (errorCode)
                {
                    fprintf(stderr, "Subscribe failed with error %s\n", aws_error_debug_str(errorCode));
                    exit(-1);
                }
                else
                {
                    if (!packetId || QoS == AWS_MQTT_QOS_FAILURE)
                    {
                        fprintf(stderr, "Subscribe rejected by the broker.");
                        exit(-1);
                    }
                    else
                    {
                        fprintf(stdout, "Subscribe on topic %s on packetId %d Succeeded\n", topic.c_str(), packetId);
                    }
                } subscribeFinishedPromise.set_value();
            };

        connection->Subscribe(topic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage, onSubAck);
        subscribeFinishedPromise.get_future().wait();

        uint32_t publishedCount = 0;
        while (publishedCount < messageCount)
        {
            ByteBuf payload = ByteBufFromArray((const uint8_t *)messagePayload.data(), messagePayload.length());

            auto onPublishComplete = [](Mqtt::MqttConnection &, uint16_t, int) {};
            connection->Publish(topic.c_str(), AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPublishComplete);
            ++publishedCount;

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        {
            std::unique_lock<std::mutex> receivedLock(receiveMutex);
            receiveSignal.wait(receivedLock, [&] { return receivedCount >= messageCount; });
        }

        /*
         * Unsubscribe from the topic.
         */
        std::promise<void> unsubscribeFinishedPromise;
        connection->Unsubscribe(
            topic.c_str(), [&](Mqtt::MqttConnection &, uint16_t, int) { unsubscribeFinishedPromise.set_value(); });
        unsubscribeFinishedPromise.get_future().wait();

        /* Disconnect */
        if (connection->Disconnect())
        {
            connectionClosedPromise.get_future().wait();
        }
    }
    else
    {
        exit(-1);
    }

    return 0;
}
