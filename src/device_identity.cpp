#include "device_identity.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

namespace wxv {
namespace cloud {
namespace {

constexpr const char *kCloudPrefsNs = "wxvcloud";

String randomHex_(size_t bytes)
{
    static const char *kHex = "0123456789abcdef";
    String out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i)
    {
        uint8_t value = static_cast<uint8_t>(esp_random() & 0xFF);
        out += kHex[(value >> 4) & 0x0F];
        out += kHex[value & 0x0F];
    }
    return out;
}

String generateUuidV4_()
{
    uint8_t bytes[16];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = static_cast<uint8_t>(esp_random() & 0xFF);

    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

    char out[37];
    snprintf(out, sizeof(out),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
    return String(out);
}

} // namespace

bool DeviceIdentityManager::begin(const String &fallbackName)
{
    Preferences prefs;
    if (!prefs.begin(kCloudPrefsNs, false))
        return false;

    identity_.uuid = prefs.getString("deviceUuid", "");
    identity_.secret = prefs.getString("deviceSecret", "");
    identity_.name = prefs.getString("deviceName", "");

    if (identity_.uuid.isEmpty())
    {
        identity_.uuid = generateUuidV4_();
        prefs.putString("deviceUuid", identity_.uuid);
    }

    if (identity_.secret.isEmpty())
    {
        identity_.secret = randomHex_(24);
        prefs.putString("deviceSecret", identity_.secret);
    }

    if (identity_.name.isEmpty())
    {
        String resolved = fallbackName;
        resolved.trim();
        if (resolved.isEmpty())
        {
            resolved = "WxVision-";
            uint64_t chip = ESP.getEfuseMac();
            char suffix[7];
            snprintf(suffix, sizeof(suffix), "%06llx", chip & 0xFFFFFFULL);
            resolved += suffix;
        }
        identity_.name = resolved;
        prefs.putString("deviceName", identity_.name);
    }

    prefs.end();
    return identity_.isValid();
}

DeviceIdentityManager &deviceIdentityManager()
{
    static DeviceIdentityManager manager;
    return manager;
}

const DeviceIdentity &deviceIdentity()
{
    return deviceIdentityManager().identity();
}

} // namespace cloud
} // namespace wxv
