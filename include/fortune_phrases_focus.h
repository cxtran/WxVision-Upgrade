// fortune_phrases_focus.h
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// =========================
// DAILY FOCUS (PROGMEM)
// Category rotation: category = dayKey % 6
// 0 Công việc, 1 Tài chính, 2 Quan hệ, 3 Sức khỏe, 4 Học tập, 5 Gia đình
// Phrases are safe + mixed tone (traditional framing, modern practical).
// =========================

// ---------- CÔNG VIỆC (20) ----------
static const char P_FOCUS_WORK_00[] PROGMEM = "Công việc: chọn 1 mục tiêu chính, chốt từng mốc rõ ràng.";
static const char P_FOCUS_WORK_01[] PROGMEM = "Công việc: ưu tiên việc quan trọng trước, việc gấp sau.";
static const char P_FOCUS_WORK_02[] PROGMEM = "Công việc: rà soát backlog, dọn việc tồn cho nhẹ nhịp.";
static const char P_FOCUS_WORK_03[] PROGMEM = "Công việc: làm theo checklist để tránh sót chi tiết.";
static const char P_FOCUS_WORK_04[] PROGMEM = "Công việc: giữ nhịp đều; đừng bứt tốc khi thiếu dữ liệu.";
static const char P_FOCUS_WORK_05[] PROGMEM = "Công việc: chốt phạm vi trước khi nhận thêm việc mới.";
static const char P_FOCUS_WORK_06[] PROGMEM = "Công việc: ưu tiên việc nội bộ, chuẩn hóa quy trình.";
static const char P_FOCUS_WORK_07[] PROGMEM = "Công việc: tránh ôm đồm; làm ít nhưng chắc sẽ thắng.";
static const char P_FOCUS_WORK_08[] PROGMEM = "Công việc: kiểm tra đầu vào trước khi triển khai để đỡ sửa.";
static const char P_FOCUS_WORK_09[] PROGMEM = "Công việc: làm việc theo nhịp, tránh thay đổi hướng giữa chừng.";
static const char P_FOCUS_WORK_10[] PROGMEM = "Công việc: hợp chốt hạng mục nhỏ, đóng việc cho gọn.";
static const char P_FOCUS_WORK_11[] PROGMEM = "Công việc: chia nhỏ bước; hoàn thành 1 việc rồi mới mở việc.";
static const char P_FOCUS_WORK_12[] PROGMEM = "Công việc: giữ nguyên tắc, nhưng giao tiếp mềm sẽ dễ thông.";
static const char P_FOCUS_WORK_13[] PROGMEM = "Công việc: ưu tiên chất lượng; làm đúng ngay từ đầu.";
static const char P_FOCUS_WORK_14[] PROGMEM = "Công việc: chọn “việc tạo đà” để tiến nhanh hơn ngày mai.";
static const char P_FOCUS_WORK_15[] PROGMEM = "Công việc: họp ít nhưng rõ; chốt kết luận và người chịu trách nhiệm.";
static const char P_FOCUS_WORK_16[] PROGMEM = "Công việc: rà lại deadline và rủi ro; thêm thời gian dự phòng.";
static const char P_FOCUS_WORK_17[] PROGMEM = "Công việc: ưu tiên việc đo lường được; tránh mục tiêu mơ hồ.";
static const char P_FOCUS_WORK_18[] PROGMEM = "Công việc: hoàn thiện hồ sơ/tài liệu để tránh vướng về sau.";
static const char P_FOCUS_WORK_19[] PROGMEM = "Công việc: làm chắc phần nền, vận sẽ tự thông dần.";

static const char* const PHRASES_FOCUS_WORK[] PROGMEM = {
  P_FOCUS_WORK_00,P_FOCUS_WORK_01,P_FOCUS_WORK_02,P_FOCUS_WORK_03,P_FOCUS_WORK_04,
  P_FOCUS_WORK_05,P_FOCUS_WORK_06,P_FOCUS_WORK_07,P_FOCUS_WORK_08,P_FOCUS_WORK_09,
  P_FOCUS_WORK_10,P_FOCUS_WORK_11,P_FOCUS_WORK_12,P_FOCUS_WORK_13,P_FOCUS_WORK_14,
  P_FOCUS_WORK_15,P_FOCUS_WORK_16,P_FOCUS_WORK_17,P_FOCUS_WORK_18,P_FOCUS_WORK_19
};
static const uint16_t PHRASES_FOCUS_WORK_COUNT = sizeof(PHRASES_FOCUS_WORK) / sizeof(PHRASES_FOCUS_WORK[0]);

