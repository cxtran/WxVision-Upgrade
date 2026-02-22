// fortune_phrases_element.h
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// =========================
// ELEMENT PHRASES (PROGMEM)
// Buckets by dayInfo.element (normalize as needed):
//   "Moc"  -> Mộc
//   "Hoa"  -> Hỏa
//   "Tho"  -> Thổ
//   "Kim"  -> Kim
//   "Thuy" -> Thủy
//
// These phrases are meant to be combined into the contextual headline,
// e.g. [ScorePhrase] + [ElementPhrase] + [PhasePhrase] + [Optional Flags].
// =========================


// ---------- MỘC (20) ----------
static const char P_ELEM_MOC_00[] PROGMEM = "Khí Mộc sinh trưởng; hợp khởi tạo và gieo nền tảng.";
static const char P_ELEM_MOC_01[] PROGMEM = "Ngày Mộc thiên về phát triển; nên nuôi dưỡng mục tiêu dài hạn.";
static const char P_ELEM_MOC_02[] PROGMEM = "Mộc khí thuận cho học tập, nâng kỹ năng và mở rộng kiến thức.";
static const char P_ELEM_MOC_03[] PROGMEM = "Mộc vượng; hợp kết nối, mở rộng quan hệ theo hướng bền.";
static const char P_ELEM_MOC_04[] PROGMEM = "Ngày Mộc, hợp bắt đầu việc mới nhỏ để tạo đà.";
static const char P_ELEM_MOC_05[] PROGMEM = "Khí Mộc mềm dẻo; nói chuyện khéo sẽ dễ thông.";
static const char P_ELEM_MOC_06[] PROGMEM = "Mộc thiên về sinh sôi; hợp cải tiến quy trình từng bước.";
static const char P_ELEM_MOC_07[] PROGMEM = "Ngày Mộc, nên ưu tiên việc “trồng cây”: làm nền, tích lũy.";
static const char P_ELEM_MOC_08[] PROGMEM = "Mộc khí giúp ý tưởng nảy nở; ghi lại và chọn cái khả thi.";
static const char P_ELEM_MOC_09[] PROGMEM = "Hợp sắp xếp kế hoạch phát triển, tránh nóng vội đốt giai đoạn.";
static const char P_ELEM_MOC_10[] PROGMEM = "Mộc thuận cho hợp tác; chọn người cùng nhịp sẽ lợi.";
static const char P_ELEM_MOC_11[] PROGMEM = "Ngày Mộc, hợp mở đường bằng bước nhỏ nhưng đều.";
static const char P_ELEM_MOC_12[] PROGMEM = "Khí Mộc tăng trưởng; làm việc có kỷ luật sẽ ra kết quả.";
static const char P_ELEM_MOC_13[] PROGMEM = "Mộc thiên về hướng ngoại; hợp gặp gỡ, trao đổi ý tưởng.";
static const char P_ELEM_MOC_14[] PROGMEM = "Ngày Mộc, nên đầu tư thời gian cho học và rèn kỹ năng.";
static const char P_ELEM_MOC_15[] PROGMEM = "Khí Mộc hợp làm việc sáng tạo, thiết kế và tối ưu.";
static const char P_ELEM_MOC_16[] PROGMEM = "Mộc khí khuyến khích mở rộng; nhưng vẫn cần kế hoạch rõ.";
static const char P_ELEM_MOC_17[] PROGMEM = "Ngày Mộc, hợp chữa “gốc rễ”: xử lý nguyên nhân thay vì triệu chứng.";
static const char P_ELEM_MOC_18[] PROGMEM = "Mộc thuận; ưu tiên việc liên quan kế hoạch, phát triển, nâng cấp.";
static const char P_ELEM_MOC_19[] PROGMEM = "Khí Mộc giúp hồi phục; sắp xếp lại nhịp làm việc sẽ lợi.";

