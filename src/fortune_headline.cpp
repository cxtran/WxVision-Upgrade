#include "fortune_headline.h"

#include <Arduino.h>
#include <Preferences.h>
#include <pgmspace.h>

#include "fortune_phrases_score.h"
#include "fortune_phrases_element.h"
#include "fortune_phrases_phase.h"
#include "fortune_phrases_branchgroup.h"
#include "fortune_phrases_flags.h"
#include "fortune_phrases_compat.h"
#include "fortune_phrases_focus.h"
#include "fortune_phrase_picker.h"
#include "fortune_lunar_maps.h"

namespace
{
constexpr uint32_t SCORE_SALT = 0xA11CE001u;
constexpr uint32_t ELEM_SALT = 0xA11CE002u;
constexpr uint32_t PHASE_SALT = 0xA11CE003u;
constexpr uint32_t GROUP_SALT = 0xA11CE004u;
constexpr uint32_t TAM_SALT = 0xA11CE005u;
constexpr uint32_t NGUYET_SALT = 0xA11CE006u;
constexpr uint32_t LEAP_SALT = 0xA11CE007u;
constexpr uint32_t COMPAT_SALT = 0xA11CE00Au;
constexpr uint32_t FOCUS_SALT_BASE = 0xA11CE00Bu;

constexpr uint16_t IDX_NONE = 0xFFFFu;

static void appendPhrase(String &out, const char *text)
{
    if (!text || text[0] == '\0')
        return;
    if (out.length() > 0 && out[out.length() - 1] != ' ')
        out += ' ';
    out += text;
}

static void adjustIfRepeat(uint16_t &idx, uint16_t lastIdx, uint16_t count)
{
    if (count > 1 && idx == lastIdx)
        idx = (idx + 1) % count;
}

static uint16_t pickPhraseIndex(uint32_t seed, uint32_t salt, uint16_t count)
{
    if (!count)
        return 0;
    return pickIndex(seed, salt, count);
}

static void appendPhraseByIndex(String &out, const char *const *table, uint16_t count, uint16_t idx)
{
    if (!table || count == 0)
        return;
    idx %= count;
    char buf[180];
    readProgmemString(table, idx, buf, sizeof(buf));
    appendPhrase(out, buf);
}

static uint16_t pickFlagIndexIf(bool enabled, uint32_t seed, uint32_t salt, uint16_t count)
{
    if (!enabled || count == 0)
        return IDX_NONE;
    return pickIndex(seed, salt, count);
}

static const char *const *scoreTableByTone(int tone, uint16_t &count)
{
    switch (tone)
    {
    case 0:
        count = PHRASES_CAT_COUNT;
        return PHRASES_CAT;
    case 2:
        count = PHRASES_HUNG_COUNT;
        return PHRASES_HUNG;
    default:
        count = PHRASES_BINH_COUNT;
        return PHRASES_BINH;
    }
}

static const char *const *elementTableByBucket(int bucket, uint16_t &count)
{
    switch (bucket)
    {
    case ELEM_MOC:
        count = PHRASES_ELEM_MOC_COUNT;
        return PHRASES_ELEM_MOC;
    case ELEM_HOA:
        count = PHRASES_ELEM_HOA_COUNT;
        return PHRASES_ELEM_HOA;
    case ELEM_KIM:
        count = PHRASES_ELEM_KIM_COUNT;
        return PHRASES_ELEM_KIM;
    case ELEM_THUY:
        count = PHRASES_ELEM_THUY_COUNT;
        return PHRASES_ELEM_THUY;
    case ELEM_THO:
    default:
        count = PHRASES_ELEM_THO_COUNT;
        return PHRASES_ELEM_THO;
    }
}

static const char *const *phaseTableByBucket(int bucket, uint16_t &count)
{
    switch (bucket)
    {
    case 0:
        count = PHRASES_PHASE_EARLY_COUNT;
        return PHRASES_PHASE_EARLY;
    case 1:
        count = PHRASES_PHASE_MID_COUNT;
        return PHRASES_PHASE_MID;
    default:
        count = PHRASES_PHASE_LATE_COUNT;
        return PHRASES_PHASE_LATE;
    }
}

static const char *const *groupTableByBucket(int group, uint16_t &count)
{
    count = PHRASES_BRANCH_GROUP_COUNT;
    switch (group)
    {
    case 0:
        return PHRASES_BRANCH_GROUP_0;
    case 1:
        return PHRASES_BRANCH_GROUP_1;
    case 2:
        return PHRASES_BRANCH_GROUP_2;
    case 3:
        return PHRASES_BRANCH_GROUP_3;
    case 4:
        return PHRASES_BRANCH_GROUP_4;
    default:
        return PHRASES_BRANCH_GROUP_5;
    }
}

static const char *focusCategoryName(uint8_t category)
{
    switch (category)
    {
    case 0:
        return "Công việc";
    case 1:
        return "Tài chính";
    case 2:
        return "Quan hệ";
    case 3:
        return "Sức khỏe";
    case 4:
        return "Học tập";
    case 5:
    default:
        return "Gia đình";
    }
}

static const char *const *focusTableByCategory(uint8_t category, uint16_t &count)
{
    switch (category)
    {
    case 0:
        count = PHRASES_FOCUS_WORK_COUNT;
        return PHRASES_FOCUS_WORK;
    case 1:
        count = PHRASES_FOCUS_FINANCE_COUNT;
        return PHRASES_FOCUS_FINANCE;
    case 2:
        count = PHRASES_FOCUS_RELATION_COUNT;
        return PHRASES_FOCUS_RELATION;
    case 3:
        count = PHRASES_FOCUS_HEALTH_COUNT;
        return PHRASES_FOCUS_HEALTH;
    case 4:
        count = PHRASES_FOCUS_STUDY_COUNT;
        return PHRASES_FOCUS_STUDY;
    case 5:
    default:
        count = PHRASES_FOCUS_FAMILY_COUNT;
        return PHRASES_FOCUS_FAMILY;
    }
}

static void appendTokenText(String &out, const String &s)
{
    if (s.length() > 0)
        out += s;
}

static String renderCompatTemplate(uint16_t compatIdx,
                                   const String &dayBr,
                                   const String &hop1,
                                   const String &hop2,
                                   const String &ky)
{
    if (PHRASES_COMPAT_COUNT == 0)
        return String();

    compatIdx %= PHRASES_COMPAT_COUNT;
    char tpl[220];
    readProgmemString(PHRASES_COMPAT, compatIdx, tpl, sizeof(tpl));

    String out;
    out.reserve(240);

    const char *p = tpl;
    while (*p)
    {
        if (p[0] == '{')
        {
            if (strncmp(p, "{DAYBR}", 7) == 0)
            {
                appendTokenText(out, dayBr);
                p += 7;
                continue;
            }
            if (strncmp(p, "{HOP1}", 6) == 0)
            {
                appendTokenText(out, hop1);
                p += 6;
                continue;
            }
            if (strncmp(p, "{HOP2}", 6) == 0)
            {
                appendTokenText(out, hop2);
                p += 6;
                continue;
            }
            if (strncmp(p, "{KY}", 4) == 0)
            {
                appendTokenText(out, ky);
                p += 4;
                continue;
            }
        }
        out += *p;
        ++p;
    }

    return out;
}

static uint16_t nvsReadIdx(Preferences &prefs, const char *key)
{
    return prefs.getUShort(key, 0);
}

static void nvsWriteIdx(Preferences &prefs, const char *key, uint16_t value)
{
    prefs.putUShort(key, value);
}

} // namespace

