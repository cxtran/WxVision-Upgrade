#pragma once

#include <Arduino.h>

enum
{
    ELEM_MOC = 0,
    ELEM_HOA = 1,
    ELEM_THO = 2,
    ELEM_KIM = 3,
    ELEM_THUY = 4
};

static inline bool equalsAny(const String &v, const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr)
{
    if (v == a)
        return true;
    if (b && v == b)
        return true;
    if (c && v == c)
        return true;
    if (d && v == d)
        return true;
    return false;
}

static inline String trimCopy(const String &in)
{
    String s = in;
    s.trim();
    return s;
}

static inline String asciiLower(const String &in)
{
    String out = in;
    for (size_t i = 0; i < out.length(); ++i)
    {
        char ch = out[i];
        if (ch >= 'A' && ch <= 'Z')
            out[i] = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

static inline int elementToBucket(const String &element)
{
    String s = trimCopy(element);
    String lo = asciiLower(s);

    if (equalsAny(s, "Moc", "moc") || equalsAny(lo, "moc", "mộc"))
        return ELEM_MOC;
    if (equalsAny(s, "Hoa", "hoa") || equalsAny(lo, "hoa", "hỏa", "hoả"))
        return ELEM_HOA;
    if (equalsAny(s, "Tho", "tho") || equalsAny(lo, "tho", "thổ"))
        return ELEM_THO;
    if (equalsAny(s, "Kim", "kim") || lo == "kim")
        return ELEM_KIM;
    if (equalsAny(s, "Thuy", "thuy") || equalsAny(lo, "thuy", "thủy", "thuỷ"))
        return ELEM_THUY;

    return ELEM_THO;
}

static inline int branchToIndex(const String &br)
{
    String s = trimCopy(br);
    String lo = asciiLower(s);

    if (equalsAny(s, "Tý", "tý") || lo == "ty")
        return 0;
    if (equalsAny(s, "Sửu", "sửu", "SỬU", "suu") || lo == "suu")
        return 1;
    if (equalsAny(s, "Dần", "dần", "DẦN", "dan") || lo == "dan")
        return 2;
    if (equalsAny(s, "Mão", "mão", "MÃO", "mao") || lo == "mao")
        return 3;
    if (equalsAny(s, "Thìn", "thìn", "THÌN", "thin") || lo == "thin")
        return 4;
    if (equalsAny(s, "Tỵ", "tỵ", "Tị", "tị") || lo == "ty")
        return 5;
    if (equalsAny(s, "Ngọ", "ngọ", "NGỌ", "ngo") || lo == "ngo")
        return 6;
    if (equalsAny(s, "Mùi", "mùi", "MÙI", "mui") || lo == "mui")
        return 7;
    if (equalsAny(s, "Thân", "thân", "THÂN", "than") || lo == "than")
        return 8;
    if (equalsAny(s, "Dậu", "dậu", "DẬU", "dau") || lo == "dau")
        return 9;
    if (equalsAny(s, "Tuất", "tuất", "TUẤT", "tuat") || lo == "tuat")
        return 10;
    if (equalsAny(s, "Hợi", "hợi", "HỢI", "hoi") || lo == "hoi")
        return 11;

    return -1;
}

static inline int branchToGroup(const String &br)
{
    const int idx = branchToIndex(br);
    switch (idx)
    {
    case 0:
    case 11:
        return 0; // Tý/Hợi
    case 2:
    case 3:
        return 1; // Dần/Mão
    case 4:
    case 5:
        return 2; // Thìn/Tỵ
    case 6:
    case 7:
        return 3; // Ngọ/Mùi
    case 8:
    case 9:
        return 4; // Thân/Dậu
    case 10:
    case 1:
        return 5; // Tuất/Sửu
    default:
        return 0;
    }
}

static inline int phaseBucket(int lunarDay)
{
    if (lunarDay <= 10)
        return 0;
    if (lunarDay <= 20)
        return 1;
    return 2;
}

static inline int scoreTone(int score)
{
    if (score >= 2)
        return 0; // CAT
    if (score <= -1)
        return 2; // HUNG
    return 1;     // BINH
}