static const char* const PHRASES_ELEM_MOC[] PROGMEM = {
  P_ELEM_MOC_00,P_ELEM_MOC_01,P_ELEM_MOC_02,P_ELEM_MOC_03,P_ELEM_MOC_04,
  P_ELEM_MOC_05,P_ELEM_MOC_06,P_ELEM_MOC_07,P_ELEM_MOC_08,P_ELEM_MOC_09,
  P_ELEM_MOC_10,P_ELEM_MOC_11,P_ELEM_MOC_12,P_ELEM_MOC_13,P_ELEM_MOC_14,
  P_ELEM_MOC_15,P_ELEM_MOC_16,P_ELEM_MOC_17,P_ELEM_MOC_18,P_ELEM_MOC_19
};
static const uint16_t PHRASES_ELEM_MOC_COUNT = sizeof(PHRASES_ELEM_MOC) / sizeof(PHRASES_ELEM_MOC[0]);


// ---------- HỎA (20) ----------
static const char P_ELEM_HOA_00[] PROGMEM = "Khí Hỏa mạnh; hợp hành động nhanh nhưng phải rõ mục tiêu.";
static const char P_ELEM_HOA_01[] PROGMEM = "Ngày Hỏa, dễ bốc; giữ bình tĩnh sẽ tránh sai lầm.";
static const char P_ELEM_HOA_02[] PROGMEM = "Hỏa khí hợp truyền thông, thuyết trình và quảng bá.";
static const char P_ELEM_HOA_03[] PROGMEM = "Ngày Hỏa, hợp khởi động việc cần năng lượng và quyết đoán.";
static const char P_ELEM_HOA_04[] PROGMEM = "Hỏa vượng; nên ưu tiên việc cần “đẩy” tiến độ.";
static const char P_ELEM_HOA_05[] PROGMEM = "Khí Hỏa sáng; hợp làm việc trước đám đông, gặp gỡ.";
static const char P_ELEM_HOA_06[] PROGMEM = "Ngày Hỏa, dễ nóng; tránh tranh luận căng thẳng.";
static const char P_ELEM_HOA_07[] PROGMEM = "Hỏa khí thúc đẩy; hợp chốt việc nhanh sau khi kiểm tra kỹ.";
static const char P_ELEM_HOA_08[] PROGMEM = "Ngày Hỏa, nên đặt giới hạn rõ để tránh quá đà.";
static const char P_ELEM_HOA_09[] PROGMEM = "Khí Hỏa hợp học kỹ năng mới, rèn phản xạ và tự tin.";
static const char P_ELEM_HOA_10[] PROGMEM = "Hỏa thiên về bứt tốc; chia nhỏ việc để kiểm soát rủi ro.";
static const char P_ELEM_HOA_11[] PROGMEM = "Ngày Hỏa, hợp kích hoạt động lực đội nhóm.";
static const char P_ELEM_HOA_12[] PROGMEM = "Hỏa khí giúp lan tỏa; nhưng cần tránh hứa vội.";
static const char P_ELEM_HOA_13[] PROGMEM = "Ngày Hỏa, hợp xử lý việc cần quyết định dứt khoát.";
static const char P_ELEM_HOA_14[] PROGMEM = "Khí Hỏa tăng; ưu tiên việc quan trọng nhất trước.";
static const char P_ELEM_HOA_15[] PROGMEM = "Ngày Hỏa, nên dùng lời mềm để giảm xung đột.";
static const char P_ELEM_HOA_16[] PROGMEM = "Hỏa thuận cho sáng tạo, nội dung và trình bày.";
static const char P_ELEM_HOA_17[] PROGMEM = "Ngày Hỏa, hợp hành động; nhưng tránh “đốt” quá nhiều việc.";
static const char P_ELEM_HOA_18[] PROGMEM = "Khí Hỏa dễ kích cảm xúc; quyết theo dữ liệu sẽ tốt.";
static const char P_ELEM_HOA_19[] PROGMEM = "Ngày Hỏa, hợp tạo đà; xong việc mới mở việc.";

