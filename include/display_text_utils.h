#pragma once

#include <stddef.h>

size_t boundedLen(const char *s, size_t cap);
size_t safeAppendN(char *dst, size_t cap, const char *src, size_t srcLen);
size_t safeAppend(char *dst, size_t cap, const char *text);
size_t buildNormalized(char *dst, size_t dstCap, const char *src);
size_t summarizeListToBullets(char *dst, size_t cap, const char *src, int maxItems);