String buildContextHeadline(
    int score,
    int lunarDay, int lunarMonth, int lunarYear, bool lunarLeap,
    const LunarDayDetail &dayInfo)
{
    const uint32_t dayKey = static_cast<uint32_t>(lunarYear) * 10000u +
                            static_cast<uint32_t>(lunarMonth) * 100u +
                            static_cast<uint32_t>(lunarDay);

    const int tone = scoreTone(score);
    const int elemBucket = elementToBucket(dayInfo.element);
    const int pBucket = phaseBucket(lunarDay);
    const int group = branchToGroup(dayInfo.branch);

    const bool tamNuong = isTamNuongDay(lunarDay);
    const bool nguyetKy = isNguyetKyDay(lunarDay);

    const uint32_t seed = dailySeed(lunarYear, lunarMonth, lunarDay, score, dayInfo.branch);

    uint16_t scoreCount = 0;
    uint16_t elemCount = 0;
    uint16_t phaseCount = 0;
    uint16_t groupCount = 0;
    uint16_t focusCount = 0;

    const char *const *scoreTable = scoreTableByTone(tone, scoreCount);
    const char *const *elemTable = elementTableByBucket(elemBucket, elemCount);
    const char *const *phaseTable = phaseTableByBucket(pBucket, phaseCount);
    const char *const *groupTable = groupTableByBucket(group, groupCount);

    const uint8_t category = static_cast<uint8_t>(dayKey % 6u);
    const char *categoryName = focusCategoryName(category);
    const char *const *focusTable = focusTableByCategory(category, focusCount);

    int bIdx = branchToIndex(dayInfo.branch);
    if (bIdx < 0)
        bIdx = 0;
    String hop1, hop2, ky;
    zodiacCompatByBranch(bIdx, hop1, hop2, ky);

    uint16_t focusIdx = pickPhraseIndex(seed, FOCUS_SALT_BASE + category, focusCount);
    uint16_t scoreIdx = pickPhraseIndex(seed, SCORE_SALT, scoreCount);
    uint16_t elemIdx = pickPhraseIndex(seed, ELEM_SALT, elemCount);
    uint16_t phaseIdx = pickPhraseIndex(seed, PHASE_SALT, phaseCount);
    uint16_t groupIdx = pickPhraseIndex(seed, GROUP_SALT, groupCount);
    uint16_t compatIdx = pickPhraseIndex(seed, COMPAT_SALT, PHRASES_COMPAT_COUNT);

    uint16_t tamIdx = pickFlagIndexIf(tamNuong, seed, TAM_SALT, PHRASES_FLAG_TAM_NUONG_COUNT);
    uint16_t nguyetIdx = pickFlagIndexIf(nguyetKy, seed, NGUYET_SALT, PHRASES_FLAG_NGUYET_KY_COUNT);
    uint16_t leapIdx = pickFlagIndexIf(lunarLeap, seed, LEAP_SALT, PHRASES_FLAG_LEAP_COUNT);

    Preferences prefs;
    prefs.begin("fortune", false);

    const uint32_t lastDayKey = prefs.getUInt("lastDayKey", 0);
    const bool sameDay = (dayKey == lastDayKey);

    uint16_t lastScoreIdx = nvsReadIdx(prefs, "lastScoreIdx");
    uint16_t lastElemIdx = nvsReadIdx(prefs, "lastElemIdx");
    uint16_t lastPhaseIdx = nvsReadIdx(prefs, "lastPhaseIdx");
    uint16_t lastGroupIdx = nvsReadIdx(prefs, "lastGroupIdx");
    uint16_t lastTamIdx = nvsReadIdx(prefs, "lastTamIdx");
    uint16_t lastNguyetIdx = nvsReadIdx(prefs, "lastNguyetIdx");
    uint16_t lastLeapIdx = nvsReadIdx(prefs, "lastLeapIdx");
    uint16_t lastFocusIdx = nvsReadIdx(prefs, "lastFocusIdx");
    uint16_t lastCompatIdx = nvsReadIdx(prefs, "lastCompatIdx");

    if (sameDay)
    {
        focusIdx = lastFocusIdx;
        scoreIdx = lastScoreIdx;
        elemIdx = lastElemIdx;
        phaseIdx = lastPhaseIdx;
        groupIdx = lastGroupIdx;
        compatIdx = lastCompatIdx;
        tamIdx = tamNuong ? lastTamIdx : IDX_NONE;
        nguyetIdx = nguyetKy ? lastNguyetIdx : IDX_NONE;
        leapIdx = lunarLeap ? lastLeapIdx : IDX_NONE;
    }
    else
    {
        adjustIfRepeat(focusIdx, lastFocusIdx, focusCount);
        adjustIfRepeat(scoreIdx, lastScoreIdx, scoreCount);
        adjustIfRepeat(elemIdx, lastElemIdx, elemCount);
        adjustIfRepeat(phaseIdx, lastPhaseIdx, phaseCount);
        adjustIfRepeat(groupIdx, lastGroupIdx, groupCount);
        adjustIfRepeat(compatIdx, lastCompatIdx, PHRASES_COMPAT_COUNT);
        if (tamIdx != IDX_NONE)
            adjustIfRepeat(tamIdx, lastTamIdx, PHRASES_FLAG_TAM_NUONG_COUNT);
        if (nguyetIdx != IDX_NONE)
            adjustIfRepeat(nguyetIdx, lastNguyetIdx, PHRASES_FLAG_NGUYET_KY_COUNT);
        if (leapIdx != IDX_NONE)
            adjustIfRepeat(leapIdx, lastLeapIdx, PHRASES_FLAG_LEAP_COUNT);
    }

    String out;
    out.reserve(650);

    out += "Chủ đề: ";
    out += categoryName;
    out += ". ";

    appendPhraseByIndex(out, focusTable, focusCount, focusIdx);
    appendPhraseByIndex(out, scoreTable, scoreCount, scoreIdx);
    appendPhraseByIndex(out, elemTable, elemCount, elemIdx);
    appendPhraseByIndex(out, phaseTable, phaseCount, phaseIdx);
    appendPhraseByIndex(out, groupTable, groupCount, groupIdx);

    const String compatText = renderCompatTemplate(compatIdx, dayInfo.branch, hop1, hop2, ky);
    appendPhrase(out, compatText.c_str());

    if (tamIdx != IDX_NONE)
        appendPhraseByIndex(out, PHRASES_FLAG_TAM_NUONG, PHRASES_FLAG_TAM_NUONG_COUNT, tamIdx);
    if (nguyetIdx != IDX_NONE)
        appendPhraseByIndex(out, PHRASES_FLAG_NGUYET_KY, PHRASES_FLAG_NGUYET_KY_COUNT, nguyetIdx);
    if (leapIdx != IDX_NONE)
        appendPhraseByIndex(out, PHRASES_FLAG_LEAP, PHRASES_FLAG_LEAP_COUNT, leapIdx);

    if (!sameDay)
    {
        prefs.putUInt("lastDayKey", dayKey);
        nvsWriteIdx(prefs, "lastScoreIdx", scoreIdx);
        nvsWriteIdx(prefs, "lastElemIdx", elemIdx);
        nvsWriteIdx(prefs, "lastPhaseIdx", phaseIdx);
        nvsWriteIdx(prefs, "lastGroupIdx", groupIdx);
        nvsWriteIdx(prefs, "lastTamIdx", tamIdx);
        nvsWriteIdx(prefs, "lastNguyetIdx", nguyetIdx);
        nvsWriteIdx(prefs, "lastLeapIdx", leapIdx);
        nvsWriteIdx(prefs, "lastFocusIdx", focusIdx);
        nvsWriteIdx(prefs, "lastCompatIdx", compatIdx);
    }

    prefs.end();
    return out;
}

