#pragma once

#include <Arduino.h>
#include <pgmspace.h>
#include <stdint.h>

#include "fortune_lunar_maps.h"

static inline void readProgmemString(const char *const *tableProgmem, uint16_t index, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!tableProgmem)
        return;

    const char *p = reinterpret_cast<const char *>(pgm_read_ptr(&tableProgmem[index]));
    if (!p)
        return;

    strncpy_P(out, p, outSize - 1);
    out[outSize - 1] = '\0';
}

static inline uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static inline uint32_t dailySeed(int ly, int lm, int ld, int score, int stemIdx, int branchIdx)
{
    uint32_t x = 0x9E3779B9u;
    x ^= static_cast<uint32_t>(ly) * 0x1F123BB5u;
    x ^= static_cast<uint32_t>(lm) * 0xA24BAED5u;
    x ^= static_cast<uint32_t>(ld) * 0x9FB21C65u;
    x ^= static_cast<uint32_t>(score + 0x400) * 0x85EBCA6Bu;
    x ^= static_cast<uint32_t>((stemIdx & 0x0F) << 12);
    x ^= static_cast<uint32_t>(branchIdx & 0x0F);
    return mix32(x);
}

static inline uint32_t dailySeed(int ly, int lm, int ld, int score, const String &branchString)
{
    const int branchIdx = branchToIndex(branchString);
    return dailySeed(ly, lm, ld, score, 0, branchIdx < 0 ? 0 : branchIdx);
}

static inline uint16_t pickIndex(uint32_t seed, uint32_t salt, uint16_t count)
{
    if (count == 0)
        return 0;
    return static_cast<uint16_t>(mix32(seed ^ salt) % count);
}
