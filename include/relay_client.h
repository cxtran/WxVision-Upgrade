#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

namespace wxv {
namespace cloud {

class RelayClientListener
{
public:
    virtual ~RelayClientListener() = default;
    virtual void onRelayConnected() = 0;
    virtual void onRelayDisconnected() = 0;
    virtual void onRelayTextMessage(const String &message) = 0;
};

class RelayClient
{
public:
    RelayClient();
    ~RelayClient();

    bool begin(const String &url, RelayClientListener *listener);
    void stop();
    void loop();
    bool isConnected() const;
    bool sendText(const String &payload);
    uint32_t lastReceiveMs() const { return lastReceiveMs_; }
    uint32_t lastConnectMs() const { return lastConnectMs_; }

private:
    void handleWebSocketEvent_(WStype_t type, uint8_t *payload, size_t length);

    void *client_ = nullptr;
    RelayClientListener *listener_ = nullptr;
    String rxBuffer_;
    bool notifiedConnected_ = false;
    uint32_t lastReceiveMs_ = 0;
    uint32_t lastConnectMs_ = 0;
};

} // namespace cloud
} // namespace wxv