#pragma once
#include <Arduino.h>
#include <pgmspace.h>

namespace wxv::lunar_luck_nen_tranh
{
static const char *const kNenLamMoc[] PROGMEM = {
    "Khởi động việc mới, kết nối hợp tác, phác thảo kế hoạch",
    "Mở đầu gọn gàng, chốt hướng đi, tạo đà cho việc mới",
    "Gặp người phù hợp, gieo ý tưởng, bắt tay việc quan trọng",
    "Dọn đường cho kế hoạch, ưu tiên bước đầu rõ ràng",
    "Thuận thời khai mở, gieo hạt hôm nay gặt quả mai sau",
    "Hợp khởi sự hanh thông, việc nhỏ thành lớn",
    "Mở lối mới, dốc lòng vun trồng nền tảng",
    "Thuận gió xuôi chèo, mạnh dạn triển khai ý tưởng",
    "Gieo nhân đúng lúc, dựng nền cho bước tiến dài"};

static const char *const kNenLamKim[] PROGMEM = {
    "Rà soát tài chính, xử lý giấy tờ, chuẩn hóa quy trình",
    "Chốt hồ sơ, kiểm tra số liệu, làm mọi thứ đúng chuẩn",
    "Tối ưu quy trình, sửa lỗi tồn, sắp xếp lại ưu tiên",
    "Gọn sổ sách, rà điều khoản, hoàn thiện thủ tục",
    "Chỉnh lý sổ sách, việc rõ ràng thì lòng an",
    "Giữ nguyên tắc vững vàng, việc đâu vào đó",
    "Lấy kỷ cương làm gốc, lấy rõ ràng làm trọng",
    "Rà soát từng bước, chặt chẽ mới bền lâu",
    "Sửa việc cũ cho ngay, mở đường mới cho sáng"};

static const char *const kNenLamThuy[] PROGMEM = {
    "Giao tiếp, mở rộng quan hệ, xử lý linh hoạt các việc phát sinh",
    "Trao đổi thẳng, kết nối nhanh, tháo gỡ vướng mắc",
    "Đàm phán mềm, lắng nghe nhiều, xoay chuyển tình huống",
    "Mở rộng liên hệ, cập nhật thông tin, phản ứng linh hoạt",
    "Thuận lời nói phải, lòng người dễ mở",
    "Mềm như nước chảy, thấu tình đạt lý",
    "Lắng nghe trước khi quyết, nói ít hiểu nhiều",
    "Kết giao chân thành, việc ắt hanh thông",
    "Dùng lời ôn hòa, hóa giải việc khó"};

static const char *const kNenLamHoa[] PROGMEM = {
    "Thuyết trình, quảng bá, học kỹ năng mới, tạo động lực cho đội nhóm",
    "Đẩy năng lượng, lan tỏa ý tưởng, dẫn dắt tinh thần",
    "Tập trung thể hiện, truyền cảm hứng, thử cách làm mới",
    "Quảng bá nhẹ nhàng, học nhanh một kỹ năng, khơi động lực",
    "Lửa nhỏ cũng đủ sưởi ấm lòng người",
    "Đúng thời phát sáng, việc lớn thành công",
    "Khơi nguồn cảm hứng, dẫn dắt tinh thần",
    "Chủ động tỏa sáng nhưng giữ lòng khiêm",
    "Truyền nhiệt huyết đúng lúc, lan động lực đúng nơi"};

static const char *const kNenLamTho[] PROGMEM = {
    "Ổn định nhịp làm việc, củng cố nền tảng, hoàn thiện kế hoạch dài hạn",
    "Gia cố việc nền, làm chắc từng bước, tính đường dài",
    "Chậm mà chắc, hoàn thiện cấu trúc, dọn các điểm yếu",
    "Củng cố nền tảng, sắp xếp lại trật tự, chốt kế hoạch dài hơi",
    "Chậm mà chắc, từng bước vững vàng",
    "Xây nền kiên cố, gốc rễ bền lâu",
    "Lấy ổn định làm trọng, lấy bền lâu làm mục tiêu",
    "Gia cố nền móng, việc sau mới vững",
    "Lo xa một bước, tránh rối trăm phần"};

static const char *const kNenTranhHigh[] PROGMEM = {
    "Tránh chủ quan, hứa vội, và chi tiêu theo cảm xúc",
    "Tránh quá tự tin, quyết nhanh, và mua sắm bốc đồng",
    "Tránh ôm đồm, nói quá tay, và tiêu tiền theo hứng",
    "Tránh làm quá tốc độ, cam kết vội, và chi tiêu thiếu kiểm soát",
    "Tránh tự mãn khi việc đang thuận",
    "Tránh lời hứa vượt khả năng thực hiện",
    "Tránh tham nhanh mà bỏ gốc",
    "Tránh phô trương quá mức",
    "Tránh nóng vội khi thời vận đang tốt"};

static const char *const kNenLamLow[] PROGMEM = {
    "Ưu tiên việc nhẹ: dọn dẹp, sắp xếp, hoàn thiện việc còn dang dở",
    "Giảm tốc độ: dọn tồn, chỉnh sửa, làm lại cho gọn",
    "Làm việc vừa sức: rà lỗi, dọn backlog, hoàn tất phần còn thiếu",
    "Tập trung việc nhỏ: thu gọn, kiểm tra, hoàn thiện nốt việc dang dở",
    "An tĩnh xử việc nhỏ, chờ thời thuận lợi",
    "Thu mình dưỡng sức, chỉnh lại điều chưa ổn",
    "Làm ít nhưng làm kỹ, sửa sai cho sạch",
    "Dọn việc cũ cho xong, giữ lòng cho nhẹ",
    "Giảm tốc một nhịp, tránh sai một bước"};

static const char *const kNenTranhLowKimHoa[] PROGMEM = {
    "Tránh ký kết lớn, đầu tư mạo hiểm, và xung đột căng thẳng",
    "Tránh quyết định lớn, tranh cãi gay, và liều tài chính",
    "Tránh chốt hợp đồng lớn, đầu tư vội, và căng thẳng đôi co",
    "Tránh va chạm, tránh mạo hiểm tiền bạc, và ký kết quan trọng",
    "Tránh đối đầu trực diện khi chưa đủ lực",
    "Tránh quyết lớn khi vận chưa thông",
    "Tránh dấn thân mạo hiểm vì nóng lòng",
    "Tránh đầu tư khi lòng còn phân vân",
    "Tránh căng thẳng kéo dài không cần thiết"};

static const char *const kNenTranhLowOther[] PROGMEM = {
    "Tránh đi xa, vay mượn, và quyết định gấp",
    "Tránh hứa hẹn lớn, vay mượn, và xử lý vội",
    "Tránh bốc đồng, tránh đi xa, và quyết định khi mệt",
    "Tránh hành động gấp, vay mượn, và thay đổi lớn",
    "Tránh di chuyển xa khi chưa thật cần",
    "Tránh cam kết dài hạn lúc tâm chưa yên",
    "Tránh vội vàng vì áp lực bên ngoài",
    "Tránh mở việc mới khi việc cũ chưa xong",
    "Tránh quyết định khi còn nhiều nghi ngại"};

static const char *const kNenLamMid[] PROGMEM = {
    "Giữ nhịp ổn định, làm việc theo kế hoạch, ưu tiên việc trong tầm kiểm soát",
    "Đi đều: làm theo kế hoạch, kiểm tra tiến độ, giữ nhịp ổn định",
    "Chọn việc chắc tay, làm từng bước, ưu tiên phần quan trọng",
    "Giữ nhịp vừa phải, ưu tiên việc rõ ràng, tránh lan man",
    "Đi từng bước chắc tay, tránh dao động",
    "Lấy ổn định làm trọng, tiến vừa đủ nhịp",
    "Làm việc trong khả năng, tránh quá tầm",
    "Giữ cân bằng giữa tiến và thủ",
    "Thuận theo kế hoạch, hạn chế biến động"};

static const char *const kNenTranhMid[] PROGMEM = {
    "Tránh khởi sự rủi ro cao hoặc quyết khi chưa đủ dữ liệu",
    "Tránh quyết vội, tránh rủi ro lớn, và đừng thiếu dữ liệu",
    "Tránh mở việc khó khi chưa sẵn sàng và chưa đủ thông tin",
    "Tránh liều lĩnh, tránh đoán mò, và đừng chốt khi còn thiếu dữ liệu",
    "Tránh thay đổi chiến lược đột ngột",
    "Tránh tin lời đồn chưa kiểm chứng",
    "Tránh làm việc theo cảm tính",
    "Tránh mở rộng khi nền chưa vững",
    "Tránh quyết nhanh khi chưa cân nhắc đủ"};
} // namespace wxv::lunar_luck_nen_tranh
