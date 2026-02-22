// fortune_phrases_score.h
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// =========================
// SCORE PHRASES (PROGMEM)
// Tone buckets:
//   - CÁT  : score >= 2
//   - BÌNH : score == 0..1 (or your neutral range)
//   - HUNG : score <= -1
// =========================

// ---------- CÁT (30) ----------
static const char P_CAT_00[] PROGMEM = "Khí vận vượng, thuận tiến hành việc đã chuẩn bị kỹ.";
static const char P_CAT_01[] PROGMEM = "Can Chi tương sinh, dễ được trợ lực đúng lúc.";
static const char P_CAT_02[] PROGMEM = "Ngày có cát khí nâng đỡ, hợp triển khai kế hoạch mới.";
static const char P_CAT_03[] PROGMEM = "Hanh thông trong phạm vi kiểm soát; làm có tính toán sẽ lợi.";
static const char P_CAT_04[] PROGMEM = "Vận trình sáng, hợp chốt việc quan trọng sau khi rà soát.";
static const char P_CAT_05[] PROGMEM = "Dễ gặp quý nhân hỗ trợ; nên chủ động kết nối và trao đổi.";
static const char P_CAT_06[] PROGMEM = "Khí ngày thuận chiều, xử lý việc tồn đọng sẽ nhanh gọn.";
static const char P_CAT_07[] PROGMEM = "Hợp cầu tài vừa phải; ưu tiên bền vững hơn “đánh nhanh”.";
static const char P_CAT_08[] PROGMEM = "Cát khí mở đường, hợp bắt đầu bước đi dài hạn.";
static const char P_CAT_09[] PROGMEM = "Vượng khí tăng dần; ký kết nhỏ, chốt hạng mục sẽ thuận.";
static const char P_CAT_10[] PROGMEM = "Ngày hợp mở rộng hợp tác nếu đã có nền tảng.";
static const char P_CAT_11[] PROGMEM = "Can ngày sinh trợ; quyết định có dữ liệu sẽ sinh lợi.";
static const char P_CAT_12[] PROGMEM = "Thuận cho thương lượng; giữ mềm dẻo nhưng rõ ràng.";
static const char P_CAT_13[] PROGMEM = "Khí vận hỗ trợ sự chủ động; đừng để cơ hội trôi qua.";
static const char P_CAT_14[] PROGMEM = "Hợp khởi tạo dự án, nhưng vẫn cần kiểm tra chi tiết.";
static const char P_CAT_15[] PROGMEM = "Ngày dễ hanh thông về tài chính nhỏ; chi tiêu có kế hoạch.";
static const char P_CAT_16[] PROGMEM = "Có cơ hội củng cố vị thế; làm đúng nhịp sẽ lên.";
static const char P_CAT_17[] PROGMEM = "Vận khí ổn định theo hướng tăng; tiến từng bước sẽ lợi.";
static const char P_CAT_18[] PROGMEM = "Hợp triển khai việc đã trì hoãn trước đó.";
static const char P_CAT_19[] PROGMEM = "Cát khí nâng đỡ quan hệ hợp tác; nói chuyện dễ thông.";
static const char P_CAT_20[] PROGMEM = "Dễ đạt kết quả tích cực khi giữ nguyên nguyên tắc.";
static const char P_CAT_21[] PROGMEM = "Thuận cho sắp xếp lại mục tiêu ngắn hạn và lịch trình.";
static const char P_CAT_22[] PROGMEM = "Hợp đẩy tiến độ việc đang vào nhịp; chốt từng mốc rõ.";
static const char P_CAT_23[] PROGMEM = "Cát vận giúp giảm trở ngại nhỏ; đừng tự tạo áp lực.";
static const char P_CAT_24[] PROGMEM = "Dễ giải quyết hồ sơ tồn nếu tập trung một việc.";
static const char P_CAT_25[] PROGMEM = "Khí ngày sáng, hợp tháo gỡ vấn đề còn vướng.";
static const char P_CAT_26[] PROGMEM = "Hợp tái khởi động kế hoạch cũ đã chuẩn bị.";
static const char P_CAT_27[] PROGMEM = "Ngày dễ tạo đà tăng trưởng nếu không nóng vội.";
static const char P_CAT_28[] PROGMEM = "Có lợi cho hành động có chuẩn bị và thông tin đầy đủ.";
static const char P_CAT_29[] PROGMEM = "Cát khí vượng; tiến hành việc trọng tâm sẽ thuận.";

static const char* const PHRASES_CAT[] PROGMEM = {
  P_CAT_00,P_CAT_01,P_CAT_02,P_CAT_03,P_CAT_04,P_CAT_05,P_CAT_06,P_CAT_07,P_CAT_08,P_CAT_09,
  P_CAT_10,P_CAT_11,P_CAT_12,P_CAT_13,P_CAT_14,P_CAT_15,P_CAT_16,P_CAT_17,P_CAT_18,P_CAT_19,
  P_CAT_20,P_CAT_21,P_CAT_22,P_CAT_23,P_CAT_24,P_CAT_25,P_CAT_26,P_CAT_27,P_CAT_28,P_CAT_29
};
static const uint16_t PHRASES_CAT_COUNT = sizeof(PHRASES_CAT) / sizeof(PHRASES_CAT[0]);