// ---------- TÀI CHÍNH (20) ----------
static const char P_FOCUS_FIN_00[] PROGMEM = "Tài chính: cân đối thu chi; tránh quyết định bốc đồng.";
static const char P_FOCUS_FIN_01[] PROGMEM = "Tài chính: ưu tiên tích lũy nhỏ; đừng tham lợi nhanh.";
static const char P_FOCUS_FIN_02[] PROGMEM = "Tài chính: rà soát hóa đơn, đối soát trước khi chốt.";
static const char P_FOCUS_FIN_03[] PROGMEM = "Tài chính: giữ ngân sách; cắt lãng phí sẽ lợi lâu.";
static const char P_FOCUS_FIN_04[] PROGMEM = "Tài chính: khoản lớn nên kiểm tra điều khoản và rủi ro.";
static const char P_FOCUS_FIN_05[] PROGMEM = "Tài chính: tránh vay mượn vội; ưu tiên an toàn dòng tiền.";
static const char P_FOCUS_FIN_06[] PROGMEM = "Tài chính: hợp dọn nợ nhỏ, đóng các khoản vụn vặt.";
static const char P_FOCUS_FIN_07[] PROGMEM = "Tài chính: chốt mua sắm theo kế hoạch, tránh mua theo hứng.";
static const char P_FOCUS_FIN_08[] PROGMEM = "Tài chính: đầu tư thời gian vào tính toán trước khi xuống tiền.";
static const char P_FOCUS_FIN_09[] PROGMEM = "Tài chính: giữ kỷ luật; vận tài đến từ thói quen tốt.";
static const char P_FOCUS_FIN_10[] PROGMEM = "Tài chính: ưu tiên quỹ dự phòng; đừng dồn hết vào một chỗ.";
static const char P_FOCUS_FIN_11[] PROGMEM = "Tài chính: kiểm tra phí ẩn, điều kiện kèm theo khi ký.";
static const char P_FOCUS_FIN_12[] PROGMEM = "Tài chính: hợp rà soát tài sản, bảo hiểm, gia hạn cần thiết.";
static const char P_FOCUS_FIN_13[] PROGMEM = "Tài chính: tránh “lướt” cảm xúc; chọn phương án chắc.";
static const char P_FOCUS_FIN_14[] PROGMEM = "Tài chính: ghi chép rõ; tiền rõ thì lòng yên.";
static const char P_FOCUS_FIN_15[] PROGMEM = "Tài chính: chậm mà chắc; ưu tiên bền hơn lời mời hấp dẫn.";
static const char P_FOCUS_FIN_16[] PROGMEM = "Tài chính: hợp thương lượng giá; đừng ngại hỏi điều kiện.";
static const char P_FOCUS_FIN_17[] PROGMEM = "Tài chính: gom chi phí nhỏ; tránh rò rỉ lặt vặt.";
static const char P_FOCUS_FIN_18[] PROGMEM = "Tài chính: việc lớn nên chia giai đoạn để giảm rủi ro.";
static const char P_FOCUS_FIN_19[] PROGMEM = "Tài chính: giữ nguyên tắc; hứa vội dễ hao.";

static const char* const PHRASES_FOCUS_FINANCE[] PROGMEM = {
  P_FOCUS_FIN_00,P_FOCUS_FIN_01,P_FOCUS_FIN_02,P_FOCUS_FIN_03,P_FOCUS_FIN_04,
  P_FOCUS_FIN_05,P_FOCUS_FIN_06,P_FOCUS_FIN_07,P_FOCUS_FIN_08,P_FOCUS_FIN_09,
  P_FOCUS_FIN_10,P_FOCUS_FIN_11,P_FOCUS_FIN_12,P_FOCUS_FIN_13,P_FOCUS_FIN_14,
  P_FOCUS_FIN_15,P_FOCUS_FIN_16,P_FOCUS_FIN_17,P_FOCUS_FIN_18,P_FOCUS_FIN_19
};
static const uint16_t PHRASES_FOCUS_FINANCE_COUNT = sizeof(PHRASES_FOCUS_FINANCE) / sizeof(PHRASES_FOCUS_FINANCE[0]);