void debugContextHeadlineSample(const LunarDayDetail &dayInfo)
{
    const int score = 0;
    const int lunarDay = 3;
    const int lunarMonth = 1;
    const int lunarYear = 2026;
    const bool lunarLeap = false;

    const uint32_t dayKey = static_cast<uint32_t>(lunarYear) * 10000u +
                            static_cast<uint32_t>(lunarMonth) * 100u +
                            static_cast<uint32_t>(lunarDay);
    const uint8_t category = static_cast<uint8_t>(dayKey % 6u);
    const uint32_t seed = dailySeed(lunarYear, lunarMonth, lunarDay, score, String("Tý"));

    uint16_t cScore = 0;
    uint16_t cElem = 0;
    uint16_t cPhase = 0;
    uint16_t cGroup = 0;
    uint16_t cFocus = 0;

    scoreTableByTone(scoreTone(score), cScore);
    elementTableByBucket(elementToBucket(dayInfo.element), cElem);
    phaseTableByBucket(phaseBucket(lunarDay), cPhase);
    groupTableByBucket(branchToGroup("Tý"), cGroup);
    focusTableByCategory(category, cFocus);

    const uint16_t iFocus = pickIndex(seed, FOCUS_SALT_BASE + category, cFocus);
    const uint16_t iScore = pickIndex(seed, SCORE_SALT, cScore);
    const uint16_t iElem = pickIndex(seed, ELEM_SALT, cElem);
    const uint16_t iPhase = pickIndex(seed, PHASE_SALT, cPhase);
    const uint16_t iGroup = pickIndex(seed, GROUP_SALT, cGroup);
    const uint16_t iCompat = pickIndex(seed, COMPAT_SALT, PHRASES_COMPAT_COUNT);
    const uint16_t iTam = pickIndex(seed, TAM_SALT, PHRASES_FLAG_TAM_NUONG_COUNT);
    const uint16_t iNguyet = pickIndex(seed, NGUYET_SALT, PHRASES_FLAG_NGUYET_KY_COUNT);
    const uint16_t iLeap = pickIndex(seed, LEAP_SALT, PHRASES_FLAG_LEAP_COUNT);

    Serial.printf("[HEADLINE_TEST] seed=%lu cat=%u idx(focus,score,elem,phase,group,compat,tam,nguyet,leap)=(%u,%u,%u,%u,%u,%u,%u,%u,%u)\n",
                  static_cast<unsigned long>(seed),
                  static_cast<unsigned>(category),
                  iFocus, iScore, iElem, iPhase, iGroup, iCompat, iTam, iNguyet, iLeap);

    LunarDayDetail sample = dayInfo;
    sample.branch = "Tý";
    String headline = buildContextHeadline(score, lunarDay, lunarMonth, lunarYear, lunarLeap, sample);
    Serial.println(String("[HEADLINE_TEST] ") + headline);
}