static const char* const PHRASES_ELEM_HOA[] PROGMEM = {
  P_ELEM_HOA_00,P_ELEM_HOA_01,P_ELEM_HOA_02,P_ELEM_HOA_03,P_ELEM_HOA_04,
  P_ELEM_HOA_05,P_ELEM_HOA_06,P_ELEM_HOA_07,P_ELEM_HOA_08,P_ELEM_HOA_09,
  P_ELEM_HOA_10,P_ELEM_HOA_11,P_ELEM_HOA_12,P_ELEM_HOA_13,P_ELEM_HOA_14,
  P_ELEM_HOA_15,P_ELEM_HOA_16,P_ELEM_HOA_17,P_ELEM_HOA_18,P_ELEM_HOA_19
};
static const uint16_t PHRASES_ELEM_HOA_COUNT = sizeof(PHRASES_ELEM_HOA) / sizeof(PHRASES_ELEM_HOA[0]);


// ---------- THỔ (20) ----------
static const char P_ELEM_THO_00[] PROGMEM = "Khí Thổ ổn định; hợp củng cố nền tảng và kỷ luật.";
static const char P_ELEM_THO_01[] PROGMEM = "Ngày Thổ, nên ưu tiên việc bền vững hơn “đánh nhanh”.";
static const char P_ELEM_THO_02[] PROGMEM = "Thổ khí hợp chuẩn hóa, hoàn thiện quy trình và kiểm soát.";
static const char P_ELEM_THO_03[] PROGMEM = "Ngày Thổ, hợp xử lý việc tồn, sắp xếp lại hệ thống.";
static const char P_ELEM_THO_04[] PROGMEM = "Khí Thổ thiên về chắc chắn; chốt từng bước sẽ lợi.";
static const char P_ELEM_THO_05[] PROGMEM = "Ngày Thổ, hợp tổng hợp dữ liệu và đưa ra kết luận thực tế.";
static const char P_ELEM_THO_06[] PROGMEM = "Thổ khí giúp ổn định tài chính; ưu tiên cân đối thu chi.";
static const char P_ELEM_THO_07[] PROGMEM = "Ngày Thổ, nên làm việc theo checklist để tránh sót.";
static const char P_ELEM_THO_08[] PROGMEM = "Khí Thổ hợp giải quyết việc cần bền, cần chắc.";
static const char P_ELEM_THO_09[] PROGMEM = "Ngày Thổ, hợp xây dựng thói quen và lịch trình ổn định.";
static const char P_ELEM_THO_10[] PROGMEM = "Thổ thiên về nội lực; làm kỹ phần nền sẽ ra quả.";
static const char P_ELEM_THO_11[] PROGMEM = "Ngày Thổ, hợp rà soát hợp đồng, điều khoản và rủi ro.";
static const char P_ELEM_THO_12[] PROGMEM = "Khí Thổ chậm mà chắc; đừng vội thay đổi hướng.";
static const char P_ELEM_THO_13[] PROGMEM = "Ngày Thổ, hợp củng cố quan hệ lâu dài hơn giao dịch nhanh.";
static const char P_ELEM_THO_14[] PROGMEM = "Thổ khí hợp dọn dẹp, sắp xếp và hoàn thiện hồ sơ.";
static const char P_ELEM_THO_15[] PROGMEM = "Ngày Thổ, ưu tiên việc có thể đo lường và kiểm chứng.";
static const char P_ELEM_THO_16[] PROGMEM = "Khí Thổ giúp giảm dao động; giữ kỷ luật sẽ lợi.";
static const char P_ELEM_THO_17[] PROGMEM = "Ngày Thổ, hợp chuẩn bị phương án dự phòng và hậu cần.";
static const char P_ELEM_THO_18[] PROGMEM = "Thổ thuận cho việc “gia cố”: sửa, nâng, chuẩn hóa.";
static const char P_ELEM_THO_19[] PROGMEM = "Ngày Thổ, hợp kết thúc việc dang dở để nhẹ đầu.";

static const char* const PHRASES_ELEM_THO[] PROGMEM = {
  P_ELEM_THO_00,P_ELEM_THO_01,P_ELEM_THO_02,P_ELEM_THO_03,P_ELEM_THO_04,
  P_ELEM_THO_05,P_ELEM_THO_06,P_ELEM_THO_07,P_ELEM_THO_08,P_ELEM_THO_09,
  P_ELEM_THO_10,P_ELEM_THO_11,P_ELEM_THO_12,P_ELEM_THO_13,P_ELEM_THO_14,
  P_ELEM_THO_15,P_ELEM_THO_16,P_ELEM_THO_17,P_ELEM_THO_18,P_ELEM_THO_19
};
static const uint16_t PHRASES_ELEM_THO_COUNT = sizeof(PHRASES_ELEM_THO) / sizeof(PHRASES_ELEM_THO[0]);