// ---------- QUAN HỆ (20) ----------
static const char P_FOCUS_REL_00[] PROGMEM = "Quan hệ: nói rõ kỳ vọng; tránh suy đoán.";
static const char P_FOCUS_REL_01[] PROGMEM = "Quan hệ: mềm dẻo nhưng ranh giới rõ sẽ yên.";
static const char P_FOCUS_REL_02[] PROGMEM = "Quan hệ: ưu tiên lắng nghe; nói ít mà đúng sẽ lợi.";
static const char P_FOCUS_REL_03[] PROGMEM = "Quan hệ: tránh tranh đúng sai; tập trung giải pháp.";
static const char P_FOCUS_REL_04[] PROGMEM = "Quan hệ: hợp kết nối lại người cũ; giữ lời nhẹ.";
static const char P_FOCUS_REL_05[] PROGMEM = "Quan hệ: chốt lịch rõ ràng; tránh hẹn mơ hồ.";
static const char P_FOCUS_REL_06[] PROGMEM = "Quan hệ: đừng nóng; một câu mềm bằng mười câu cứng.";
static const char P_FOCUS_REL_07[] PROGMEM = "Quan hệ: việc nhạy cảm nên nói riêng, không nói trước đám đông.";
static const char P_FOCUS_REL_08[] PROGMEM = "Quan hệ: ưu tiên hòa khí; thắng thua hôm nay không quan trọng.";
static const char P_FOCUS_REL_09[] PROGMEM = "Quan hệ: hỏi lại cho chắc; hiểu đúng thì việc mới trôi.";
static const char P_FOCUS_REL_10[] PROGMEM = "Quan hệ: hợp gặp gỡ; nhưng chọn chủ đề nhẹ nhàng.";
static const char P_FOCUS_REL_11[] PROGMEM = "Quan hệ: tôn trọng khác biệt; đừng ép người theo mình.";
static const char P_FOCUS_REL_12[] PROGMEM = "Quan hệ: nhắn tin ngắn gọn; tránh dài dòng gây hiểu lầm.";
static const char P_FOCUS_REL_13[] PROGMEM = "Quan hệ: giữ chữ tín; hứa ít làm nhiều sẽ bền.";
static const char P_FOCUS_REL_14[] PROGMEM = "Quan hệ: tránh khơi chuyện cũ; nói việc hiện tại thôi.";
static const char P_FOCUS_REL_15[] PROGMEM = "Quan hệ: biết dừng đúng lúc; im lặng đôi khi là khôn.";
static const char P_FOCUS_REL_16[] PROGMEM = "Quan hệ: hợp xin góp ý; đừng phòng thủ quá sớm.";
static const char P_FOCUS_REL_17[] PROGMEM = "Quan hệ: giao tiếp rõ ràng; mọi thứ sẽ nhẹ hơn.";
static const char P_FOCUS_REL_18[] PROGMEM = "Quan hệ: chọn lời tử tế; vận tốt thường đến từ miệng lành.";
static const char P_FOCUS_REL_19[] PROGMEM = "Quan hệ: giữ nhịp bình; đừng phản ứng ngay khi chưa rõ.";

static const char* const PHRASES_FOCUS_RELATION[] PROGMEM = {
  P_FOCUS_REL_00,P_FOCUS_REL_01,P_FOCUS_REL_02,P_FOCUS_REL_03,P_FOCUS_REL_04,
  P_FOCUS_REL_05,P_FOCUS_REL_06,P_FOCUS_REL_07,P_FOCUS_REL_08,P_FOCUS_REL_09,
  P_FOCUS_REL_10,P_FOCUS_REL_11,P_FOCUS_REL_12,P_FOCUS_REL_13,P_FOCUS_REL_14,
  P_FOCUS_REL_15,P_FOCUS_REL_16,P_FOCUS_REL_17,P_FOCUS_REL_18,P_FOCUS_REL_19
};
static const uint16_t PHRASES_FOCUS_RELATION_COUNT = sizeof(PHRASES_FOCUS_RELATION) / sizeof(PHRASES_FOCUS_RELATION[0]);

