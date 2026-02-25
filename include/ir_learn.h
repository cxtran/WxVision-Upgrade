#pragma once

#include <Arduino.h>
#include <IRrecv.h>

namespace wxv {
namespace irlearn {

bool start();
void cancel();
bool clearLearnedRemote();
bool isActive();
void onDecodedFrame(const decode_results &res);
void tick();
bool consumeReturnToSystemMenuRequest();

} // namespace irlearn
} // namespace wxv

