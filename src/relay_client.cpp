#include "relay_client.h"

#include <esp_crt_bundle.h>
#include <esp_websocket_client.h>

namespace wxv {
namespace cloud {

RelayClient::RelayClient() = default;

RelayClient::~RelayClient()
{
    stop();
}

bool RelayClient::begin(const String &url, RelayClientListener *listener)
{
    stop();

    listener_ = listener;

    esp_websocket_client_config_t cfg = {};
    cfg.uri = url.c_str();
    cfg.task_prio = 4;
    cfg.task_stack = 6144;
    cfg.buffer_size = 1024;
    cfg.disable_auto_reconnect = true;
    cfg.transport = url.startsWith("wss://")
        ? WEBSOCKET_TRANSPORT_OVER_SSL
        : WEBSOCKET_TRANSPORT_OVER_TCP;
    cfg.pingpong_timeout_sec = 30;
    cfg.disable_pingpong_discon = true;
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 15;
    cfg.keep_alive_interval = 15;
    cfg.keep_alive_count = 3;
    cfg.ping_interval_sec = 20;

    esp_websocket_client_handle_t client =
        esp_websocket_client_init(&cfg);
    if (client == nullptr)
        return false;

    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, &RelayClient::handleEvent_, this);
    if (esp_websocket_client_start(client) != ESP_OK)
    {
        esp_websocket_client_destroy(client);
        return false;
    }

    client_ = client;
    rxBuffer_.remove(0);
    return true;
}

void RelayClient::stop()
{
    if (client_ == nullptr)
        return;

    esp_websocket_client_handle_t client = static_cast<esp_websocket_client_handle_t>(client_);
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    client_ = nullptr;
    rxBuffer_.remove(0);
}

bool RelayClient::isConnected() const
{
    if (client_ == nullptr)
        return false;
    return esp_websocket_client_is_connected(static_cast<esp_websocket_client_handle_t>(client_));
}

bool RelayClient::sendText(const String &payload)
{
    if (!isConnected())
        return false;
    int sent = esp_websocket_client_send_text(
        static_cast<esp_websocket_client_handle_t>(client_),
        payload.c_str(),
        payload.length(),
        1000 / portTICK_PERIOD_MS);
    return sent >= 0;
}

void RelayClient::handleEvent_(void *handlerArgs, esp_event_base_t, int32_t eventId, void *eventData)
{
    RelayClient *self = static_cast<RelayClient *>(handlerArgs);
    if (self != nullptr)
        self->handleEventInternal_(eventId, eventData);
}

void RelayClient::handleEventInternal_(int32_t eventId, void *eventData)
{
    if (eventId == WEBSOCKET_EVENT_CONNECTED)
    {
        if (listener_ != nullptr)
            listener_->onRelayConnected();
        return;
    }

    if (eventId == WEBSOCKET_EVENT_DISCONNECTED)
    {
        rxBuffer_.remove(0);
        if (listener_ != nullptr)
            listener_->onRelayDisconnected();
        return;
    }

    if (eventId != WEBSOCKET_EVENT_DATA)
        return;

    auto *data = static_cast<esp_websocket_event_data_t *>(eventData);
    if (data == nullptr)
        return;

    appendMessageChunk_(
        reinterpret_cast<const uint8_t *>(data->data_ptr),
        static_cast<size_t>(data->data_len),
        static_cast<size_t>(data->payload_offset),
        static_cast<size_t>(data->payload_len),
        data->op_code);
}

void RelayClient::appendMessageChunk_(const uint8_t *data, size_t len, size_t payloadOffset, size_t payloadLen, int opcode)
{
    if (opcode != 0x1 || data == nullptr || len == 0)
        return;

    if (payloadOffset == 0)
        rxBuffer_.remove(0);

    rxBuffer_.concat(reinterpret_cast<const char *>(data), len);
    if ((payloadOffset + len) < payloadLen)
        return;

    if (listener_ != nullptr)
        listener_->onRelayTextMessage(rxBuffer_);
    rxBuffer_.remove(0);
}

} // namespace cloud
} // namespace wxv