// ---------- BÌNH (30) ----------
static const char P_BINH_00[] PROGMEM = "Ngày bình hòa, không xung không hợp rõ rệt.";
static const char P_BINH_01[] PROGMEM = "Khí vận trung tính; nên giữ tiến độ đều.";
static const char P_BINH_02[] PROGMEM = "Không lợi cũng không hại; ưu tiên ổn định.";
static const char P_BINH_03[] PROGMEM = "Can Chi cân bằng; hợp hoàn thiện việc dang dở.";
static const char P_BINH_04[] PROGMEM = "Hợp rà soát kế hoạch hơn là mở rộng lớn.";
static const char P_BINH_05[] PROGMEM = "Tiến chậm mà chắc sẽ lợi hơn bứt tốc.";
static const char P_BINH_06[] PROGMEM = "Ngày phù hợp củng cố nền tảng và dọn việc tồn.";
static const char P_BINH_07[] PROGMEM = "Bình vận; tránh đổi hướng đột ngột giữa chừng.";
static const char P_BINH_08[] PROGMEM = "Hợp chỉnh sửa chi tiết trước khi công bố.";
static const char P_BINH_09[] PROGMEM = "Khí ngày không nổi bật; làm trong phạm vi an toàn.";
static const char P_BINH_10[] PROGMEM = "Không nên đặt kỳ vọng quá cao; làm chắc phần mình.";
static const char P_BINH_11[] PROGMEM = "Giữ kế hoạch hiện tại là phương án hợp lý.";
static const char P_BINH_12[] PROGMEM = "Hợp xử lý giấy tờ và chuẩn hóa quy trình.";
static const char P_BINH_13[] PROGMEM = "Nên tập trung việc có thể kiểm soát, tránh dàn trải.";
static const char P_BINH_14[] PROGMEM = "Khí vận ổn định; không cần liều, cũng không cần lùi.";
static const char P_BINH_15[] PROGMEM = "Hợp rà soát tài chính trước khi quyết định mới.";
static const char P_BINH_16[] PROGMEM = "Không thuận cho thay đổi lớn; nhưng tốt cho chuẩn bị.";
static const char P_BINH_17[] PROGMEM = "Nên duy trì nhịp công việc hiện có để giảm sai sót.";
static const char P_BINH_18[] PROGMEM = "Bình khí; hợp tổng hợp dữ liệu và phân tích.";
static const char P_BINH_19[] PROGMEM = "Ưu tiên việc nền tảng hơn là mở rộng.";
static const char P_BINH_20[] PROGMEM = "Hợp kiểm tra điều khoản; chốt sau khi rõ ràng.";
static const char P_BINH_21[] PROGMEM = "Ngày hợp đánh giá nội bộ và hoàn thiện quy trình.";
static const char P_BINH_22[] PROGMEM = "Bình vận giúp giảm rủi ro nếu làm theo kế hoạch.";
static const char P_BINH_23[] PROGMEM = "Không có xung đột lớn; vẫn cần kiên nhẫn.";
static const char P_BINH_24[] PROGMEM = "Hợp hoàn tất phần việc đang triển khai.";
static const char P_BINH_25[] PROGMEM = "Hợp củng cố quan hệ hiện có; nói chuyện vừa đủ.";
static const char P_BINH_26[] PROGMEM = "Không nên thay đổi hướng đi khi chưa đủ dữ liệu.";
static const char P_BINH_27[] PROGMEM = "Ngày phù hợp dọn backlog và sắp xếp lại lịch.";
static const char P_BINH_28[] PROGMEM = "Bình khí thiên về ổn định hơn tăng trưởng nhanh.";
static const char P_BINH_29[] PROGMEM = "Giữ nhịp đều; kết quả sẽ đến đúng thời điểm.";

static const char* const PHRASES_BINH[] PROGMEM = {
  P_BINH_00,P_BINH_01,P_BINH_02,P_BINH_03,P_BINH_04,P_BINH_05,P_BINH_06,P_BINH_07,P_BINH_08,P_BINH_09,
  P_BINH_10,P_BINH_11,P_BINH_12,P_BINH_13,P_BINH_14,P_BINH_15,P_BINH_16,P_BINH_17,P_BINH_18,P_BINH_19,
  P_BINH_20,P_BINH_21,P_BINH_22,P_BINH_23,P_BINH_24,P_BINH_25,P_BINH_26,P_BINH_27,P_BINH_28,P_BINH_29
};
static const uint16_t PHRASES_BINH_COUNT = sizeof(PHRASES_BINH) / sizeof(PHRASES_BINH[0]);


