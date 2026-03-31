#include <Arduino.h>
#include <string.h>

#include "display.h"
#include "display_lunar_luck_state.h"

#if WXV_ENABLE_LUNAR_CALENDAR && WXV_ENABLE_LUNAR_LUCK

extern size_t buildNormalized(char *dst, size_t dstCap, const char *src);
extern size_t summarizeListToBullets(char *dst, size_t cap, const char *src, int maxItems);

#ifndef SEP
#define SEP " * "
#endif

#define MAX_SECTIONS 10
#define CONTENT_MAX 420
#define TITLE_MAX 48
#define GOIY_CONTENT_MAX 800

namespace
{
    static char g_sectionContent[MAX_SECTIONS][CONTENT_MAX];

    static size_t boundedLen(const char *s, size_t maxLen)
    {
        if (!s)
            return 0;
        size_t n = 0;
        while (n < maxLen && s[n] != '\0')
            ++n;
        return n;
    }

    static size_t safeAppend(char *dst, size_t cap, const char *text)
    {
        if (!dst || cap == 0 || !text)
            return 0;

        size_t dstLen = boundedLen(dst, cap - 1);
        size_t room = cap - 1 - dstLen;
        if (room == 0)
            return dstLen;

        size_t i = 0;
        while (i < room && text[i] != '\0')
        {
            dst[dstLen + i] = text[i];
            ++i;
        }
        dst[dstLen + i] = '\0';
        return dstLen + i;
    }

    static void safeAppendClauseBoundary(char *dst, size_t cap, const char *clause)
    {
        if (!dst || cap == 0 || !clause || clause[0] == '\0')
            return;

        if (dst[0] == '\0')
        {
            safeAppend(dst, cap, clause);
            return;
        }

        if ((boundedLen(dst, cap - 1) + boundedLen(SEP, 8) + boundedLen(clause, cap - 1)) < (cap - 1))
        {
            safeAppend(dst, cap, SEP);
            safeAppend(dst, cap, clause);
            return;
        }

        safeAppend(dst, cap, "...");
    }

    static bool startsWithTopicPrefix(const char *src, size_t &prefixLen)
    {
        prefixLen = 0;
        if (!src)
            return false;

        static const char *const kPrefixes[] = {
            u8"Ch\u1EE7 \u0111\u1EC1:",
            u8"Ch\u1EE7 \u0110\u1EC1:",
            u8"Ch\u1EE7 \u0111\u1EC1 :",
            "Chu de:",
            "Chu De:",
            "Chu de :",
            "chu de:"};
        for (size_t i = 0; i < (sizeof(kPrefixes) / sizeof(kPrefixes[0])); ++i)
        {
            const size_t n = strlen(kPrefixes[i]);
            if (strncmp(src, kPrefixes[i], n) == 0)
            {
                prefixLen = n;
                return true;
            }
        }
        return false;
    }

    static const char *skipTopicLeadForGoiY(const char *headlineRaw)
    {
        if (!headlineRaw)
            return "";

        const char *p = headlineRaw;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            ++p;

        size_t prefixLen = 0;
        bool hasPrefix = startsWithTopicPrefix(p, prefixLen);
        if (!hasPrefix)
        {
            if (strncmp(p, "Chu De:", 7) == 0 || strncmp(p, "chu de:", 7) == 0)
            {
                hasPrefix = true;
                prefixLen = 7;
            }
        }
        if (!hasPrefix)
            return p;

        p += prefixLen;
        while (*p != '\0' && *p != '.')
            ++p;
        if (*p == '.')
            ++p;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            ++p;
        return p;
    }

    static const char *skipFocusLeadForGoiY(const char *goiYRaw, const char *focusPhrase)
    {
        if (!goiYRaw)
            return "";
        const char *p = goiYRaw;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            ++p;

        if (!focusPhrase || focusPhrase[0] == '\0')
            return p;

        size_t focusLen = strlen(focusPhrase);
        if (strncmp(p, focusPhrase, focusLen) != 0)
            return p;

        p += focusLen;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '.' || *p == ';' || *p == ':')
            ++p;
        return p;
    }

    static void addSection(const char *title, const char *contentSrc, bool normalize, bool summarize, int maxItems,
                           char *contentBuf = nullptr, size_t contentCap = 0)
    {
        if (g_sectionCount >= MAX_SECTIONS)
            return;

        LuckSection &sec = g_sections[g_sectionCount];
        sec.title = title ? title : "";
        if (!contentBuf || contentCap == 0)
        {
            contentBuf = g_sectionContent[g_sectionCount];
            contentCap = CONTENT_MAX;
        }
        sec.content = contentBuf;
        sec.contentCap = static_cast<uint16_t>(contentCap);
        sec.content[0] = '\0';

        if (summarize)
            summarizeListToBullets(sec.content, sec.contentCap, contentSrc, maxItems);
        else if (normalize)
            buildNormalized(sec.content, sec.contentCap, contentSrc);
        else if (contentSrc)
            safeAppend(sec.content, sec.contentCap, contentSrc);

        sec.contentLen = static_cast<uint16_t>(strlen(sec.content));
        sec.contentWidthPx = static_cast<int16_t>(measureTextWidthPx(sec.content, sec.contentLen));
        sec.marquee = (sec.contentWidthPx > PANEL_RES_X);
        ++g_sectionCount;
    }
}

