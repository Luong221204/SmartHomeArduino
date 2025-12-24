Các thiết bị sử dụng là:
-2 bóng đèn
- máy bơm
- quạt
- servor để làm cửa
- còi
- cảm biến lửa khói mưa
- cảm biên dht 11
luồng của hệ thống:
*trong hàm loop:
- sẽ có 1 biến là "statuscheckpass" để phát hiện xem người dùng có đang nhập bàn phím ko , nếu có sẽ dừng toàn bộ các http req để tập trung lắng nghe keypad vì các http req là tác vụ mất rất nhiều thời gian nên cần làm vậy
- nếu statuscheckpass = false thì thưc hiện các http req bao gồm : phương thức get fetchStatus để lấy trạng thái các thiết bị :bật và tắt  hàm getSensorLevels để lấy giá trị mặc định của các cảm biến
- cứ 1 phút hệ thống sẽ lấy mật khẩu là server qua getPasswordFromAPI() và lưu nó vào bộ nhớ EFROOM của esp loadPasswordFromEEPROM() đây là hàm lấy mật khẩu
- cứ 1 phút sẽ gửi nhiệt độ và độ ẩm lên server qua hàm sendTempHumid() và gửi các giá trị từ chân analog của các cảm biến lên server
      sendLevel("update-fs/data", flameLevel);
      sendLevel("update-gs/data", gasLevel);
      sendLevel("update-rs/data", rainLevel);
- hệ thống sẽ lắng nghe liên tục nếu có cháy, khói thì sẽ còi sẽ kêu và gửi thông báo cho server qua hàm postEmergency() 
