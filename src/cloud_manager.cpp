#include "cloud_manager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>

#include "datetimesettings.h"
#include "device_identity.h"
#include "settings.h"
#include "web.h"

namespace wxv {
namespace cloud {
namespace {

struct JobContext
{
    CloudManager *manager = nullptr;
    CloudManager::JobType job = CloudManager::JobType::None;
};

String readHttpResponseBody_(HTTPClient &http, size_t maxBytes)
{
    String body;
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr)
        return body;

    const unsigned long start = millis();
    while ((millis() - start) < 8000UL)
    {
        while (stream->available() > 0)
        {
            char buffer[256];
            const int read = stream->readBytes(buffer, sizeof(buffer));
            if (read <= 0)
                break;
            if ((body.length() + static_cast<size_t>(read)) > maxBytes)
                return body;
            body.concat(buffer, static_cast<size_t>(read));
        }

        if (!http.connected() && stream->available() <= 0)
            break;

        delay(10);
    }
    return body;
}

String sanitizedSecretSuffix_(const String &secret)
{
    if (secret.length() <= 4)
        return "****";
    return "****" + secret.substring(secret.length() - 4);
}

String abbreviatedBody_(const String &body, size_t maxLen = 240)
{
    if (body.length() <= maxLen)
        return body;
    return body.substring(0, maxLen) + "...";
}

String buildRelayUrlWithDeviceAuth_(const String &baseUrl, const DeviceIdentity &id)
{
    String url = baseUrl;
    const bool hasQuery = url.indexOf('?') >= 0;
    url += hasQuery ? '&' : '?';
    url += "device_uuid=";
    url += id.uuid;
    url += "&device_secret=";
    url += id.secret;
    return url;
}

} // namespace

void CloudManager::begin(const String &fallbackDeviceName)
{
    fallbackDeviceName_ = fallbackDeviceName;
    state_.enabled = cloudEnabled;
    state_.registered = false;
    state_.relayConnected = false;
    state_.relayAuthenticated = false;
    state_.registerBackoffMs = cloudReconnectInitialMs;
    state_.relayBackoffMs = cloudReconnectInitialMs;
    state_.lastError = "";
    state_.lastRegisterStatusCode = 0;
    state_.lastHeartbeatStatusCode = 0;
    state_.lastRegisterDetail = "";
    state_.lastHeartbeatDetail = "";

    state_.identityReady = deviceIdentityManager().begin(fallbackDeviceName_);
    scheduleRegister_(1000);
    scheduleRelayReconnect_(1000);
}

void CloudManager::loop()
{
    state_.enabled = cloudEnabled;
    state_.timeReady = hasUsableTime_();

    if (!state_.enabled)
    {
        relayClient_.stop();
        state_.relayConnected = false;
        state_.relayAuthenticated = false;
        return;
    }

    if (!wifiReady_() || !state_.identityReady)
        return;

    const uint32_t now = millis();

    if (!state_.registered && !jobRunning_ && static_cast<int32_t>(now - state_.nextRegisterAttemptMs) >= 0)
        startJob_(JobType::Register);
    else if (state_.registered &&
             !jobRunning_ &&
             (immediateHeartbeatRequested_ ||
              static_cast<uint32_t>(now - state_.lastHeartbeatMs) >= cloudHeartbeatIntervalMs))
    {
        startJob_(JobType::Heartbeat);
        immediateHeartbeatRequested_ = false;
    }

    if (!relayClient_.isConnected() &&
        !jobRunning_ &&
        static_cast<int32_t>(now - state_.nextRelayAttemptMs) >= 0)
    {
        const String relayUrl = buildRelayUrlWithDeviceAuth_(cloudRelayUrl, deviceIdentity());
        if (relayClient_.begin(relayUrl, this))
        {
            state_.lastError = "";
        }
        else
        {
            scheduleRelayReconnect_(state_.relayBackoffMs);
            state_.relayBackoffMs = min<uint32_t>(state_.relayBackoffMs * 2U, cloudReconnectMaxMs);
        }
    }

    if (relayClient_.isConnected() &&
        static_cast<uint32_t>(now - state_.lastRelayHeartbeatMs) >= cloudHeartbeatIntervalMs)
    {
      JsonDocument payload;
      payload["ts"] = now;
      sendRelayEnvelope_("heartbeat", "", payload.as<JsonVariantConst>());
      state_.lastRelayHeartbeatMs = now;
    }
}

void CloudManager::onRelayConnected()
{
    state_.relayConnected = true;
    state_.relayAuthenticated = false;
    state_.relayBackoffMs = cloudReconnectInitialMs;
    state_.lastRelayHeartbeatMs = 0;
    sendRelayAuth_();
}

void CloudManager::onRelayDisconnected()
{
    state_.relayConnected = false;
    state_.relayAuthenticated = false;
    scheduleRelayReconnect_(state_.relayBackoffMs);
    state_.relayBackoffMs = min<uint32_t>(state_.relayBackoffMs * 2U, cloudReconnectMaxMs);
}

void CloudManager::noteLocalAppConnectionState(bool connected)
{
    if (localAppConnected_ == connected)
        return;

    localAppConnected_ = connected;
    if (state_.registered)
        immediateHeartbeatRequested_ = true;
}

void CloudManager::onRelayTextMessage(const String &message)
{
    JsonDocument doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok || !doc.is<JsonObject>())
        return;
    handleRelayEnvelope_(doc);
}

