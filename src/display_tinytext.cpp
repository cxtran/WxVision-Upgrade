#include "display_tinytext.h"

#include "display.h"

enum ViToneMarkBits : uint16_t
{
    VI_TONE_NONE = 0,
    VI_TONE_ACUTE = 1 << 0,
    VI_TONE_GRAVE = 1 << 1,
    VI_TONE_HOOK = 1 << 2,
    VI_TONE_TILDE = 1 << 3,
    VI_TONE_DOT = 1 << 4,
    VI_SHAPE_CIRC = 1 << 5,
    VI_SHAPE_BREVE = 1 << 6,
    VI_SHAPE_HORN = 1 << 7,
    VI_SHAPE_DBAR = 1 << 8
};

struct ViGlyphTiny
{
    char base;
    uint16_t marks;
};

static bool decodeUtf8Tiny(const String &text, int &i, uint32_t &cp)
{
    if (i >= text.length())
        return false;
    const uint8_t c0 = static_cast<uint8_t>(text[i++]);
    if ((c0 & 0x80) == 0)
    {
        cp = c0;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i < text.length())
    {
        const uint8_t c1 = static_cast<uint8_t>(text[i++]);
        cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && (i + 1) < text.length())
    {
        const uint8_t c1 = static_cast<uint8_t>(text[i++]);
        const uint8_t c2 = static_cast<uint8_t>(text[i++]);
        cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        return true;
    }
    cp = '?';
    return true;
}

