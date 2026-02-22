// fortune_phrases_compat.h
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// =========================
// ZODIAC COMPATIBILITY (PROGMEM)
// Templates with placeholders:
//   {DAYBR}  : dayInfo.branch (e.g., "Tý")
//   {HOP1}   : hop1 (e.g., "Thân")
//   {HOP2}   : hop2 (e.g., "Thìn")
//   {KY}     : ky  (e.g., "Ngọ")
// Keep mixed tone: traditional structure + practical wording.
// =========================

static const char P_COMPAT_00[] PROGMEM = "Ngày {DAYBR} hợp tuổi {HOP1}, {HOP2}; kỵ {KY}, nên tránh đối đầu.";
static const char P_COMPAT_01[] PROGMEM = "Hợp {HOP1}/{HOP2}; kỵ {KY}. Việc quan trọng nên chọn người hợp tuổi.";
static const char P_COMPAT_02[] PROGMEM = "Kỵ {KY}; nếu phải xử lý việc khó, ưu tiên mềm dẻo và tránh tranh cãi.";
static const char P_COMPAT_03[] PROGMEM = "Ngày {DAYBR} dễ thuận khi có {HOP1} hoặc {HOP2} hỗ trợ; hạn chế xung với {KY}.";
static const char P_COMPAT_04[] PROGMEM = "Hợp tuổi {HOP1}, {HOP2} để bàn việc; kỵ {KY} cho chuyện nhạy cảm.";
static const char P_COMPAT_05[] PROGMEM = "Gặp việc cần chốt nhanh, nhờ người hợp {HOP1}/{HOP2} sẽ nhẹ hơn.";
static const char P_COMPAT_06[] PROGMEM = "Ngày {DAYBR} kỵ {KY}; tránh lời nặng, tránh quyết định khi nóng.";
static const char P_COMPAT_07[] PROGMEM = "Hợp {HOP1}, {HOP2} cho phối hợp; kỵ {KY} cho thương lượng căng.";
static const char P_COMPAT_08[] PROGMEM = "Ngày {DAYBR} hợp tác với {HOP1}/{HOP2} dễ thông; tránh khơi xung tuổi {KY}.";
static const char P_COMPAT_09[] PROGMEM = "Hợp tuổi {HOP1}, {HOP2}; kỵ {KY}. Việc tiền bạc nên rõ ràng giấy tờ.";
static const char P_COMPAT_10[] PROGMEM = "Ngày {DAYBR} nên ưu tiên người hợp {HOP1}/{HOP2} khi cần thống nhất.";
static const char P_COMPAT_11[] PROGMEM = "Kỵ {KY}; đừng tranh đúng sai, hãy tranh “cách làm” cho êm.";
static const char P_COMPAT_12[] PROGMEM = "Hợp {HOP1}/{HOP2} để mở lời; kỵ {KY} thì nên giảm kỳ vọng.";
static const char P_COMPAT_13[] PROGMEM = "Ngày {DAYBR} dễ có quý nhân {HOP1}/{HOP2}; hạn chế va chạm với {KY}.";
static const char P_COMPAT_14[] PROGMEM = "Hợp tuổi {HOP1}, {HOP2} cho việc nhóm; kỵ {KY} cho chuyện cảm xúc.";
static const char P_COMPAT_15[] PROGMEM = "Nếu cần ký kết nhỏ, chọn giờ tốt và người hợp {HOP1}/{HOP2}; tránh xung {KY}.";
static const char P_COMPAT_16[] PROGMEM = "Ngày {DAYBR} kỵ {KY}; nên nói rõ phạm vi, tránh hiểu lầm.";
static const char P_COMPAT_17[] PROGMEM = "Hợp {HOP1}/{HOP2} để chốt tiến độ; kỵ {KY} thì nên chậm lại.";
static const char P_COMPAT_18[] PROGMEM = "Gặp vấn đề khó, nhờ {HOP1} hoặc {HOP2} góp ý sẽ sáng hơn.";
static const char P_COMPAT_19[] PROGMEM = "Kỵ {KY}; hạn chế nhắc chuyện cũ, ưu tiên giải pháp thực tế.";
static const char P_COMPAT_20[] PROGMEM = "Ngày {DAYBR} hợp tuổi {HOP1}, {HOP2}; kỵ {KY}. Việc nhà nên nhường nhịn.";
static const char P_COMPAT_21[] PROGMEM = "Hợp {HOP1}/{HOP2} cho mở rộng quan hệ; kỵ {KY} thì giữ khoảng cách.";
static const char P_COMPAT_22[] PROGMEM = "Ngày {DAYBR} nên chọn đồng hành hợp {HOP1}, {HOP2}; tránh tranh luận với {KY}.";
static const char P_COMPAT_23[] PROGMEM = "Kỵ {KY}; nếu buộc phải làm chung, hãy chốt quy ước và deadline rõ.";
static const char P_COMPAT_24[] PROGMEM = "Hợp {HOP1}, {HOP2} để cầu tài nhỏ; kỵ {KY} thì tránh hứa vội.";
static const char P_COMPAT_25[] PROGMEM = "Ngày {DAYBR} dễ thuận “đúng người”: ưu tiên {HOP1}/{HOP2}, hạn chế {KY}.";
static const char P_COMPAT_26[] PROGMEM = "Hợp tuổi {HOP1}/{HOP2}; kỵ {KY}. Gặp việc nhạy cảm, chọn lời mềm.";
static const char P_COMPAT_27[] PROGMEM = "Ngày {DAYBR} nên nhờ người hợp {HOP1}, {HOP2} kiểm tra giúp cho chắc.";
static const char P_COMPAT_28[] PROGMEM = "Kỵ {KY}; tránh quyết định theo cảm xúc, nhất là trong quan hệ.";
static const char P_COMPAT_29[] PROGMEM = "Hợp {HOP1}, {HOP2} để bàn chuyện lớn; kỵ {KY} thì ưu tiên hòa khí.";

static const char* const PHRASES_COMPAT[] PROGMEM = {
  P_COMPAT_00,P_COMPAT_01,P_COMPAT_02,P_COMPAT_03,P_COMPAT_04,
  P_COMPAT_05,P_COMPAT_06,P_COMPAT_07,P_COMPAT_08,P_COMPAT_09,
  P_COMPAT_10,P_COMPAT_11,P_COMPAT_12,P_COMPAT_13,P_COMPAT_14,
  P_COMPAT_15,P_COMPAT_16,P_COMPAT_17,P_COMPAT_18,P_COMPAT_19,
  P_COMPAT_20,P_COMPAT_21,P_COMPAT_22,P_COMPAT_23,P_COMPAT_24,
  P_COMPAT_25,P_COMPAT_26,P_COMPAT_27,P_COMPAT_28,P_COMPAT_29
};
static const uint16_t PHRASES_COMPAT_COUNT = sizeof(PHRASES_COMPAT) / sizeof(PHRASES_COMPAT[0]);