void CloudManager::scheduleRegister_(uint32_t delayMs)
{
    state_.nextRegisterAttemptMs = millis() + delayMs;
}

void CloudManager::scheduleRelayReconnect_(uint32_t delayMs)
{
    state_.nextRelayAttemptMs = millis() + delayMs;
}

bool CloudManager::hasUsableTime_() const
{
    return time(nullptr) > 1700000000;
}

bool CloudManager::wifiReady_() const
{
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(static_cast<uint32_t>(0));
}

bool CloudManager::startJob_(JobType job)
{
    if (jobRunning_)
        return false;

    JobContext *ctx = new JobContext();
    if (ctx == nullptr)
        return false;

    ctx->manager = this;
    ctx->job = job;
    activeJob_ = job;
    jobRunning_ = true;
    state_.jobRunning = true;

    BaseType_t created = xTaskCreatePinnedToCore(
        &CloudManager::jobTaskEntry_,
        "wxv-cloud",
        8192,
        ctx,
        1,
        nullptr,
        1);
    if (created != pdPASS)
    {
        delete ctx;
        activeJob_ = JobType::None;
        jobRunning_ = false;
        state_.jobRunning = false;
        return false;
    }
    return true;
}

void CloudManager::jobTaskEntry_(void *param)
{
    std::unique_ptr<JobContext> ctx(static_cast<JobContext *>(param));
    if (ctx && ctx->manager)
        ctx->manager->runJob_(ctx->job);
    vTaskDelete(nullptr);
}

void CloudManager::runJob_(JobType job)
{
    bool ok = false;
    switch (job)
    {
    case JobType::Register:
        ok = performRegister_();
        break;
    case JobType::Heartbeat:
        ok = performHeartbeat_();
        break;
    default:
        break;
    }

    (void)ok;
    activeJob_ = JobType::None;
    jobRunning_ = false;
    state_.jobRunning = false;
}

