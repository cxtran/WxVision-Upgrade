#include "display_text_utils.h"

#include <ctype.h>
#include <string.h>

#ifndef SEP
#define SEP " * "
#endif

size_t boundedLen(const char *s, size_t cap)
{
    if (!s || cap == 0)
        return 0;
    size_t i = 0;
    while (i < cap && s[i] != '\0')
        ++i;
    return i;
}

size_t safeAppendN(char *dst, size_t cap, const char *src, size_t srcLen)
{
    if (!dst || cap == 0 || !src)
        return 0;
    size_t len = boundedLen(dst, cap - 1);
    if (len >= cap - 1)
    {
        dst[cap - 1] = '\0';
        return cap - 1;
    }
    size_t room = (cap - 1) - len;
    size_t copyLen = (srcLen < room) ? srcLen : room;
    if (copyLen > 0)
        memcpy(dst + len, src, copyLen);
    dst[len + copyLen] = '\0';
    return len + copyLen;
}

size_t safeAppend(char *dst, size_t cap, const char *text)
{
    if (!text)
        return boundedLen(dst, cap);
    return safeAppendN(dst, cap, text, strlen(text));
}

static size_t appendSeparator(char *dst, size_t cap)
{
    size_t len = boundedLen(dst, cap - 1);
    while (len > 0 && dst[len - 1] == ' ')
    {
        dst[len - 1] = '\0';
        --len;
    }
    if (len == 0)
        return 0;
    return safeAppend(dst, cap, SEP);
}

size_t buildNormalized(char *dst, size_t dstCap, const char *src)
{
    if (!dst || dstCap == 0)
        return 0;
    dst[0] = '\0';
    if (!src)
        return 0;

    bool prevSpace = false;
    const size_t sepLen = strlen(SEP);
    auto appendChunk = [&](const char *chunk, size_t chunkLen)
    {
        if (chunkLen == 0)
            return;
        safeAppendN(dst, dstCap, chunk, chunkLen);
    };

    const uint8_t *p = reinterpret_cast<const uint8_t *>(src);
    while (*p != '\0')
    {
        if (*p == 0xC2 && *(p + 1) == 0xA6)
        {
            appendSeparator(dst, dstCap);
            prevSpace = false;
            p += 2;
            continue;
        }

        if (*p == '|' || *p == ';' || *p == ',' || *p == '\n' || *p == '\r')
        {
            appendSeparator(dst, dstCap);
            prevSpace = false;
            ++p;
            continue;
        }

        const char c = static_cast<char>(*p);
        if (isspace(static_cast<unsigned char>(c)))
        {
            if (!prevSpace)
            {
                appendChunk(" ", 1);
                prevSpace = true;
            }
            ++p;
            continue;
        }

        appendChunk(reinterpret_cast<const char *>(p), 1);
        prevSpace = false;
        ++p;
    }

    size_t len = boundedLen(dst, dstCap - 1);
    while (len > 0 && dst[len - 1] == ' ')
    {
        dst[len - 1] = '\0';
        --len;
    }
    return len;
}

static bool isTokenSeparator(char c)
{
    return (c == ';' || c == ',' || c == '\n' || c == '\r' || c == '\0');
}

size_t summarizeListToBullets(char *dst, size_t cap, const char *src, int maxItems)
{
    if (!dst || cap == 0)
        return 0;
    dst[0] = '\0';
    if (!src || maxItems <= 0)
        return 0;

    char token[96];
    size_t tokLen = 0;
    int added = 0;

    for (size_t i = 0;; ++i)
    {
        const char c = src[i];
        if (!isTokenSeparator(c))
        {
            if (tokLen < sizeof(token) - 1)
                token[tokLen++] = c;
            continue;
        }

        token[tokLen] = '\0';
        size_t start = 0;
        while (token[start] != '\0' && isspace(static_cast<unsigned char>(token[start])))
            ++start;
        size_t end = strlen(token);
        while (end > start && isspace(static_cast<unsigned char>(token[end - 1])))
            --end;

        if (end > start)
        {
            if (added > 0)
                safeAppend(dst, cap, SEP);
            safeAppendN(dst, cap, token + start, end - start);
            ++added;
            if (added >= maxItems)
                break;
        }

        tokLen = 0;
        if (c == '\0')
            break;
    }

    return boundedLen(dst, cap - 1);
}