// ---------- KIM (20) ----------
static const char P_ELEM_KIM_00[] PROGMEM = "Khí Kim thiên về nguyên tắc; hợp rà soát và chốt tiêu chuẩn.";
static const char P_ELEM_KIM_01[] PROGMEM = "Ngày Kim, hợp xử lý giấy tờ, pháp lý và điều khoản.";
static const char P_ELEM_KIM_02[] PROGMEM = "Kim khí sắc; nói thẳng quá dễ gây căng, nên mềm lại.";
static const char P_ELEM_KIM_03[] PROGMEM = "Ngày Kim, hợp chuẩn hóa quy trình và kiểm soát chất lượng.";
static const char P_ELEM_KIM_04[] PROGMEM = "Kim thuận cho quyết định dựa trên số liệu, tránh cảm tính.";
static const char P_ELEM_KIM_05[] PROGMEM = "Khí Kim hợp cắt bỏ phần thừa, tối ưu và gọn hóa.";
static const char P_ELEM_KIM_06[] PROGMEM = "Ngày Kim, nên kiểm tra kỹ chi tiết trước khi ký.";
static const char P_ELEM_KIM_07[] PROGMEM = "Kim khí hợp chốt hạng mục; đặt chuẩn rõ sẽ dễ làm.";
static const char P_ELEM_KIM_08[] PROGMEM = "Ngày Kim, hợp dọn backlog và hoàn tất việc ngắn hạn.";
static const char P_ELEM_KIM_09[] PROGMEM = "Khí Kim nghiêm; kỷ luật tốt sẽ sinh hiệu quả.";
static const char P_ELEM_KIM_10[] PROGMEM = "Ngày Kim, hợp rà soát tài chính và siết chi tiêu.";
static const char P_ELEM_KIM_11[] PROGMEM = "Kim thuận cho sửa lỗi, audit, kiểm định và đối soát.";
static const char P_ELEM_KIM_12[] PROGMEM = "Ngày Kim, hợp thương lượng theo nguyên tắc rõ ràng.";
static const char P_ELEM_KIM_13[] PROGMEM = "Khí Kim giúp quyết đoán; nhưng tránh cứng quá.";
static const char P_ELEM_KIM_14[] PROGMEM = "Ngày Kim, nên ưu tiên việc “đóng” hơn việc “mở”.";
static const char P_ELEM_KIM_15[] PROGMEM = "Kim khí hợp làm việc cần chính xác, đo lường.";
static const char P_ELEM_KIM_16[] PROGMEM = "Ngày Kim, hợp chốt mục tiêu và mốc thời gian cụ thể.";
static const char P_ELEM_KIM_17[] PROGMEM = "Khí Kim hợp tối ưu tài nguyên; cắt lãng phí sẽ lợi.";
static const char P_ELEM_KIM_18[] PROGMEM = "Ngày Kim, hợp giải quyết việc còn vướng về thủ tục.";
static const char P_ELEM_KIM_19[] PROGMEM = "Kim thuận; làm theo checklist sẽ tránh sai sót.";

static const char* const PHRASES_ELEM_KIM[] PROGMEM = {
  P_ELEM_KIM_00,P_ELEM_KIM_01,P_ELEM_KIM_02,P_ELEM_KIM_03,P_ELEM_KIM_04,
  P_ELEM_KIM_05,P_ELEM_KIM_06,P_ELEM_KIM_07,P_ELEM_KIM_08,P_ELEM_KIM_09,
  P_ELEM_KIM_10,P_ELEM_KIM_11,P_ELEM_KIM_12,P_ELEM_KIM_13,P_ELEM_KIM_14,
  P_ELEM_KIM_15,P_ELEM_KIM_16,P_ELEM_KIM_17,P_ELEM_KIM_18,P_ELEM_KIM_19
};
static const uint16_t PHRASES_ELEM_KIM_COUNT = sizeof(PHRASES_ELEM_KIM) / sizeof(PHRASES_ELEM_KIM[0]);