bool CloudManager::performRegister_()
{
    JsonDocument doc;
    const DeviceIdentity &id = deviceIdentity();
    doc["device_uuid"] = id.uuid;
    doc["discovered_name"] = id.name;
    doc["device_name"] = id.name;
    doc["firmware_version"] = firmwareVersion_();
    doc["hardware_model"] = hardwareModel_();
    doc["local_ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["local_app_connected"] = localAppConnected_;
    if (hasUsableTime_())
        doc["timezone"] = currentTimezoneId();

  String body;
  serializeJson(doc, body);

  int statusCode = 0;
    String response;
    bool ok = httpPostJson_("/api/device/register", body, statusCode, response) && statusCode >= 200 && statusCode < 300;
    if (ok)
    {
        state_.registered = true;
        state_.heartbeatFailures = 0;
        state_.lastRegisterMs = millis();
        state_.registerBackoffMs = cloudReconnectInitialMs;
        state_.lastError = "";
        state_.lastRegisterStatusCode = statusCode;
        state_.lastRegisterDetail = response;
        scheduleRegister_(cloudHeartbeatIntervalMs * 4U);
        Serial.printf("[Cloud] Registered %s (%s)\n", id.uuid.c_str(), sanitizedSecretSuffix_(id.secret).c_str());
        return true;
    }

    const String failureDetail = response.length() > 0 ? abbreviatedBody_(response) : state_.lastError;
    state_.registered = false;
    state_.lastError = "register_failed";
    state_.lastRegisterStatusCode = statusCode;
    state_.lastRegisterDetail = failureDetail;
    scheduleRegister_(state_.registerBackoffMs);
    state_.registerBackoffMs = min<uint32_t>(state_.registerBackoffMs * 2U, cloudReconnectMaxMs);
    Serial.printf("[Cloud] Registration failed (HTTP %d)\n", statusCode);
    if (response.length() > 0)
        Serial.printf("[Cloud] Register response: %s\n", abbreviatedBody_(response).c_str());
    else if (state_.lastError.length() > 0)
        Serial.printf("[Cloud] Register error: %s\n", state_.lastError.c_str());
    return false;
}

bool CloudManager::performHeartbeat_()
{
    JsonDocument doc;
    const DeviceIdentity &id = deviceIdentity();
    doc["device_uuid"] = id.uuid;
    doc["local_ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["relay_connected"] = relayClient_.isConnected();
    doc["local_app_connected"] = localAppConnected_;
    doc["uptime_sec"] = millis() / 1000UL;

    String body;
    serializeJson(doc, body);

    int statusCode = 0;
    String response;
    bool ok = httpPostJson_("/api/device/heartbeat", body, statusCode, response) && statusCode >= 200 && statusCode < 300;
    if (ok)
    {
        state_.lastHeartbeatMs = millis();
        state_.heartbeatFailures = 0;
        state_.lastError = "";
        state_.lastHeartbeatStatusCode = statusCode;
        state_.lastHeartbeatDetail = response;
        return true;
    }

    const String failureDetail = response.length() > 0 ? abbreviatedBody_(response) : state_.lastError;
    state_.heartbeatFailures++;
    state_.lastError = "heartbeat_failed";
    state_.lastHeartbeatStatusCode = statusCode;
    state_.lastHeartbeatDetail = failureDetail;
    if (state_.heartbeatFailures >= 3)
    {
        state_.registered = false;
        scheduleRegister_(cloudReconnectInitialMs);
    }
    Serial.printf("[Cloud] Heartbeat failed (HTTP %d)\n", statusCode);
    if (response.length() > 0)
        Serial.printf("[Cloud] Heartbeat response: %s\n", abbreviatedBody_(response).c_str());
    else if (state_.lastError.length() > 0)
        Serial.printf("[Cloud] Heartbeat error: %s\n", state_.lastError.c_str());
    return false;
}

bool CloudManager::httpPostJson_(const String &path, const String &body, int &statusCode, String &responseBody)
{
    if (!hasUsableTime_())
    {
        statusCode = 0;
        responseBody = "";
        state_.lastError = "time_not_ready";
        Serial.printf("[Cloud] Skipping HTTPS %s until system time is ready\n", path.c_str());
        return false;
    }

    String url = cloudApiBaseUrl;
    if (url.endsWith("/"))
        url.remove(url.length() - 1);
    url += makeRelativePath_(path);

    const DeviceIdentity &id = deviceIdentity();
    if (url.startsWith("https://"))
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.useHTTP10(true);
        http.setTimeout(8000);
        http.setReuse(false);

        if (!http.begin(client, url))
        {
            statusCode = 0;
            responseBody = "";
            state_.lastError = "http_begin_failed";
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        http.addHeader("Connection", "close");
        http.addHeader("X-Device-UUID", id.uuid);
        http.addHeader("X-Device-Secret", id.secret);

        const int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body.c_str())), body.length());
        if (code <= 0)
        {
            statusCode = 0;
            responseBody = "";
            state_.lastError = http.errorToString(code);
            http.end();
            return false;
        }

        statusCode = code;
        responseBody = readHttpResponseBody_(http, 2048);
        http.end();
        return true;
    }

    WiFiClient client;
    HTTPClient http;
    http.useHTTP10(true);
    http.setTimeout(8000);
    http.setReuse(false);

    if (!http.begin(client, url))
    {
        statusCode = 0;
        responseBody = "";
        state_.lastError = "http_begin_failed";
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    http.addHeader("X-Device-UUID", id.uuid);
    http.addHeader("X-Device-Secret", id.secret);

    const int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body.c_str())), body.length());
    if (code <= 0)
    {
        statusCode = 0;
        responseBody = "";
        state_.lastError = http.errorToString(code);
        http.end();
        return false;
    }

    statusCode = code;
    responseBody = readHttpResponseBody_(http, 2048);
    http.end();
    return true;
}

