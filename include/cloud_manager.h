#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "relay_client.h"

namespace wxv {
namespace cloud {

struct CloudRuntimeState
{
    bool enabled = false;
    bool identityReady = false;
    bool timeReady = false;
    bool registered = false;
    bool relayConnected = false;
    bool relayAuthenticated = false;
    bool jobRunning = false;
    uint32_t lastRegisterMs = 0;
    uint32_t lastHeartbeatMs = 0;
    uint32_t lastRelayHeartbeatMs = 0;
    uint32_t nextRegisterAttemptMs = 0;
    uint32_t nextRelayAttemptMs = 0;
    uint32_t registerBackoffMs = 0;
    uint32_t relayBackoffMs = 0;
    uint8_t heartbeatFailures = 0;
    int lastRegisterStatusCode = 0;
    int lastHeartbeatStatusCode = 0;
    String lastError;
    String lastRegisterDetail;
    String lastHeartbeatDetail;
};

class CloudManager : public RelayClientListener
{
public:
    enum class JobType : uint8_t
    {
        None,
        Register,
        Heartbeat,
    };

    void begin(const String &fallbackDeviceName);
    void loop();
    const CloudRuntimeState &state() const { return state_; }
    void noteLocalAppConnectionState(bool connected);

    void onRelayConnected() override;
    void onRelayDisconnected() override;
    void onRelayTextMessage(const String &message) override;

private:
    void scheduleRegister_(uint32_t delayMs);
    void scheduleRelayReconnect_(uint32_t delayMs);
    bool hasUsableTime_() const;
    bool wifiReady_() const;
    bool startJob_(JobType job);
    static void jobTaskEntry_(void *param);
    void runJob_(JobType job);
    bool performRegister_();
    bool performHeartbeat_();
    bool httpPostJson_(const String &path, const String &body, int &statusCode, String &responseBody);
    void sendRelayEnvelope_(const char *type, const String &requestId, JsonVariantConst payload);
    void sendRelayEnvelope_(const char *type, const String &requestId, const String &payloadJson, bool payloadIsRawJson);
    void handleRelayEnvelope_(JsonDocument &doc);
    void sendRelayAuth_();
    String buildAuthPayload_() const;
    String makeRelativePath_(const String &path) const;
    String hardwareModel_() const;
    String firmwareVersion_() const;

    CloudRuntimeState state_;
    RelayClient relayClient_;
    String fallbackDeviceName_;
    bool localAppConnected_ = false;
    bool immediateHeartbeatRequested_ = false;
    volatile bool jobRunning_ = false;
    JobType activeJob_ = JobType::None;
};

CloudManager &cloudManager();
const CloudRuntimeState &cloudState();
void cloudBegin(const String &fallbackDeviceName);
void cloudLoop();

} // namespace cloud
} // namespace wxv
