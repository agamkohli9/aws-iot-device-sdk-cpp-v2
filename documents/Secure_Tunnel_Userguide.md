# Secure Tunnel Guide
## Introduction
When devices are deployed behind restricted firewalls at remote sites, you need a way to gain access to those devices for troubleshooting, configuration updates, and other operational tasks. Use secure tunneling to establish bidirectional communication to remote devices over a secure connection that is managed by AWS IoT. Secure tunneling does not require updates to your existing inbound firewall rules, so you can keep the same security level provided by firewall rules at a remote site.

More information on the service and how to open, close, and manage secure tunnels can be found here: https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling.html

A sample is also provided and can be found here: https://github.com/aws/aws-iot-device-sdk-cpp-v2/tree/main/samples#secure-tunnel



# Getting started with Secure Tunnels
## How to Create a Secure Tunnel Client
Once a Secure Tunnel builder has been created, it is ready to make a Secure Tunnel client. Something important to note is that once a Secure Tunnel client is built and finalized, the configuration is immutable and cannot be changed. Further modifications to the Secure Tunnel builder will not change the settings of already created Secure Tunnel clients.

```cpp
// Create Secure Tunnel Builder
SecureTunnelBuilder builder = SecureTunnelBuilder(...);

// Build Secure Tunnel Client
std::shared_ptr<SecureTunnel> secureTunnel = builder.Build();

if (secureTunnel == nullptr)
{
    fprintf(stdout, "Secure Tunnel creation failed.\n");
    return -1;
}

// Start the secure tunnel connection
if (!secureTunnel->Start())
{
    fprintf("Failed to start Secure Tunnel\n");
    return -1;
}
```
## Callbacks

### OnConnectionSuccess
When the Secure Tunnel Client successfully connects with the Secure Tunnel service, this callback will return the available (if any) service ids.

### OnConnectionFailure
When a WebSocket upgrade request fails to connect, this callback will return an error code.

### OnConnectionShutdown
When the WebSocket connection shuts down, this callback will be invoked.

### OnSendDataComplete
When a message has been completely written to the socket, this callback will be invoked.

### OnMessageReceived
When a message is received on an open Secure Tunnel stream, this callback will return the message.

### OnStreamStarted
When a stream is started by a Source connected to the Destination, the Destination will invoke this callback and return the stream information.

### OnStreamStopped
When an open stream is closed, this callback will be invoked and return the stopped stream's information.

### OnSessionReset
When the Secure Tunnel service requests the Secure Tunnel client fully reset, this callback is invoked.

### OnStopped
When the Secure Tunnel has reached a fully stopped state this callback is invoked.

## Setting Secure Tunnel Callbacks
The Secure Tunnel client uses callbacks to keep the user updated on its status and pass along relavant information. These can be set up using the Secure Tunnel builder's With functions.

```cpp
// Create Secure Tunnel Builder
SecureTunnelBuilder builder = SecureTunnelBuilder(...);

// Setting the onMessageReceived callback using the builder
builder.WithOnMessageReceived([&](SecureTunnel *secureTunnel, const MessageReceivedEventData &eventData) {
        {
            std::shared_ptr<Message> message = eventData.message;
            if (message->getServiceId().has_value()){
                fprintf(
                    stdout,
                    "Message received on service id:'" PRInSTR "'\n",
                    AWS_BYTE_CURSOR_PRI(message->getServiceId().value()));
            }

            if(message->getPayload().has_value()){
                fprintf(
                    stdout,
                    "Message has payload:'" PRInSTR "'\n",
                    AWS_BYTE_CURSOR_PRI(message->getPayload().value()));
            }
        }
    });

// Build Secure Tunnel Client
std::shared_ptr<SecureTunnel> secureTunnel = builder.Build();

if (secureTunnel == nullptr)
{
    fprintf(stdout, "Secure Tunnel creation failed.\n");
    return -1;
}

// Start the secure tunnel connection
if (!secureTunnel->Start())
{
    fprintf("Failed to start Secure Tunnel\n");
    return -1;
}

// Messages received on a stream will now be printed to stdout.
```

# How to Start and Stop

## Start
Invoking `Start()` on the Secure Tunnel Client will put it into an active state where it recurrently establishes a connection to the configured Secure Tunnel endpoint using the provided [Client Access Token](https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling-concepts.html). If a [Client Token](https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling-concepts.html) is provided, the Secure Tunnel Client will use it. If a Client Token is not provided, the Secure Tunnel Client will automatically generate one for use on a reconnection attempts. The Client Token for any initial connection to the Secure Tunnel service **MUST** be unique. Reusing a Client Token from a previous connection will result in a failed connection to the Secure Tunnel Service.
```cpp
// Create Secure Tunnel Builder
SecureTunnelBuilder builder = SecureTunnelBuilder(...);

// Adding a client token to the builder
String clientToken;
builder.WithClientToken(clientToken.c_str());

// Build Secure Tunnel Client
std::shared_ptr<SecureTunnel> secureTunnel = builder.Build();

if (secureTunnel == nullptr)
{
    fprintf(stdout, "Secure Tunnel creation failed.\n");
    return -1;
}

// Start the secure tunnel connection
if (!secureTunnel->Start())
{
    fprintf("Failed to start Secure Tunnel\n");
    return -1;
}
```

