#pragma once

#include <Arduino.h>
#include <pgmspace.h>

static const char PHRASE_GROUP_0_0[] PROGMEM = "Chi Tý/Hợi: hợp học hỏi và hoạch định.";
static const char PHRASE_GROUP_0_1[] PROGMEM = "Nhóm Tý/Hợi: nên ưu tiên tư duy chiến lược.";
static const char PHRASE_GROUP_0_2[] PROGMEM = "Tý/Hợi: hợp gom thông tin trước khi quyết.";

static const char PHRASE_GROUP_1_0[] PROGMEM = "Chi Dần/Mão: hợp mở rộng quan hệ việc làm.";
static const char PHRASE_GROUP_1_1[] PROGMEM = "Nhóm Dần/Mão: nên tăng trao đổi trực tiếp.";
static const char PHRASE_GROUP_1_2[] PROGMEM = "Dần/Mão: hợp thúc đẩy hợp tác nhỏ.";

static const char PHRASE_GROUP_2_0[] PROGMEM = "Chi Thìn/Tỵ: hợp chuẩn hóa và rà soát.";
static const char PHRASE_GROUP_2_1[] PROGMEM = "Nhóm Thìn/Tỵ: nên ưu tiên hồ sơ, quy trình.";
static const char PHRASE_GROUP_2_2[] PROGMEM = "Thìn/Tỵ: hợp làm việc cần sự tỉ mỉ.";

static const char PHRASE_GROUP_3_0[] PROGMEM = "Chi Ngọ/Mùi: hợp chăm nền tảng gia đạo, hậu cần.";
static const char PHRASE_GROUP_3_1[] PROGMEM = "Nhóm Ngọ/Mùi: nên giữ nhịp bền và đều.";
static const char PHRASE_GROUP_3_2[] PROGMEM = "Ngọ/Mùi: hợp xử lý việc cần sự ổn định.";

static const char PHRASE_GROUP_4_0[] PROGMEM = "Chi Thân/Dậu: hợp dọn backlog và chốt nhanh.";
static const char PHRASE_GROUP_4_1[] PROGMEM = "Nhóm Thân/Dậu: nên ưu tiên việc có deadline gần.";
static const char PHRASE_GROUP_4_2[] PROGMEM = "Thân/Dậu: hợp hoàn tất các đầu việc ngắn.";

static const char PHRASE_GROUP_5_0[] PROGMEM = "Chi Tuất/Sửu: hợp củng cố mục tiêu dài hạn.";
static const char PHRASE_GROUP_5_1[] PROGMEM = "Nhóm Tuất/Sửu: nên chọn phương án chắc chắn.";
static const char PHRASE_GROUP_5_2[] PROGMEM = "Tuất/Sửu: hợp việc bền vững, ít rủi ro.";

static const char *const PHRASES_BRANCH_GROUP_0[] PROGMEM = {PHRASE_GROUP_0_0, PHRASE_GROUP_0_1, PHRASE_GROUP_0_2};
static const char *const PHRASES_BRANCH_GROUP_1[] PROGMEM = {PHRASE_GROUP_1_0, PHRASE_GROUP_1_1, PHRASE_GROUP_1_2};
static const char *const PHRASES_BRANCH_GROUP_2[] PROGMEM = {PHRASE_GROUP_2_0, PHRASE_GROUP_2_1, PHRASE_GROUP_2_2};
static const char *const PHRASES_BRANCH_GROUP_3[] PROGMEM = {PHRASE_GROUP_3_0, PHRASE_GROUP_3_1, PHRASE_GROUP_3_2};
static const char *const PHRASES_BRANCH_GROUP_4[] PROGMEM = {PHRASE_GROUP_4_0, PHRASE_GROUP_4_1, PHRASE_GROUP_4_2};
static const char *const PHRASES_BRANCH_GROUP_5[] PROGMEM = {PHRASE_GROUP_5_0, PHRASE_GROUP_5_1, PHRASE_GROUP_5_2};

static constexpr uint16_t PHRASES_BRANCH_GROUP_COUNT = 3;
