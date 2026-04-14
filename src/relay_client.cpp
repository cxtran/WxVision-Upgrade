#include "relay_client.h"

#include <Arduino.h>
#include <WebSocketsClient.h>

namespace wxv {
namespace cloud {

RelayClient::RelayClient() = default;

RelayClient::~RelayClient()
{
    stop();
}

static void parseWebSocketUrl(
    const String &url,
    bool &secure,
    String &host,
    uint16_t &port,
    String &path)
{
    secure = false;
    host = "";
    port = 80;
    path = "/";

    String working = url;
    if (working.startsWith("wss://")) {
        secure = true;
        working.remove(0, 6);
        port = 443;
    } else if (working.startsWith("ws://")) {
        working.remove(0, 5);
        port = 80;
    }

    int slashPos = working.indexOf('/');
    String hostPort = slashPos >= 0 ? working.substring(0, slashPos) : working;
    path = slashPos >= 0 ? working.substring(slashPos) : "/";

    int colonPos = hostPort.indexOf(':');
    if (colonPos >= 0) {
        host = hostPort.substring(0, colonPos);
        port = static_cast<uint16_t>(hostPort.substring(colonPos + 1).toInt());
    } else {
        host = hostPort;
    }
}

void RelayClient::handleWebSocketEvent_(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
        notifiedConnected_ = true;
        lastConnectMs_ = millis();
        lastReceiveMs_ = lastConnectMs_;
        rxBuffer_.remove(0);
        if (listener_ != nullptr) {
            listener_->onRelayConnected();
        }
        break;

    case WStype_DISCONNECTED:
        rxBuffer_.remove(0);
        if (listener_ != nullptr && notifiedConnected_) {
            listener_->onRelayDisconnected();
        }
        notifiedConnected_ = false;
        break;

    case WStype_TEXT:
        lastReceiveMs_ = millis();
        rxBuffer_ = String(reinterpret_cast<const char *>(payload), length);
        if (listener_ != nullptr) {
            listener_->onRelayTextMessage(rxBuffer_);
        }
        rxBuffer_.remove(0);
        break;

    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_PING:
    case WStype_PONG:
    default:
        break;
    }
}

bool RelayClient::begin(const String &url, RelayClientListener *listener)
{
    stop();

    listener_ = listener;
    rxBuffer_.remove(0);
    notifiedConnected_ = false;
    lastReceiveMs_ = 0;
    lastConnectMs_ = 0;

    bool secure = false;
    String host;
    uint16_t port = 0;
    String path;
    parseWebSocketUrl(url, secure, host, port, path);

    if (host.isEmpty()) {
        return false;
    }

    WebSocketsClient *ws = new WebSocketsClient();
    if (ws == nullptr) {
        return false;
    }

    ws->onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
        this->handleWebSocketEvent_(type, payload, length);
    });

    if (secure) {
        ws->beginSSL(host.c_str(), port, path.c_str());
    } else {
        ws->begin(host.c_str(), port, path.c_str());
    }

    ws->setReconnectInterval(0);   // match your old disable_auto_reconnect = true
    ws->enableHeartbeat(15000, 3000, 2);

    client_ = ws;
    return true;
}

void RelayClient::stop()
{
    if (client_ == nullptr) {
        return;
    }

    WebSocketsClient *ws = static_cast<WebSocketsClient *>(client_);
    ws->disconnect();
    delete ws;
    client_ = nullptr;

    rxBuffer_.remove(0);
    notifiedConnected_ = false;
}

bool RelayClient::isConnected() const
{
    if (client_ == nullptr) {
        return false;
    }

    WebSocketsClient *ws = static_cast<WebSocketsClient *>(client_);
    return ws->isConnected();
}

bool RelayClient::sendText(const String &payload)
{
    if (!isConnected()) {
        return false;
    }

    WebSocketsClient *ws = static_cast<WebSocketsClient *>(client_);
    String copy = payload;
    return ws->sendTXT(copy);
}

void RelayClient::loop()
{
    if (client_ == nullptr) {
        return;
    }

    WebSocketsClient *ws = static_cast<WebSocketsClient *>(client_);
    ws->loop();
}

} // namespace cloud
} // namespace wxv