#pragma once

#include <Arduino.h>
#include <pgmspace.h>

static const char PHRASE_PHASE_EARLY_0[] PROGMEM = "Đầu tháng: hợp khởi tạo nhịp mới.";
static const char PHRASE_PHASE_EARLY_1[] PROGMEM = "Đầu kỳ: nên đặt mục tiêu ngắn hạn.";
static const char PHRASE_PHASE_EARLY_2[] PROGMEM = "Đầu tháng: ưu tiên mở đầu gọn gàng.";

static const char PHRASE_PHASE_MID_0[] PROGMEM = "Giữa tháng: hợp tăng tốc hạng mục chính.";
static const char PHRASE_PHASE_MID_1[] PROGMEM = "Giữa kỳ: nên giữ kỷ luật tiến độ.";
static const char PHRASE_PHASE_MID_2[] PROGMEM = "Nhịp giữa tháng: ưu tiên chốt việc dang dở.";

static const char PHRASE_PHASE_LATE_0[] PROGMEM = "Cuối tháng: hợp tổng kết và dọn tồn.";
static const char PHRASE_PHASE_LATE_1[] PROGMEM = "Cuối kỳ: nên khóa việc quan trọng trước.";
static const char PHRASE_PHASE_LATE_2[] PROGMEM = "Cuối tháng: ưu tiên hoàn thiện hơn mở mới.";

static const char *const PHRASES_PHASE_EARLY[] PROGMEM = {
    PHRASE_PHASE_EARLY_0,
    PHRASE_PHASE_EARLY_1,
    PHRASE_PHASE_EARLY_2};

static const char *const PHRASES_PHASE_MID[] PROGMEM = {
    PHRASE_PHASE_MID_0,
    PHRASE_PHASE_MID_1,
    PHRASE_PHASE_MID_2};

static const char *const PHRASES_PHASE_LATE[] PROGMEM = {
    PHRASE_PHASE_LATE_0,
    PHRASE_PHASE_LATE_1,
    PHRASE_PHASE_LATE_2};

static constexpr uint16_t PHRASES_PHASE_EARLY_COUNT = 3;
static constexpr uint16_t PHRASES_PHASE_MID_COUNT = 3;
static constexpr uint16_t PHRASES_PHASE_LATE_COUNT = 3;