// ---------- THỦY (20) ----------
static const char P_ELEM_THUY_00[] PROGMEM = "Khí Thủy linh hoạt; hợp giao tiếp và điều phối khéo.";
static const char P_ELEM_THUY_01[] PROGMEM = "Ngày Thủy, hợp đàm phán; mềm dẻo nhưng rõ ràng.";
static const char P_ELEM_THUY_02[] PROGMEM = "Thủy khí thuận xử lý việc phát sinh; ưu tiên thích ứng.";
static const char P_ELEM_THUY_03[] PROGMEM = "Ngày Thủy, hợp kết nối quan hệ và trao đổi thông tin.";
static const char P_ELEM_THUY_04[] PROGMEM = "Khí Thủy giúp “chuyển hướng”; làm gọn và linh hoạt.";
static const char P_ELEM_THUY_05[] PROGMEM = "Ngày Thủy, nên lắng nghe kỹ trước khi trả lời.";
static const char P_ELEM_THUY_06[] PROGMEM = "Thủy thuận cho sáng tạo mềm: viết, ý tưởng, cải tiến nhỏ.";
static const char P_ELEM_THUY_07[] PROGMEM = "Ngày Thủy, hợp làm việc cần nhịp nhàng và phối hợp.";
static const char P_ELEM_THUY_08[] PROGMEM = "Khí Thủy dễ dao động; giữ kế hoạch sẽ tránh rối.";
static const char P_ELEM_THUY_09[] PROGMEM = "Ngày Thủy, hợp xử lý công việc linh hoạt, tránh cứng nhắc.";
static const char P_ELEM_THUY_10[] PROGMEM = "Thủy khí hợp kết nối khách hàng, đối tác, người thân.";
static const char P_ELEM_THUY_11[] PROGMEM = "Ngày Thủy, ưu tiên thông tin rõ; tránh suy đoán.";
static const char P_ELEM_THUY_12[] PROGMEM = "Khí Thủy thuận cho học hỏi; tiếp thu nhanh nếu tập trung.";
static const char P_ELEM_THUY_13[] PROGMEM = "Ngày Thủy, hợp tối ưu luồng công việc và phối hợp nhóm.";
static const char P_ELEM_THUY_14[] PROGMEM = "Thủy khí giúp mềm hóa xung đột; nói nhẹ sẽ lợi.";
static const char P_ELEM_THUY_15[] PROGMEM = "Ngày Thủy, hợp xử lý việc cần thương lượng và điều chỉnh.";
static const char P_ELEM_THUY_16[] PROGMEM = "Khí Thủy hợp “dọn kênh”: làm rõ quy trình giao tiếp.";
static const char P_ELEM_THUY_17[] PROGMEM = "Ngày Thủy, hợp xử lý việc dang dở theo hướng linh hoạt.";
static const char P_ELEM_THUY_18[] PROGMEM = "Thủy thuận; làm việc theo nhịp, tránh hấp tấp.";
static const char P_ELEM_THUY_19[] PROGMEM = "Ngày Thủy, hợp kiểm tra thông tin trước khi chốt quyết.";

static const char* const PHRASES_ELEM_THUY[] PROGMEM = {
  P_ELEM_THUY_00,P_ELEM_THUY_01,P_ELEM_THUY_02,P_ELEM_THUY_03,P_ELEM_THUY_04,
  P_ELEM_THUY_05,P_ELEM_THUY_06,P_ELEM_THUY_07,P_ELEM_THUY_08,P_ELEM_THUY_09,
  P_ELEM_THUY_10,P_ELEM_THUY_11,P_ELEM_THUY_12,P_ELEM_THUY_13,P_ELEM_THUY_14,
  P_ELEM_THUY_15,P_ELEM_THUY_16,P_ELEM_THUY_17,P_ELEM_THUY_18,P_ELEM_THUY_19
};
static const uint16_t PHRASES_ELEM_THUY_COUNT = sizeof(PHRASES_ELEM_THUY) / sizeof(PHRASES_ELEM_THUY[0]);