// ---------- SỨC KHỎE (20) ----------
static const char P_FOCUS_HEA_00[] PROGMEM = "Sức khỏe: giữ nhịp ngủ; thiếu ngủ là hao vận.";
static const char P_FOCUS_HEA_01[] PROGMEM = "Sức khỏe: uống đủ nước; cơ thể nhẹ thì đầu óc sáng.";
static const char P_FOCUS_HEA_02[] PROGMEM = "Sức khỏe: ăn vừa phải; tránh quá đà vì cảm xúc.";
static const char P_FOCUS_HEA_03[] PROGMEM = "Sức khỏe: vận động nhẹ 15–30 phút để thông khí.";
static const char P_FOCUS_HEA_04[] PROGMEM = "Sức khỏe: nghỉ ngắn đúng lúc; đừng cố quá sức.";
static const char P_FOCUS_HEA_05[] PROGMEM = "Sức khỏe: tránh tranh luận căng; stress làm suy khí.";
static const char P_FOCUS_HEA_06[] PROGMEM = "Sức khỏe: ưu tiên việc nhẹ; cơ thể cần hồi phục.";
static const char P_FOCUS_HEA_07[] PROGMEM = "Sức khỏe: chú ý an toàn khi đi lại; chậm mà chắc.";
static const char P_FOCUS_HEA_08[] PROGMEM = "Sức khỏe: giữ ấm/giữ mát đúng cách; đừng chủ quan.";
static const char P_FOCUS_HEA_09[] PROGMEM = "Sức khỏe: hít thở sâu; bình tâm thì khí mới thuận.";
static const char P_FOCUS_HEA_10[] PROGMEM = "Sức khỏe: hạn chế cà phê muộn; ngủ ngon là đại lợi.";
static const char P_FOCUS_HEA_11[] PROGMEM = "Sức khỏe: lắng nghe cơ thể; đau mỏi thì giảm nhịp.";
static const char P_FOCUS_HEA_12[] PROGMEM = "Sức khỏe: chỉnh tư thế ngồi; đỡ mỏi vai gáy.";
static const char P_FOCUS_HEA_13[] PROGMEM = "Sức khỏe: ăn đúng giờ; ổn định là gốc.";
static const char P_FOCUS_HEA_14[] PROGMEM = "Sức khỏe: thư giãn mắt; nghỉ 20 giây mỗi 20 phút.";
static const char P_FOCUS_HEA_15[] PROGMEM = "Sức khỏe: đừng ăn quá mặn/ngọt; giữ cân bằng sẽ bền.";
static const char P_FOCUS_HEA_16[] PROGMEM = "Sức khỏe: tắm nắng nhẹ; tinh thần sẽ sáng hơn.";
static const char P_FOCUS_HEA_17[] PROGMEM = "Sức khỏe: dọn không gian sống; sạch là thông khí.";
static const char P_FOCUS_HEA_18[] PROGMEM = "Sức khỏe: giảm tin xấu; tâm an thì thân an.";
static const char P_FOCUS_HEA_19[] PROGMEM = "Sức khỏe: ưu tiên phục hồi; làm ít mà chắc hôm nay.";

static const char* const PHRASES_FOCUS_HEALTH[] PROGMEM = {
  P_FOCUS_HEA_00,P_FOCUS_HEA_01,P_FOCUS_HEA_02,P_FOCUS_HEA_03,P_FOCUS_HEA_04,
  P_FOCUS_HEA_05,P_FOCUS_HEA_06,P_FOCUS_HEA_07,P_FOCUS_HEA_08,P_FOCUS_HEA_09,
  P_FOCUS_HEA_10,P_FOCUS_HEA_11,P_FOCUS_HEA_12,P_FOCUS_HEA_13,P_FOCUS_HEA_14,
  P_FOCUS_HEA_15,P_FOCUS_HEA_16,P_FOCUS_HEA_17,P_FOCUS_HEA_18,P_FOCUS_HEA_19
};
static const uint16_t PHRASES_FOCUS_HEALTH_COUNT = sizeof(PHRASES_FOCUS_HEALTH) / sizeof(PHRASES_FOCUS_HEALTH[0]);