void CloudManager::sendRelayEnvelope_(const char *type, const String &requestId, JsonVariantConst payload)
{
    JsonDocument doc;
    doc["type"] = type;
    if (!requestId.isEmpty())
        doc["requestId"] = requestId;
    if (!payload.isNull())
        doc["payload"] = payload;

    String body;
    serializeJson(doc, body);
    relayClient_.sendText(body);
}

void CloudManager::sendRelayEnvelope_(const char *type, const String &requestId, const String &payloadJson, bool payloadIsRawJson)
{
    JsonDocument doc;
    doc["type"] = type;
    if (!requestId.isEmpty())
        doc["requestId"] = requestId;

    if (payloadIsRawJson)
    {
        JsonDocument payloadDoc;
        if (deserializeJson(payloadDoc, payloadJson) == DeserializationError::Ok)
            doc["payload"] = payloadDoc.as<JsonVariantConst>();
        else
            doc["payload"] = payloadJson;
    }
    else
    {
        doc["payload"] = payloadJson;
    }

    String body;
    serializeJson(doc, body);
    relayClient_.sendText(body);
}

void CloudManager::handleRelayEnvelope_(JsonDocument &doc)
{
    String type = doc["type"] | "";
    String requestId = doc["requestId"] | "";

    if (type == "ping")
    {
        JsonDocument payload;
        payload["ts"] = millis();
        sendRelayEnvelope_("pong", requestId, payload.as<JsonVariantConst>());
        return;
    }

    if (type == "auth_ok" || type == "authenticated")
    {
        state_.relayAuthenticated = true;
        return;
    }

    if (type != "request")
        return;

    JsonVariantConst payload = doc["payload"];
    String method = payload["method"] | "GET";
    String path = payload["path"] | "";

    String body;
    if (payload["body"].is<JsonObjectConst>() || payload["body"].is<JsonArrayConst>())
        serializeJson(payload["body"], body);
    else if (!payload["body"].isNull())
        body = payload["body"].as<String>();

    // Remote relay requests intentionally reuse the same app API bridge as LAN clients.
    // The mobile app later attaches the registered device on the backend using device_uuid.
    AppApiResponse apiResponse;
    if (!handleAppApiRequest(method, path, body, apiResponse))
    {
        JsonDocument resp;
        resp["status"] = 404;
        resp["contentType"] = "application/json";
        resp["body"]["ok"] = false;
        resp["body"]["error"] = "unsupported_path";
        sendRelayEnvelope_("response", requestId, resp.as<JsonVariantConst>());
        return;
    }

    JsonDocument resp;
    resp["status"] = apiResponse.statusCode;
    resp["contentType"] = apiResponse.contentType;
    const bool looksLikeJson =
        apiResponse.body.length() > 0 &&
        (apiResponse.body[0] == '{' || apiResponse.body[0] == '[');
    if (looksLikeJson && String(apiResponse.contentType).indexOf("json") >= 0)
        resp["body"] = serialized(apiResponse.body);
    else
        resp["body"] = apiResponse.body;
    sendRelayEnvelope_("response", requestId, resp.as<JsonVariantConst>());
}

void CloudManager::sendRelayAuth_()
{
    JsonDocument payload;
    payload["device_uuid"] = deviceIdentity().uuid;
    payload["device_secret"] = deviceIdentity().secret;
    payload["firmware_version"] = firmwareVersion_();
    payload["hardware_model"] = hardwareModel_();
    sendRelayEnvelope_("auth", "", payload.as<JsonVariantConst>());
}

String CloudManager::buildAuthPayload_() const
{
    JsonDocument doc;
    doc["device_uuid"] = deviceIdentity().uuid;
    doc["device_secret"] = deviceIdentity().secret;
    String body;
    serializeJson(doc, body);
    return body;
}

String CloudManager::makeRelativePath_(const String &path) const
{
    if (path.startsWith("/"))
        return path;
    return "/" + path;
}

String CloudManager::hardwareModel_() const
{
    return "WxVision";
}

String CloudManager::firmwareVersion_() const
{
#ifdef APP_VERSION
    return String(APP_VERSION);
#else
    return String(__DATE__) + " " + String(__TIME__);
#endif
}

CloudManager &cloudManager()
{
    static CloudManager manager;
    return manager;
}

const CloudRuntimeState &cloudState()
{
    return cloudManager().state();
}

void cloudBegin(const String &fallbackDeviceName)
{
    cloudManager().begin(fallbackDeviceName);
}

void cloudLoop()
{
    cloudManager().loop();
}

} // namespace cloud
} // namespace wxv
