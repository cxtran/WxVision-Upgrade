#pragma once

#include <Arduino.h>
#include <pgmspace.h>

static const char PHRASE_FLAG_TAMNUONG_0[] PROGMEM = "Tam Nương: nên hoãn việc lớn, giữ an toàn.";
static const char PHRASE_FLAG_TAMNUONG_1[] PROGMEM = "Ngày Tam Nương: ưu tiên kiểm tra và chỉnh sửa.";
static const char PHRASE_FLAG_TAMNUONG_2[] PROGMEM = "Tam Nương: hợp việc nội bộ, tránh mạo hiểm.";

static const char PHRASE_FLAG_NGUYETKY_0[] PROGMEM = "Nguyệt Kỵ: tránh quyết định theo cảm xúc.";
static const char PHRASE_FLAG_NGUYETKY_1[] PROGMEM = "Ngày Nguyệt Kỵ: nên chậm lại để rà soát.";
static const char PHRASE_FLAG_NGUYETKY_2[] PROGMEM = "Nguyệt Kỵ: ưu tiên quản trị rủi ro.";

static const char PHRASE_FLAG_LEAP_0[] PROGMEM = "Tháng nhuận: nên xác nhận kỹ điều khoản.";
static const char PHRASE_FLAG_LEAP_1[] PROGMEM = "Tháng nhuận: hợp kiểm chứng thông tin trước khi chốt.";
static const char PHRASE_FLAG_LEAP_2[] PROGMEM = "Nhuận nguyệt: ưu tiên phương án dự phòng.";

static const char *const PHRASES_FLAG_TAM_NUONG[] PROGMEM = {
    PHRASE_FLAG_TAMNUONG_0,
    PHRASE_FLAG_TAMNUONG_1,
    PHRASE_FLAG_TAMNUONG_2};

static const char *const PHRASES_FLAG_NGUYET_KY[] PROGMEM = {
    PHRASE_FLAG_NGUYETKY_0,
    PHRASE_FLAG_NGUYETKY_1,
    PHRASE_FLAG_NGUYETKY_2};

static const char *const PHRASES_FLAG_LEAP[] PROGMEM = {
    PHRASE_FLAG_LEAP_0,
    PHRASE_FLAG_LEAP_1,
    PHRASE_FLAG_LEAP_2};

static constexpr uint16_t PHRASES_FLAG_TAM_NUONG_COUNT = 3;
static constexpr uint16_t PHRASES_FLAG_NGUYET_KY_COUNT = 3;
static constexpr uint16_t PHRASES_FLAG_LEAP_COUNT = 3;