// ---------- HỌC TẬP (20) ----------
static const char P_FOCUS_STU_00[] PROGMEM = "Học tập: học ít nhưng đều; tích lũy mới bền.";
static const char P_FOCUS_STU_01[] PROGMEM = "Học tập: ôn trọng tâm; đừng dàn trải quá rộng.";
static const char P_FOCUS_STU_02[] PROGMEM = "Học tập: ghi chú ngắn; tóm ý sẽ nhớ lâu.";
static const char P_FOCUS_STU_03[] PROGMEM = "Học tập: làm bài tập nhỏ; hiểu sâu hơn đọc nhiều.";
static const char P_FOCUS_STU_04[] PROGMEM = "Học tập: học theo mục tiêu; tránh học theo cảm hứng.";
static const char P_FOCUS_STU_05[] PROGMEM = "Học tập: hỏi một câu đúng sẽ tiết kiệm một giờ.";
static const char P_FOCUS_STU_06[] PROGMEM = "Học tập: thử lại kiến thức bằng ví dụ thực tế.";
static const char P_FOCUS_STU_07[] PROGMEM = "Học tập: nghỉ đúng lúc; não mệt thì học không vào.";
static const char P_FOCUS_STU_08[] PROGMEM = "Học tập: chia phiên 25–50 phút; giữ nhịp tập trung.";
static const char P_FOCUS_STU_09[] PROGMEM = "Học tập: chốt 1 kỹ năng; làm tới nơi sẽ lợi.";
static const char P_FOCUS_STU_10[] PROGMEM = "Học tập: đọc ít nhưng chất; ưu tiên tài liệu chuẩn.";
static const char P_FOCUS_STU_11[] PROGMEM = "Học tập: luyện lại phần yếu; bền hơn học phần mới.";
static const char P_FOCUS_STU_12[] PROGMEM = "Học tập: đặt câu hỏi; hiểu “vì sao” sẽ chắc.";
static const char P_FOCUS_STU_13[] PROGMEM = "Học tập: tối ưu môi trường học; yên tĩnh là lợi.";
static const char P_FOCUS_STU_14[] PROGMEM = "Học tập: làm sơ đồ; hệ thống hóa sẽ dễ nhớ.";
static const char P_FOCUS_STU_15[] PROGMEM = "Học tập: ôn lại nhanh cuối ngày; bồi gốc rất tốt.";
static const char P_FOCUS_STU_16[] PROGMEM = "Học tập: học theo dự án; làm ra sản phẩm sẽ nhớ.";
static const char P_FOCUS_STU_17[] PROGMEM = "Học tập: tránh đa nhiệm; một việc một lúc.";
static const char P_FOCUS_STU_18[] PROGMEM = "Học tập: hỏi người giỏi; đi tắt đúng chỗ.";
static const char P_FOCUS_STU_19[] PROGMEM = "Học tập: giữ kỷ luật; vận tốt đến từ thói quen.";

static const char* const PHRASES_FOCUS_STUDY[] PROGMEM = {
  P_FOCUS_STU_00,P_FOCUS_STU_01,P_FOCUS_STU_02,P_FOCUS_STU_03,P_FOCUS_STU_04,
  P_FOCUS_STU_05,P_FOCUS_STU_06,P_FOCUS_STU_07,P_FOCUS_STU_08,P_FOCUS_STU_09,
  P_FOCUS_STU_10,P_FOCUS_STU_11,P_FOCUS_STU_12,P_FOCUS_STU_13,P_FOCUS_STU_14,
  P_FOCUS_STU_15,P_FOCUS_STU_16,P_FOCUS_STU_17,P_FOCUS_STU_18,P_FOCUS_STU_19
};
static const uint16_t PHRASES_FOCUS_STUDY_COUNT = sizeof(PHRASES_FOCUS_STUDY) / sizeof(PHRASES_FOCUS_STUDY[0]);