// ---------- HUNG (30) ----------
static const char P_HUNG_00[] PROGMEM = "Ngày có hung khí chi phối; tránh việc đại sự.";
static const char P_HUNG_01[] PROGMEM = "Can Chi tương khắc; nên chậm lại một nhịp.";
static const char P_HUNG_02[] PROGMEM = "Khí vận suy; không hợp khai trương hay ký kết lớn.";
static const char P_HUNG_03[] PROGMEM = "Dễ phát sinh hiểu lầm; hãy kiểm tra kỹ thông tin.";
static const char P_HUNG_04[] PROGMEM = "Không lợi cho đầu tư mạo hiểm trong hôm nay.";
static const char P_HUNG_05[] PROGMEM = "Hung vận làm quyết định nóng dễ sai; ưu tiên chắc chắn.";
static const char P_HUNG_06[] PROGMEM = "Nên giữ ổn định thay vì mở rộng.";
static const char P_HUNG_07[] PROGMEM = "Ngày dễ hao tài nhỏ nếu chi tiêu bốc đồng.";
static const char P_HUNG_08[] PROGMEM = "Khí ngày nghịch chiều; tránh thay đổi lớn.";
static const char P_HUNG_09[] PROGMEM = "Không hợp tranh chấp hay thương lượng căng thẳng.";
static const char P_HUNG_10[] PROGMEM = "Dễ phát sinh trục trặc kỹ thuật nhỏ; kiểm tra trước khi làm.";
static const char P_HUNG_11[] PROGMEM = "Nên hạn chế cam kết dài hạn khi chưa thật cần thiết.";
static const char P_HUNG_12[] PROGMEM = "Hung khí làm giảm trợ lực; dự phòng thêm thời gian.";
static const char P_HUNG_13[] PROGMEM = "Không thuận cho ký hợp đồng quy mô lớn.";
static const char P_HUNG_14[] PROGMEM = "Nên tập trung xử lý việc nội bộ; tránh ôm đồm.";
static const char P_HUNG_15[] PROGMEM = "Ngày dễ chậm trễ ngoài dự kiến; đừng gấp.";
static const char P_HUNG_16[] PROGMEM = "Tránh quyết định theo cảm xúc; bám dữ liệu.";
static const char P_HUNG_17[] PROGMEM = "Không hợp xuất hành xa hoặc đi lại gấp.";
static const char P_HUNG_18[] PROGMEM = "Dễ gặp thị phi nếu tranh luận gay gắt; nên nhường.";
static const char P_HUNG_19[] PROGMEM = "Lùi một bước để giữ yên ổn, tránh xung đột.";
static const char P_HUNG_20[] PROGMEM = "Hung vận khiến việc mới khó vào nhịp; chờ thời điểm tốt hơn.";
static const char P_HUNG_21[] PROGMEM = "Không lợi cho thay đổi cấu trúc lớn trong hôm nay.";
static const char P_HUNG_22[] PROGMEM = "Dễ hao năng lượng nếu dàn trải; làm ít nhưng chắc.";
static const char P_HUNG_23[] PROGMEM = "Nên trì hoãn việc trọng đại sang ngày khác.";
static const char P_HUNG_24[] PROGMEM = "Không hợp mở rộng đầu tư lúc này; giữ an toàn tài chính.";
static const char P_HUNG_25[] PROGMEM = "Khí vận giảm; ưu tiên an toàn là chính.";
static const char P_HUNG_26[] PROGMEM = "Dễ sai sót nếu thiếu kiểm tra; làm chậm để đúng.";
static const char P_HUNG_27[] PROGMEM = "Hung khí khiến tiến độ khó ổn định; giữ kế hoạch tối giản.";
static const char P_HUNG_28[] PROGMEM = "Không thuận cho quyết định rủi ro cao; chọn phương án chắc.";
static const char P_HUNG_29[] PROGMEM = "Nên xử lý việc nhỏ trước; tránh đại sự.";

static const char* const PHRASES_HUNG[] PROGMEM = {
  P_HUNG_00,P_HUNG_01,P_HUNG_02,P_HUNG_03,P_HUNG_04,P_HUNG_05,P_HUNG_06,P_HUNG_07,P_HUNG_08,P_HUNG_09,
  P_HUNG_10,P_HUNG_11,P_HUNG_12,P_HUNG_13,P_HUNG_14,P_HUNG_15,P_HUNG_16,P_HUNG_17,P_HUNG_18,P_HUNG_19,
  P_HUNG_20,P_HUNG_21,P_HUNG_22,P_HUNG_23,P_HUNG_24,P_HUNG_25,P_HUNG_26,P_HUNG_27,P_HUNG_28,P_HUNG_29
};
static const uint16_t PHRASES_HUNG_COUNT = sizeof(PHRASES_HUNG) / sizeof(PHRASES_HUNG[0]);
