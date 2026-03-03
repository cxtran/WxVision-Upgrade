#pragma once
#include <Arduino.h>

class ScrollLine {
public:
    ScrollLine(int screenWidth, unsigned int scrollSpeedMs);

    void setLines(const String lines[], int n, bool resetPosition = true);
    void setScrollSpeed(unsigned int ms);
    void setScrollStepPx(int px);
    void setStartPauseMs(unsigned int ms);
    void setContinuousWrap(bool enabled);

    void setTitleText(const String& text);
    void setTitleMode(bool enable);

    void reset();
    void update();
    void draw(int x, int y, uint16_t defaultColor);

    bool isActive() const;
    bool selectedLineNeedsScroll() const;
    bool consumeCycleCompleted();
    bool consumeEnteredFromRight();

    // Set colors for lines (arrays must have at least n elements)
    void setLineColors(uint16_t textColors[], uint16_t bgColors[], int n);
    // Set colors for title text and background
    void setTitleColors(uint16_t textColor, uint16_t bgColor);

    void setTitleScrollDirection(int dir) ;
    void setLineScrollDirection(int lineIndex, int dir) ;
    void setBounceEnabled(bool enabled);
private:
    bool _bounceEnabled;
    int _screenWidth;
    unsigned int _scrollSpeedMs;
    unsigned int _startPauseMs;
    int _scrollStepPx;
    bool _continuousWrap;

    static const int MAX_LINES = 16;
    String _lines[MAX_LINES];
    int _lineCount;

    int _scrollOffsets[MAX_LINES];
    int _scrollDirections[MAX_LINES];
    unsigned long _lastScrollTimes[MAX_LINES];
    int _selIndex;

    String _titleText;
    int _titleTextWidth;
    int _titleScrollOffset;
    int _titleScrollDirection;
    unsigned long _titleLastScrollTime;

    bool _isTitleMode;

    uint16_t _textColors[MAX_LINES];
    uint16_t _bgColors[MAX_LINES];
    uint16_t _titleTextColor;
    uint16_t _titleBgColor;
    bool _cycleCompleted;
    bool _enteredFromRight;

    int getTextWidth(const char* text) const;
};
