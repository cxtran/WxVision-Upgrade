#pragma once

#include <Arduino.h>
#include <esp_event.h>

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
    bool isConnected() const;
    bool sendText(const String &payload);

private:
    static void handleEvent_(void *handlerArgs, esp_event_base_t base, int32_t eventId, void *eventData);
    void handleEventInternal_(int32_t eventId, void *eventData);
    void appendMessageChunk_(const uint8_t *data, size_t len, size_t payloadOffset, size_t payloadLen, int opcode);

    void *client_ = nullptr;
    RelayClientListener *listener_ = nullptr;
    String rxBuffer_;
};

} // namespace cloud
} // namespace wxv