// ---------- GIA ĐÌNH (20) ----------
static const char P_FOCUS_FAM_00[] PROGMEM = "Gia đình: ưu tiên hòa khí; chuyện nhỏ nói nhẹ cho qua.";
static const char P_FOCUS_FAM_01[] PROGMEM = "Gia đình: quan tâm bằng hành động; hỏi han đúng lúc.";
static const char P_FOCUS_FAM_02[] PROGMEM = "Gia đình: dọn dẹp nhà cửa; sạch là thông khí.";
static const char P_FOCUS_FAM_03[] PROGMEM = "Gia đình: chốt việc nhà theo danh sách; đỡ quên.";
static const char P_FOCUS_FAM_04[] PROGMEM = "Gia đình: tránh nhắc chuyện cũ; nói chuyện hiện tại thôi.";
static const char P_FOCUS_FAM_05[] PROGMEM = "Gia đình: ăn bữa cơm yên; năng lượng sẽ hồi.";
static const char P_FOCUS_FAM_06[] PROGMEM = "Gia đình: ưu tiên chăm sóc người lớn/trẻ nhỏ; phúc ở đây.";
static const char P_FOCUS_FAM_07[] PROGMEM = "Gia đình: chia việc rõ; mỗi người một phần cho nhẹ.";
static const char P_FOCUS_FAM_08[] PROGMEM = "Gia đình: việc tiền bạc nên minh bạch để tránh hiểu lầm.";
static const char P_FOCUS_FAM_09[] PROGMEM = "Gia đình: dành thời gian lắng nghe; nói ít mà hiểu nhiều.";
static const char P_FOCUS_FAM_10[] PROGMEM = "Gia đình: sửa đồ nhỏ, gia cố hậu cần; bền lâu sẽ lợi.";
static const char P_FOCUS_FAM_11[] PROGMEM = "Gia đình: nhường một câu để yên một nhà.";
static const char P_FOCUS_FAM_12[] PROGMEM = "Gia đình: chọn lời tử tế; phúc khí dễ tụ.";
static const char P_FOCUS_FAM_13[] PROGMEM = "Gia đình: chăm góc học tập/làm việc; gọn thì tâm yên.";
static const char P_FOCUS_FAM_14[] PROGMEM = "Gia đình: tránh quyết định nóng; bàn kỹ rồi hãy chốt.";
static const char P_FOCUS_FAM_15[] PROGMEM = "Gia đình: hợp sắp xếp lại lịch sinh hoạt; ổn định là gốc.";
static const char P_FOCUS_FAM_16[] PROGMEM = "Gia đình: quan tâm sức khỏe nhau; phòng hơn chữa.";
static const char P_FOCUS_FAM_17[] PROGMEM = "Gia đình: hợp làm việc nội bộ; xây nền thì phúc bền.";
static const char P_FOCUS_FAM_18[] PROGMEM = "Gia đình: tránh tranh luận dài; chốt ý ngắn gọn.";
static const char P_FOCUS_FAM_19[] PROGMEM = "Gia đình: giữ ấm lòng; vận tốt thường đến từ nhà yên.";

static const char* const PHRASES_FOCUS_FAMILY[] PROGMEM = {
  P_FOCUS_FAM_00,P_FOCUS_FAM_01,P_FOCUS_FAM_02,P_FOCUS_FAM_03,P_FOCUS_FAM_04,
  P_FOCUS_FAM_05,P_FOCUS_FAM_06,P_FOCUS_FAM_07,P_FOCUS_FAM_08,P_FOCUS_FAM_09,
  P_FOCUS_FAM_10,P_FOCUS_FAM_11,P_FOCUS_FAM_12,P_FOCUS_FAM_13,P_FOCUS_FAM_14,
  P_FOCUS_FAM_15,P_FOCUS_FAM_16,P_FOCUS_FAM_17,P_FOCUS_FAM_18,P_FOCUS_FAM_19
};
static const uint16_t PHRASES_FOCUS_FAMILY_COUNT = sizeof(PHRASES_FOCUS_FAMILY) / sizeof(PHRASES_FOCUS_FAMILY[0]);