## Stop
Invoking `Stop()` on the Secure Tunnel Client breaks the current connection (if any) and moves the client into an idle state.
```cpp
if(!secureTunnel->Stop()){
    fprintf(stdout, "Failed to stop the Secure Tunnel connection session. Exiting..\n");
}
```
# Multiplexing
You can use multiple data streams per Secure Tunnel by using the [Multiplexing](https://docs.aws.amazon.com/iot/latest/developerguide/multiplexing.html) feature.

## Opening a Secure Tunnel with Multiplexing
To use Multiplexing, a Secure Tunnel must be created with one to three "services". A Secure Tunnel can be opened through the AWS IoT console [Secure Tunnel Hub](https://console.aws.amazon.com/iot/home#/tunnelhub) or by using the [OpenTunnel API](https://docs.aws.amazon.com/iot/latest/apireference/API_Operations_AWS_IoT_Secure_Tunneling.html). Both of these methods allow you to add services with whichever names suit your needs.
## Services Within the Secure Tunnel Client
On a successfull connection to a Secure Tunnel, the Secure Tunnel Client will invoke the `OnConnectionSuccess` callback. This callback will return `ConnectionSuccessEventData` that will contain any available Service Ids that can be used for multiplexing. Below is an example of how to set the callback using the Secure Tunnel Builder and check whether a Service Id is available.
```cpp
// Create Secure Tunnel Builder
SecureTunnelBuilder builder = SecureTunnelBuilder(...);

// Add a callback function to the builder for OnConnectionSuccess
builder.WithOnConnectionSuccess([&](SecureTunnel *secureTunnel, const ConnectionSuccessEventData &eventData) {
        //Check if a Service Id is available
        if (eventData.connectionData->getServiceId1().has_value())
        {
            // This Secure Tunnel IS using Service Ids and ServiceId1 is set.
            fprintf(
                stdout,
                "Secure Tunnel connected with  Service IDs '" PRInSTR "'",
                AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId1().value()));

            // Service Ids are set in the connectionData in order of availability. ServiceId2 will only be set if ServiceId1 is set.
            if (eventData.connectionData->getServiceId2().has_value())
            {
                fprintf(
                    stdout, ", '" PRInSTR "'", AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId2().value()));

                // ServiceId3 will only be set if ServiceId2 is set.
                if (eventData.connectionData->getServiceId3().has_value())
                {
                    fprintf(
                        stdout,
                        ", '" PRInSTR "'",
                        AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId3().value()));
                }
            }
            fprintf(stdout, "\n");
        }
        else
        {
            // This Secure Tunnel is NOT using Service Ids and Multiplexing
            fprintf(stdout, "Secure Tunnel is not using Service Ids.\n");
        }
    });
```
## Using Service Ids
Service Ids can be added to outbound Messages as shown below in the Send Message example. If the Service Id is both available on the current Secure Tunnel and there is an open stream with a Source device on that Service Id, the message will be sent. If the Service Id does not exist on the current Secure Tunnel or there is no currently active stream available on that Service Id, the Message will not be sent and a Warning will be logged. The `OnStreamStarted` callback is invoked when a stream is started and it returns a `StreamStartedEventData` which can be parsed to determine if a stream was started using a Service Id for Multiplexing. Incoming messages can also be parsed to determine if a Service Id has been set as shown above in the [Setting Secure Tunnel Callbacks](#setting-secure-tunnel-callbacks) code example.
# Secure Tunnel Operations

## Send Message
The `SendMessage()` operation takes a description of the Message you wish to send and returns a success/failure in the synchronous logic that kicks off the `SendMessage()` operation. When the message is fully written to the socket, the `OnSendDataComplete` callback will be invoked.

```cpp
Crt::String serviceId_string = "ssh";
Crt::String message_string = "any payload";

ByteCursor serviceId = ByteCursorFromString(serviceId_string);
ByteCursor payload = ByteCursorFromString(message_string);

// Create Message
std::shared_ptr<Message> message = std::make_shared<Message>();
message->withServiceId(serviceId);
message->withPayload(payload);

// Send Message
secureTunnel->SendMessage(message);
```
