#pragma once

#include <Arduino.h>

namespace wxv {
namespace cloud {

struct DeviceIdentity
{
    String uuid;
    String secret;
    String name;

    bool isValid() const
    {
        return !uuid.isEmpty() && !secret.isEmpty();
    }
};

class DeviceIdentityManager
{
public:
    bool begin(const String &fallbackName);
    const DeviceIdentity &identity() const { return identity_; }

private:
    DeviceIdentity identity_;
};

DeviceIdentityManager &deviceIdentityManager();
const DeviceIdentity &deviceIdentity();

} // namespace cloud
} // namespace wxv