static ViGlyphTiny mapVietnameseTiny(uint32_t cp)
{
    ViGlyphTiny g = {'?', VI_TONE_NONE};
    if (cp < 128)
    {
        g.base = static_cast<char>(cp);
        return g;
    }

    switch (cp)
    {
    case 0x0110: g = {'D', VI_SHAPE_DBAR}; break;
    case 0x0111: g = {'d', VI_SHAPE_DBAR}; break;

    case 0x00C0: g = {'A', VI_TONE_GRAVE}; break;
    case 0x00C1: g = {'A', VI_TONE_ACUTE}; break;
    case 0x1EA2: g = {'A', VI_TONE_HOOK}; break;
    case 0x00C3: g = {'A', VI_TONE_TILDE}; break;
    case 0x1EA0: g = {'A', VI_TONE_DOT}; break;
    case 0x0102: g = {'A', VI_SHAPE_BREVE}; break;
    case 0x1EAE: g = {'A', VI_SHAPE_BREVE | VI_TONE_ACUTE}; break;
    case 0x1EB0: g = {'A', VI_SHAPE_BREVE | VI_TONE_GRAVE}; break;
    case 0x1EB2: g = {'A', VI_SHAPE_BREVE | VI_TONE_HOOK}; break;
    case 0x1EB4: g = {'A', VI_SHAPE_BREVE | VI_TONE_TILDE}; break;
    case 0x1EB6: g = {'A', VI_SHAPE_BREVE | VI_TONE_DOT}; break;
    case 0x00C2: g = {'A', VI_SHAPE_CIRC}; break;
    case 0x1EA4: g = {'A', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1EA6: g = {'A', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1EA8: g = {'A', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1EAA: g = {'A', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1EAC: g = {'A', VI_SHAPE_CIRC | VI_TONE_DOT}; break;

    case 0x00C8: g = {'E', VI_TONE_GRAVE}; break;
    case 0x00C9: g = {'E', VI_TONE_ACUTE}; break;
    case 0x1EBA: g = {'E', VI_TONE_HOOK}; break;
    case 0x1EBC: g = {'E', VI_TONE_TILDE}; break;
    case 0x1EB8: g = {'E', VI_TONE_DOT}; break;
    case 0x00CA: g = {'E', VI_SHAPE_CIRC}; break;
    case 0x1EBE: g = {'E', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1EC0: g = {'E', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1EC2: g = {'E', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1EC4: g = {'E', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1EC6: g = {'E', VI_SHAPE_CIRC | VI_TONE_DOT}; break;

    case 0x00CC: g = {'I', VI_TONE_GRAVE}; break;
    case 0x00CD: g = {'I', VI_TONE_ACUTE}; break;
    case 0x1EC8: g = {'I', VI_TONE_HOOK}; break;
    case 0x0128: g = {'I', VI_TONE_TILDE}; break;
    case 0x1ECA: g = {'I', VI_TONE_DOT}; break;

    case 0x00D2: g = {'O', VI_TONE_GRAVE}; break;
    case 0x00D3: g = {'O', VI_TONE_ACUTE}; break;
    case 0x1ECE: g = {'O', VI_TONE_HOOK}; break;
    case 0x00D5: g = {'O', VI_TONE_TILDE}; break;
    case 0x1ECC: g = {'O', VI_TONE_DOT}; break;
    case 0x00D4: g = {'O', VI_SHAPE_CIRC}; break;
    case 0x1ED0: g = {'O', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1ED2: g = {'O', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1ED4: g = {'O', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1ED6: g = {'O', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1ED8: g = {'O', VI_SHAPE_CIRC | VI_TONE_DOT}; break;
    case 0x01A0: g = {'O', VI_SHAPE_HORN}; break;
    case 0x1EDA: g = {'O', VI_SHAPE_HORN | VI_TONE_ACUTE}; break;
    case 0x1EDC: g = {'O', VI_SHAPE_HORN | VI_TONE_GRAVE}; break;
    case 0x1EDE: g = {'O', VI_SHAPE_HORN | VI_TONE_HOOK}; break;
    case 0x1EE0: g = {'O', VI_SHAPE_HORN | VI_TONE_TILDE}; break;
    case 0x1EE2: g = {'O', VI_SHAPE_HORN | VI_TONE_DOT}; break;

    case 0x00D9: g = {'U', VI_TONE_GRAVE}; break;
    case 0x00DA: g = {'U', VI_TONE_ACUTE}; break;
    case 0x1EE6: g = {'U', VI_TONE_HOOK}; break;
    case 0x0168: g = {'U', VI_TONE_TILDE}; break;
    case 0x1EE4: g = {'U', VI_TONE_DOT}; break;
    case 0x01AF: g = {'U', VI_SHAPE_HORN}; break;
    case 0x1EE8: g = {'U', VI_SHAPE_HORN | VI_TONE_ACUTE}; break;
    case 0x1EEA: g = {'U', VI_SHAPE_HORN | VI_TONE_GRAVE}; break;
    case 0x1EEC: g = {'U', VI_SHAPE_HORN | VI_TONE_HOOK}; break;
    case 0x1EEE: g = {'U', VI_SHAPE_HORN | VI_TONE_TILDE}; break;
    case 0x1EF0: g = {'U', VI_SHAPE_HORN | VI_TONE_DOT}; break;
    case 0x00DD: g = {'Y', VI_TONE_ACUTE}; break;
    case 0x1EF2: g = {'Y', VI_TONE_GRAVE}; break;
    case 0x1EF6: g = {'Y', VI_TONE_HOOK}; break;
    case 0x1EF8: g = {'Y', VI_TONE_TILDE}; break;
    case 0x1EF4: g = {'Y', VI_TONE_DOT}; break;

    case 0x00E0: g = {'a', VI_TONE_GRAVE}; break;
    case 0x00E1: g = {'a', VI_TONE_ACUTE}; break;
    case 0x1EA3: g = {'a', VI_TONE_HOOK}; break;
    case 0x00E3: g = {'a', VI_TONE_TILDE}; break;
    case 0x1EA1: g = {'a', VI_TONE_DOT}; break;
    case 0x0103: g = {'a', VI_SHAPE_BREVE}; break;
    case 0x1EAF: g = {'a', VI_SHAPE_BREVE | VI_TONE_ACUTE}; break;
    case 0x1EB1: g = {'a', VI_SHAPE_BREVE | VI_TONE_GRAVE}; break;
    case 0x1EB3: g = {'a', VI_SHAPE_BREVE | VI_TONE_HOOK}; break;
    case 0x1EB5: g = {'a', VI_SHAPE_BREVE | VI_TONE_TILDE}; break;
    case 0x1EB7: g = {'a', VI_SHAPE_BREVE | VI_TONE_DOT}; break;
    case 0x00E2: g = {'a', VI_SHAPE_CIRC}; break;
    case 0x1EA5: g = {'a', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1EA7: g = {'a', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1EA9: g = {'a', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1EAB: g = {'a', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1EAD: g = {'a', VI_SHAPE_CIRC | VI_TONE_DOT}; break;

    case 0x00E8: g = {'e', VI_TONE_GRAVE}; break;
    case 0x00E9: g = {'e', VI_TONE_ACUTE}; break;
    case 0x1EBB: g = {'e', VI_TONE_HOOK}; break;
    case 0x1EBD: g = {'e', VI_TONE_TILDE}; break;
    case 0x1EB9: g = {'e', VI_TONE_DOT}; break;
    case 0x00EA: g = {'e', VI_SHAPE_CIRC}; break;
    case 0x1EBF: g = {'e', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1EC1: g = {'e', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1EC3: g = {'e', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1EC5: g = {'e', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1EC7: g = {'e', VI_SHAPE_CIRC | VI_TONE_DOT}; break;

    case 0x00EC: g = {'i', VI_TONE_GRAVE}; break;
    case 0x00ED: g = {'i', VI_TONE_ACUTE}; break;
    case 0x1EC9: g = {'i', VI_TONE_HOOK}; break;
    case 0x0129: g = {'i', VI_TONE_TILDE}; break;
    case 0x1ECB: g = {'i', VI_TONE_DOT}; break;

    case 0x00F2: g = {'o', VI_TONE_GRAVE}; break;
    case 0x00F3: g = {'o', VI_TONE_ACUTE}; break;
    case 0x1ECF: g = {'o', VI_TONE_HOOK}; break;
    case 0x00F5: g = {'o', VI_TONE_TILDE}; break;
    case 0x1ECD: g = {'o', VI_TONE_DOT}; break;
    case 0x00F4: g = {'o', VI_SHAPE_CIRC}; break;
    case 0x1ED1: g = {'o', VI_SHAPE_CIRC | VI_TONE_ACUTE}; break;
    case 0x1ED3: g = {'o', VI_SHAPE_CIRC | VI_TONE_GRAVE}; break;
    case 0x1ED5: g = {'o', VI_SHAPE_CIRC | VI_TONE_HOOK}; break;
    case 0x1ED7: g = {'o', VI_SHAPE_CIRC | VI_TONE_TILDE}; break;
    case 0x1ED9: g = {'o', VI_SHAPE_CIRC | VI_TONE_DOT}; break;
    case 0x01A1: g = {'o', VI_SHAPE_HORN}; break;
    case 0x1EDB: g = {'o', VI_SHAPE_HORN | VI_TONE_ACUTE}; break;
    case 0x1EDD: g = {'o', VI_SHAPE_HORN | VI_TONE_GRAVE}; break;
    case 0x1EDF: g = {'o', VI_SHAPE_HORN | VI_TONE_HOOK}; break;
    case 0x1EE1: g = {'o', VI_SHAPE_HORN | VI_TONE_TILDE}; break;
    case 0x1EE3: g = {'o', VI_SHAPE_HORN | VI_TONE_DOT}; break;

    case 0x00F9: g = {'u', VI_TONE_GRAVE}; break;
    case 0x00FA: g = {'u', VI_TONE_ACUTE}; break;
    case 0x1EE7: g = {'u', VI_TONE_HOOK}; break;
    case 0x0169: g = {'u', VI_TONE_TILDE}; break;
    case 0x1EE5: g = {'u', VI_TONE_DOT}; break;
    case 0x01B0: g = {'u', VI_SHAPE_HORN}; break;
    case 0x1EE9: g = {'u', VI_SHAPE_HORN | VI_TONE_ACUTE}; break;
    case 0x1EEB: g = {'u', VI_SHAPE_HORN | VI_TONE_GRAVE}; break;
    case 0x1EED: g = {'u', VI_SHAPE_HORN | VI_TONE_HOOK}; break;
    case 0x1EEF: g = {'u', VI_SHAPE_HORN | VI_TONE_TILDE}; break;
    case 0x1EF1: g = {'u', VI_SHAPE_HORN | VI_TONE_DOT}; break;
    case 0x00FD: g = {'y', VI_TONE_ACUTE}; break;
    case 0x1EF3: g = {'y', VI_TONE_GRAVE}; break;
    case 0x1EF7: g = {'y', VI_TONE_HOOK}; break;
    case 0x1EF9: g = {'y', VI_TONE_TILDE}; break;
    case 0x1EF5: g = {'y', VI_TONE_DOT}; break;
    default: break;
    }
    return g;
}

static void drawTinyVietnameseMarks(int x, int yTop, uint16_t marks, uint16_t color)
{
    if (marks & VI_SHAPE_CIRC)
    {
        dma_display->drawPixel(x + 1, yTop - 1, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
        dma_display->drawPixel(x + 3, yTop - 1, color);
    }
    if (marks & VI_SHAPE_BREVE)
    {
        dma_display->drawPixel(x + 1, yTop - 2, color);
        dma_display->drawPixel(x + 2, yTop - 1, color);
        dma_display->drawPixel(x + 3, yTop - 2, color);
    }
    if (marks & VI_SHAPE_HORN)
    {
        dma_display->drawPixel(x + 4, yTop, color);
        dma_display->drawPixel(x + 5, yTop - 1, color);
    }
    if (marks & VI_TONE_ACUTE)
    {
        dma_display->drawPixel(x + 3, yTop - 2, color);
        dma_display->drawPixel(x + 4, yTop - 3, color);
    }
    if (marks & VI_TONE_GRAVE)
    {
        dma_display->drawPixel(x + 1, yTop - 3, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
    }
    if (marks & VI_TONE_HOOK)
    {
        dma_display->drawPixel(x + 2, yTop - 3, color);
        dma_display->drawPixel(x + 3, yTop - 3, color);
        dma_display->drawPixel(x + 3, yTop - 2, color);
    }
    if (marks & VI_TONE_TILDE)
    {
        dma_display->drawPixel(x + 1, yTop - 3, color);
        dma_display->drawPixel(x + 2, yTop - 2, color);
        dma_display->drawPixel(x + 3, yTop - 3, color);
        dma_display->drawPixel(x + 4, yTop - 2, color);
    }
    if (marks & VI_TONE_DOT)
    {
        dma_display->drawPixel(x + 2, yTop + 7, color);
    }
    if (marks & VI_SHAPE_DBAR)
    {
        dma_display->drawPixel(x + 1, yTop + 3, color);
        dma_display->drawPixel(x + 2, yTop + 3, color);
        dma_display->drawPixel(x + 3, yTop + 3, color);
    }
}

int drawTinyVietnameseText(int x, int yTop, const String &text, uint16_t color)
{
    dma_display->setFont(nullptr);
    dma_display->setTextWrap(false);
    int cursorX = x;
    int i = 0;
    while (i < text.length())
    {
        uint32_t cp = '?';
        if (!decodeUtf8Tiny(text, i, cp))
            break;
        ViGlyphTiny glyph = mapVietnameseTiny(cp);
        dma_display->drawChar(cursorX, yTop, static_cast<unsigned char>(glyph.base), color, myBLACK, 1);
        if (glyph.marks != VI_TONE_NONE)
            drawTinyVietnameseMarks(cursorX, yTop, glyph.marks, color);
        cursorX += 6;
    }
    return cursorX - x;
}

uint16_t tinyVietnameseTextWidth(const String &text)
{
    int count = 0;
    int i = 0;
    while (i < text.length())
    {
        uint32_t cp = '?';
        if (!decodeUtf8Tiny(text, i, cp))
            break;
        count++;
    }
    return static_cast<uint16_t>(max(1, count * 6));
}