void buildLuckSections(
    int lunarDay, int lunarMonth, int lunarYear,
    const char *canChiDay, const char *canChiMonth, const char *solarTerm, const char *canChiYear,
    const char *hop1, const char *hop2, const char *ky,
    const char *gioTotRawOrPreformatted,
    int score,
    const char *categoryName,
    const char *focusPhrase,
    const char *headlineRaw,
    const char *nenLamRaw,
    const char *nenTranhRaw,
    const char *xuatHanhLine,
    int xuatHanhTone,
    const char *xuatHanhName,
    const char *caDaoLine)
{
    (void)categoryName;
    g_sectionCount = 0;

    char buf[CONTENT_MAX];

    snprintf(buf, sizeof(buf), "%02d/%02d/%04d" SEP "%s" SEP "%s" SEP "%s" SEP "%s",
             lunarDay, lunarMonth, lunarYear,
             canChiDay ? canChiDay : "",
             canChiMonth ? canChiMonth : "",
             solarTerm ? solarTerm : "",
             canChiYear ? canChiYear : "");
    addSection(u8"\u00C2m L\u1ECBch", buf, false, false, 0);

    const char *scoreLabel = u8"B\u00ECnh";
    if (score >= 2)
        scoreLabel = u8"T\u1ED1t";
    else if (score <= -1)
        scoreLabel = u8"X\u1EA5u";
    snprintf(buf, sizeof(buf), u8"Ng\u00E0y: %s * X.H\u00E0nh: %s", scoreLabel, xuatHanhLine ? xuatHanhLine : "");
    buf[sizeof(buf) - 1] = '\0';
    addSection(u8"V\u1EADn Kh\u00ED", buf, false, false, 0);

    buf[0] = '\0';
    safeAppend(buf, sizeof(buf), hop1 ? hop1 : "");
    if (hop2 && hop2[0] != '\0')
    {
        safeAppend(buf, sizeof(buf), SEP);
        safeAppend(buf, sizeof(buf), hop2);
    }
    addSection(u8"H\u1EE3p Tu\u1ED5i", buf, false, false, 0);

    addSection(u8"K\u1EF5 Tu\u1ED5i", ky ? ky : "", false, false, 0);
    addSection(u8"Gi\u1EDD T\u1ED1t", gioTotRawOrPreformatted ? gioTotRawOrPreformatted : "", false, false, 0);
    addSection(u8"Ch\u1EE7 \u0110\u1EC1", focusPhrase ? focusPhrase : "", false, false, 0);

    const char *goiYNoTopic = skipTopicLeadForGoiY(headlineRaw);
    const char *goiYBody = skipFocusLeadForGoiY(goiYNoTopic, focusPhrase);
    char goiyBuilt[GOIY_CONTENT_MAX];
    buildNormalized(goiyBuilt, sizeof(goiyBuilt), goiYBody ? goiYBody : "");
    if (xuatHanhName && xuatHanhName[0] != '\0')
    {
        const char *clause = u8"Xu\u1EA5t h\u00E0nh v\u1EEBa: \u0111i \u0111\u01B0\u1EE3c nh\u01B0ng ch\u1ECDn vi\u1EC7c ch\u1EAFc, tr\u00E1nh v\u1ED9i";
        if (xuatHanhTone > 0)
            clause = u8"Xu\u1EA5t h\u00E0nh thu\u1EADn: d\u1EC5 g\u1EB7p tr\u1EE3 l\u1EF1c, vi\u1EC7c \u0111\u1ED1i ngo\u1EA1i hanh th\u00F4ng";
        else if (xuatHanhTone < 0)
            clause = u8"Xu\u1EA5t h\u00E0nh k\u00E9m: n\u00EAn gi\u1EEF an to\u00E0n, h\u1EA1n ch\u1EBF \u0111i xa v\u00E0 ch\u1ED1t l\u1EDBn";
        safeAppendClauseBoundary(goiyBuilt, sizeof(goiyBuilt), clause);
    }
    addSection(u8"L\u1EDDi B\u00E0n", goiyBuilt, false, false, 0, g_goiyContent, GOIY_CONTENT_MAX);

    char nenBuilt[CONTENT_MAX];
    char tranhBuilt[CONTENT_MAX];
    summarizeListToBullets(nenBuilt, sizeof(nenBuilt), nenLamRaw ? nenLamRaw : "", 5);
    summarizeListToBullets(tranhBuilt, sizeof(tranhBuilt), nenTranhRaw ? nenTranhRaw : "", 5);
    if (xuatHanhName && xuatHanhName[0] != '\0')
    {
        const char *nenClause = u8"\u0111i l\u1EA1i v\u1EEBa ph\u1EA3i, ch\u1ECDn vi\u1EC7c \u0111\u01A1n gi\u1EA3n v\u00E0 ch\u1EAFc ch\u1EAFn";
        const char *tranhClause = u8"tr\u00E1nh m\u1EDF vi\u1EC7c qu\u00E1 l\u1EDBn khi ch\u01B0a s\u1EB5n s\u00E0ng";
        if (xuatHanhTone > 0)
        {
            nenClause = u8"h\u1EE3p \u0111i l\u1EA1i, g\u1EB7p g\u1EE1, x\u1EED l\u00FD vi\u1EC7c \u0111\u1ED1i ngo\u1EA1i";
            tranhClause = u8"tr\u00E1nh ch\u1EA7n ch\u1EEB b\u1ECF l\u1EE1 th\u1EDDi \u0111i\u1EC3m";
        }
        else if (xuatHanhTone < 0)
        {
            nenClause = u8"\u01B0u ti\u00EAn vi\u1EC7c g\u1EA7n, vi\u1EC7c n\u1ED9i b\u1ED9, r\u00E0 so\u00E1t v\u00E0 ch\u1EC9nh s\u1EEDa";
            tranhClause = u8"h\u1EA1n ch\u1EBF \u0111i xa, khai tr\u01B0\u01A1ng, k\u00FD k\u1EBFt l\u1EDBn";
        }
        safeAppendClauseBoundary(nenBuilt, sizeof(nenBuilt), nenClause);
        safeAppendClauseBoundary(tranhBuilt, sizeof(tranhBuilt), tranhClause);
    }
    addSection(u8"N\u00EAn", nenBuilt, false, false, 0);
    addSection(u8"Tr\u00E1nh", tranhBuilt, false, false, 0);
    addSection("Ca Dao", caDaoLine ? caDaoLine : "", false, false, 0);

    for (uint8_t i = 0; i < g_sectionCount; ++i)
    {
        g_sections[i].contentLen = static_cast<uint16_t>(strlen(g_sections[i].content));
        g_sections[i].contentWidthPx = static_cast<int16_t>(measureTextWidthPx(g_sections[i].content, g_sections[i].contentLen));
        g_sections[i].marquee = (g_sections[i].contentWidthPx > PANEL_RES_X);
    }

    currentSectionIndex = 0;
    hdrState = HDR_IDLE;
    hdrDoorPx = PANEL_RES_X / 2;
    hdrDrawNew = true;
    hdrDelayActive = false;
    if (hdrBrightnessPulsed)
    {
        setPanelBrightness(hdrBrightnessSaved);
        hdrBrightnessPulsed = false;
    }
    hdrOld[0] = '\0';
    strncpy(hdrNew, g_sections[0].title ? g_sections[0].title : "", TITLE_MAX - 1);
    hdrNew[TITLE_MAX - 1] = '\0';
    setSectionStartState();
}

#else

void buildLuckSections(
    int lunarDay, int lunarMonth, int lunarYear,
    const char *canChiDay, const char *canChiMonth, const char *solarTerm, const char *canChiYear,
    const char *hop1, const char *hop2, const char *ky,
    const char *gioTotRawOrPreformatted,
    int score,
    const char *categoryName,
    const char *focusPhrase,
    const char *headlineRaw,
    const char *nenLamRaw,
    const char *nenTranhRaw,
    const char *xuatHanhLine,
    int xuatHanhTone,
    const char *xuatHanhName,
    const char *caDaoLine)
{
    (void)lunarDay;
    (void)lunarMonth;
    (void)lunarYear;
    (void)canChiDay;
    (void)canChiMonth;
    (void)solarTerm;
    (void)canChiYear;
    (void)hop1;
    (void)hop2;
    (void)ky;
    (void)gioTotRawOrPreformatted;
    (void)score;
    (void)categoryName;
    (void)focusPhrase;
    (void)headlineRaw;
    (void)nenLamRaw;
    (void)nenTranhRaw;
    (void)xuatHanhLine;
    (void)xuatHanhTone;
    (void)xuatHanhName;
    (void)caDaoLine;
}

#endif
