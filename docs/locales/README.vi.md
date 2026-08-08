<p align="center">
  <a href="https://luxdmx.org/video"><img src="docs/mock.png" alt="LuxDMX V2 — cổng truyền thống Art-Net / sACN sang DMX512 với giao diện web chẩn đoán trực tuyến" width="100%"></a>
</p>

<p align="center">
  <img src="docs/logo.png" alt="Logo LuxDMX V2" width="120">
</p>

<h1 align="center">LuxDMX V2</h1>

<p align="center">
  <b>Nguồn mở Art-Net / sACN (E1.31) &rarr; cổng truyền thức DMX512 cho ESP32 / ESP32-S3 / Ethernet.</b>
</p>

<p align="center">
  Không chỉ là một núi Art-Net — đây là một núi truyền thức mạng <i>và</i> một công cụ chẩn đoán trực tuyến.<br>
  Xem tất cả 512 kênh cập nhật theo thời gian thực trong trình duyệt của bạn, nhận cảnh báo ngay lập tức
  <b>khi hai bảng điều khiển tranh giành quyền một vũ trụ</b>, xem FPS và độ lệch khung thời gian của từng
  nguồn, và điều khiển ra DMX<b> cách điện hoàn toàn</b>. Có thể lắp đặt với vài đồng.
</p>

<p align="center">
  <a href="https://tombueng.github.io/LuxDMX/"><img alt="Flash trong trình duyệt" src="https://img.shields.io/badge/flash%20in-browser-2dd4bf"></a>
  <a href="https://github.com/tombueng/LuxDMX/actions/workflows/build.yml"><img alt="Build" src="https://github.com/tombueng/LuxDMX/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/tombueng/LuxDMX/releases"><img alt="Firmware mới nhất" src="https://img.shields.io/github/v/tag/tombueng/LuxDMX?filter=v*&sort=semver&label=firmware"></a>
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="Version" src="https://img.shields.io/badge/version-V2.0%20modular-blue">
  <img alt="Status: stable" src="https://img.shields.io/badge/status-stable-green">
</p>

<p align="center">
  <a href="https://tombueng.github.io/LuxDMX/"><b>⚡ Flash từ trình duyệt</b></a> &nbsp;·&nbsp;
  <a href="https://luxdmx.org/video"><b>▶ Xem video demo</b></a> &nbsp;·&nbsp;
  <a href="hardware/README.md"><b>🛠 PCB tùy chỉnh</b></a> &nbsp;·&nbsp;
  <a href="#-cài-%C4%91%E1%BA%AFt-phi%C3%AAn-b%E1%BA%BFn-%C4%91%C3%A3-xay-d%E1%BB%B1ng">Cài đặt</a>
</p>

| Trang trạng thái | Trang cài đặt |
|---|---|
| ![Trang trạng thái](docs/screenshot-status.png) | ![Trang cài đặt](docs/screenshot-config.png) |

---

## 🎯 Điều mới trong V2 (Kiến trúc mô-đun)

V2 là một bản **rewrite hoàn toàn mô-đun** của firmware gốc độc lập. Mã nguồn đã được chia thành một kiến trúc 5 lớp sạch sẽ và có khả năng kiểm thử để các tính năng có thể phát triển độc lập mà không làm bất ổn cho đường truyền DMX lõi.

### Các cải tiến V2 nổi bật

| Tính năng V2 | Nội dung thay đổi |
|---|---|
| **Kiến trúc 5 lớp mô-đun** | Firmware được chia thành `drv` &rarr; `cfg` &rarr; `core` &rarr; `net` &rarr; `app/sys`, kết nối bởi một `main.cpp` mỏng. Mỗi lớp sở hữu một mối quan tâm riêng và có thể được kiểm thử độc lập. |
| **Đầu ra DMX512 dựa trên RMT** | DMX được đưa ra thông qua **phụ kiện RMT** (issue #64), không phải UART. Điều này bất khả xâm phạm trước sự cạnh tranh DMA của core-0 mạng — cùng một lỗi làm hỏng break dưới tải WiFi nặng. |
| **Lên đến 4 kênh ra DMX** | Mở rộng từ 2 lên **4 vũ trụ độc lập** (các kênh RMT 0&ndash;3). Các kênh A & B hỗ trợ RDM; C & D chỉ là DMX. |
| **Hỗ trợ PSRAM** | Bản dựng tùy chọn PSRAM 8MB octal (`esp32s3_psram`) di chuyển bộ đệm WiFi/lwIP và bảng RDM sang RAM bên ngoài, giải phóng ~150KB DRAM nội bộ. |
| **Bảng mạch 4 vũ trụ** | Môi trường `esp32s3_n16r8_eth` chuyên dụng cho bảng mạch **LuxDMX-4uni** (ESP32-S3-WROOM-2 N16R8 + W5500). |
| **Bộ đệm seqlock** | Một seqlock single-writer/single-reader (`include/seqlock.h`) bảo vệ bộ đệm khung DMX giữa task nhận core-0 và task truyền core-1 — các đọc bị rách nhau được phát hiện và bỏ qua, không bao giờ được truyền. |
| **Khởi tạo đầu ra an toàn khi lỗi** | Một chuỗi khởi tạo được bảo vệ với đếm số lỗi NVS tiến triển việc vô hiệu hóa các kênh nếu khởi tạo gặp panic, để một chân lỗi không thể brick thiết bị. |
| **Lưu cấu hình trực tiếp** | Hầu hết cài đặt áp dụng ngay lập tức mà không cần khởi động lại (vũ trụ, chế độ hòa trộn, tốc độ TX, chính sách mất tín hiệu, độ sáng). Chỉ có cài đặt GPIO/driver mới yêu cầu khởi động lại. |
| **Cấu hình dựa trên schema** | Mỗi cài đặt được lưu trữ được mô tả một lần trong `src/cfg/config_schema.cpp`; bảng này điều khiển NVS tải/lưu, form web `/config`, console serial, và engine di chuyển. Giá trị mặc định nằm trong `templates/*.ini`, không phải macro `-D`. |
| **Phong cách truyền delta** | Phong cách TX cho từng kênh: **Continues** (chạy tự do ở tần suất khung đã cấu hình) hoặc **Delta** (một khung DMX cho mỗi gói tin nhận được). Cả hai áp dụng trực tiếp qua giao diện web. |
| **Chế độ ra đầu có thể cấu hình** | Mỗi kênh có thể được đặt thành **DMX-only** (tự động hướng RS485) hoặc **RDM full** (GPIO DE/RE + UART RX). Đặt chân RTS tự động bật chế độ RDM. |
| **Nguồn phong cách truyền** | Theo dõi xem phong cách TX được đặt cục bộ (web UI / serial) hay bởi một bảng điều khiển (Art-Net), để bảng điều khiển có thể đẩy một phong cách và thấy phản ánh. |
| **PID RDM mở rộng** | Tập hợp đầy đủ PID E1.20 có kiểu: DEVICE_MODE, DEVICE_MODES, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_DEFINITION/VALUE/RECORD, STATUS_MESSAGE. |
| **Liệt kê thiết bị phụ** | Truy vấn số lượng thiết bị phụ thông qua DEVICE_INFO; giới hạn thiết bị RDM tùy chọn (mặc định 8). |
| **Khám phá vũ trụ sACN + Stream Sync** | Nhận gói tin Khám phá vũ trụ sACN; tôn trọng Stream Sync cho từng kênh với thời gian chờ 500ms — các khung được lưu trữ sẽ được chuyển tiếp chỉ sau khi hết thời gian chờ đồng bộ. |
| **Giám sát kiểm tra kéo dài** | Cờ build `LUXDMX_SOAK_TEST` bật watchdog bộ nhớ 60 giây, ghi nhận DRAM/PSRAM mỗi phút và khởi động lại nếu DRAM còại lại dưới 30KB. Được phơi bày qua `/diag/soak-stats`. |
| **OTA có chữ ký Ed25519** | Hình ảnh firmware phiên bản được ký Ed25519; build nhúng khóa công 32 byte và xác minh chữ ký 64 byte trước khi cam kết cập nhật (`OTA_SIGN_ENABLED` cho sản phẩm; build dev bỏ qua xác minh). |
| **Chính sách hàng đợi nền** | Mức độ nghiêm trọng thu thập trạng thái ArtPoll có thể cấu hình (vô hiệu / lời khuyên / cảnh báo / lỗi) qua `artnetBridgeDispatch` — điều chỉnh tần suất thiết bị báo cáo trạng thái nền cho bảng điều khiển. |

---

## ✨ Tính năng

### Giao thức & DMX

| Tính năng | Chi tiết |
|---|---|
| **Art-Net &rarr; DMX512** | Đầy đủ 512 kênh, unicast hoặc broadcast, vũ trụ có thể cấu hình (0–15) |
| **sACN / E1.31 &rarr; DMX512** | Nhận multicast, vũ trụ có thể cấu hình, chạy cùng lúc với Art-Net |
| **Chọn giao thức** | Art-Net only / sACN only / Cả hai — có thể cấu hình trong giao diện web |
| **Hòa trộn nguồn** | Từng kênh: HTP (giá trị cao nhất được ưu tiên) / LTP (mới nhất thắng) / tắt, tôn trọng trường ưu tiên sACN |
| **Thống kê độ lệch** | Độ lệch thời gian gửi khung theo thời gian thực (EMA) |
| **Nhật ký thay đổi** | Nhật ký thời gian thực của các giá trị DMX thay đổi với N kênh thay đổi nhiều nhất mỗi khung |
| **Chính sách mất tín hiệu** | Từng kênh: giữ khung cuối cùng (mặc định), đèn tắt, hoặc dừng gửi. Làm mới 40Hz liên tục vượt qua khoảng thời gian đầu vào ngắn. |
| **Tốc độ & phong cách truyền ra** | Từng kênh: tốc độ (20 / 25 / 33.3 / 40 / 41.7 fps) và phong cách (Continues hoặc Delta). Cả hai áp dụng trực tiếp qua giao diện web. |
| **Lên đến 4 kênh ra DMX** | Lên đến 4 vũ trụ độc lập; A+B hỗ trợ RDM, C+D chỉ là DMX |
| **RDM (E1.20)** | Khám phá DISC_UNIQUE_BRANCH, GET/SET DEVICE_INFO / địa chỉ bắt đầu / identify / cảm biến / personality / thông điệp trạng thái trên kênh hỗ trợ RDM (cần chân DE/RE) |
| **PID RDM mở rộng** | DEVICE_MODE, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_RECORD — tương thích đầy đủ với bảng điều khiển |
| **Liệt kê thiết bị phụ** | Truy vấn số lượng thiết bị phụ qua DEVICE_INFO; giới hạn thiết bị RDM tùy chọn (mặc định 8) |
| **RDM qua Art-Net** | Cổng RDM ra Art-Net 4 đầy đủ (ArtPoll / ArtTodRequest / ArtTodControl / ArtRdm). Khám phá được lên lịch một giao dịch trên mỗi khung DMX — RDM không bao giờ làm treo đầu ra DMX. |
| **Điều khiển DMX thủ công** | Đặt bất kỳ kênh nào từ trình duyệt thông qua thanh trượt |
| **Nút đèn tắt** | Đặt tất cả kênh về 0 ngay lập tức từ trình duyệt |
| **Chuyển đổi Art-Net / Thủ công** | Chuyển đổi giữa passthrough giao thức và ghi đè thủ công |
| **Cấu hình IP từ xa (ArtIpProg)** | Bảng điều khiển có thể đọc/đặt IP/mặt nạ/gateway hoặc chuyển sang DHCP qua Art-Net `ArtIpProg`. Tắt mặc định (Art-Net không có xác thực). |
| **Nguồn phong cách truyền** | Theo dõi xem phong cách TX được đặt cục bộ (web UI / serial) hay bởi bảng điều khiển (Art-Net) — bảng điều khiển có thể đẩy một phong cách và thấy phản ánh. |
| **Chế độ ra đầu có thể cấu hình** | Mỗi kênh có thể được đặt thành DMX-only (tự động hướng) hoặc RDM full (GPIO DE/RE + UART RX). Đặt chân RTS tự động bật RDM. |

### Mạng & Kết nối

| Tính năng | Chi tiết |
|---|---|
| **Giao diện Web sống** | Theme Bootstrap 5 tối, WebSocket đẩy (~10/s), tất cả 512 kênh đều hiển thị |
| **Danh sách nguồn** | Hiển thị tất cả nguồn Art-Net / sACN đang hoạt động với FPS từng nguồn |
| **Phát hiện xung đột** | Thanh cảnh báo khi nhiều nguồn hoạt động đồng thời |
| **Đường sparkline** | Lịch sử kênh trong modal chi tiết |
| **Nhãn kênh** | Đặt tên bất kỳ kênh nào — hiển thị trong lưới, modal, và nhật ký thay đổi |
| **Xác định** | Nhấp nháy một kênh lên đầy trong ~1.5s để xác định vật dụng vật lý |
| **IP tĩnh hoặc DHCP** | Cấu hình IP/gateway/subnet/DNS tĩnh, hoặc DHCP tự động |
| **WiFi nhận thức mesh** | Quét tất cả kênh và kết nối **mạng mạnh nhất** (thân thiện mesh/đa-AP) |
| **Cổng thiết lập đầu tiên** | Khi khởi động lần đầu (hoặc giữ nút BOOT) thiết bị mở access point `LuxDMX-setup` riêng của nó với captive portal |
| **Chọn chế độ mạng** | Chọn WiFi hoặc Ethernet dây, và WiFi client/STA hoặc AP độc lập |
| **Chế độ AP độc lập** | Tạo mạng WiFi riêng tại `192.168.4.1` |
| **Chính sách mất kết nối dây** | Giữ kết nối lại / mở WPA2 AP / khởi động lại / chuyển về WiFi đã lưu. Watchdog thời gian thực áp dụng giữa chạy. |
| **mDNS + DHCP hostname** | Truy cập qua `dmx-gateway.local` qua mDNS, và thiết bị gửi hostname qua DHCP (tùy chọn 12) |
| **REST API** | `GET /dmx.json`, `/senders.json`, `/log.json`, `/version.json`, `/labels.json`, `/info.json`, `/rdm.json` |
| **OTA theo phiên bản** | Cài đặt bất kỳ bản phát hành nào, hoặc tự động cập nhật bản mới nhất. Asset GitHub release được **ký Ed25519** — thiết bị xác minh chữ ký trước khi cam kết. |
| **Cập nhật OTA** | ArduinoOTA (IDE/CLI) + tải lên `.bin` thủ công + cập nhật một cú nhấp từ luxdmx.org |
| **Chính sách hàng đợi nền** | Mức độ nghiêm trọng thu thập trạng thái ArtPoll có thể cấu hình (vô hiệu / lời khuyên / cảnh báo / lỗi) — điều chỉnh tần suất thiết bị báo cáo nền cho bảng điều khiển |
| **Giám sát kiểm tra kéo dài** | Cờ build `LUXDMX_SOAK_TEST` bật watchdog bộ nhớ 60 giây, ghi nhận DRAM/PSRAM mỗi phút và khởi động lại nếu DRAM còại lại dưới 30KB. Được phơi bày qua `/diag/soak-stats`. |

### Phần cứng & I/O

| Tính năng | Chi tiết |
|---|---|
| **Ethernet dây** | W5500 (SPI, tất cả board), DM9051 (SPI, chưa thử nghiệm), LAN8720/IP101/RTL8201/DP83848/KSZ8081/JL1101 (RMII, WT32-ETH01) |
| **LED trạng thái** | GPIO thường, WS2812 RGB NeoPixel, hoặc panel 5-LED — xanh = kết nối, xanh lam = RDM, cam = dự phòng WiFi, đỏ = không mạng |
| **Màn hình tùy chọn** | I2C SSD1306 / SH1106 (128×64, 128×32) hoặc SPI SSD1351 màu (128×128) |
| **Điều khiển trên thiết bị** | Encoder quay + lên đến 4 nút tùy chọn điều khiển menu trên màn hình nhỏ |
| **Chân DMX có thể cấu hình** | Từng kênh: vũ trụ, cổng UART, chân TX / RX / RTS GPIO — cài đặt thời gian thực qua giao diện web, không cần biên dịch lại |
| **Lưu trữ NVS** | Vũ trụ, giao thức, IP cấu hình, nhãn, hostname, mật khẩu OTA, chân LED/DMX — sống sót qua khởi động lại |
| **Đặt lại cấu hình** | Giữ nút BOOT 3s khi khởi động, hoặc qua trang `/reset` |
| **Khởi động lại từ xa** | Khởi động lại từ giao diện web hoặc `POST /reboot` |

---

## 📋 Yêu cầu tiên quyết

| Yêu cầu | Phiên bản |
|---|---|
| **PlatformIO Core** (tiếp xúc VS Code khuyến nghị) | mới nhất |
| **Python** (cho `esptool`) | 3.8+ |
| **Arduino IDE** (tùy chọn, cho OTA serial) | 2.0+ |
| **Board hỗ trợ** | ESP32 (WROOM-32), ESP32-S3 DevKitC-1, WT32-ETH01, hoặc PCB LuxDMX v6 / LuxDMX-4uni |

### Các môi trường build hỗ trợ

`platformio.ini` định nghĩa các môi trường sau:

| Môi trường | MCU | Build | Mạng | Kênh ra | Ghi chú |
|---|---|---|---|---|---|
| `esp32dev` | ESP32 (WROOM-32) | Được biên dịch sẵn | WiFi | 2 | LED mặc định trên GPIO2; Ethernet W5500 SPI tùy chọn |
| `esp32s3dev` | ESP32-S3 | Từ nguồn | WiFi | 2 | WS2812 trên GPIO48; bộ giảm áp brownout đã tắt (build từ nguồn) |
| `wt32eth01` | ESP32 | Được biên dịch sẵn | RMII Ethernet | 2 | DMN trên GPIO4/5; WiFi có thể chọn thời gian chạy |
| `esp32s3_psram` | ESP32-S3 | Từ nguồn | WiFi | 2 | PSRAM 8MB octal bật; RAM bên ngoài cho lwIP/bảng RDM |
| `esp32s3_n16r8_eth` | ESP32-S3 | Từ nguồn | W5500 SPI | **4** | PCB LuxDMX-4uni: 4 vũ trụ, 8MB PSRAM, giám sát kiểm tra kéo dài |

---

## ⚙️ Cài đặt & Sử dụng

### ⚡ Flash từ trình duyệt — không cần cài đặt, không cần dòng lệnh

Mở **[trình flash web](https://tombueng.github.io/LuxDMX/)** trong Chrome hoặc Edge trên máy tính, cắm board, chọn model, nhấn flash. Hoàn tất — không cần Python, esptool, hay toolchain.

> Các phương pháp thủ công/scripted dưới đây vẫn hoạt động cho WT32-ETH01 (không có cổng USB).

### Chế độ khởi động (bắt buộc cho tất cả phương pháp thủ công)

1. Giữ nút **BOOT**
2. Nhấn và giải phóng **EN** (hoặc **RST**) trong khi vẫn giữ BOOT
3. Giải phóng BOOT — chip bây giờ ở chế độ tải xuống
4. Chạy lệnh flash

> **ESP32-S3 DevKitC-1:** sử dụng cổng **USB-UART** (đánh dấu trên board), không phải cổng USB native.

#### Windows — lệnh một dòng (PowerShell)

```powershell
Set-ExecutionPolicy -Scope Process Bypass; irm https://raw.githubusercontent.com/tombueng/LuxDMX/master/flash.ps1 | iex
```

#### macOS / Linux

```bash
pip install esptool

# ESP32 (WROOM-32)
REPO=tombueng/LuxDMX
for f in firmware.bin bootloader.bin partitions.bin boot_app0.bin; do
  curl -sL "$(curl -s https://api.github.com/repos/$REPO/releases/tags/latest \
    | python3 -c "import sys,json; assets=json.load(sys.stdin)['assets']; \
      print(next(a['browser_download_url'] for a in assets if a['name']=='$f'))")" -o $f
done

esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m \
  0x1000 bootloader.bin 0x8000 partitions.bin \
  0xe000 boot_app0.bin 0x10000 firmware.bin
```

> Thay `/dev/ttyUSB0` bằng cổng của bạn (`/dev/tty.usbserial-*` trên macOS).

### Build từ nguồn (PlatformIO)

```bash
# Cài đặt PlatformIO
pip install platformio

# Build cho board của bạn
pio run -e esp32dev      # ESP32 WROOM-32
pio run -e esp32s3dev    # ESP32-S3 DevKitC-1
pio run -e wt32eth01     # WT32-ETH01
pio run -e esp32s3_psram # ESP32-S3 với PSRAM
pio run -e esp32s3_n16r8_eth  # LuxDMX-4uni (4 vũ trụ)

# Flash (lần flash đầu tiên qua USB)
pio run -e esp32s3dev --target upload
```

> **Tải lên thất bại ("Wrong boot mode")?** Giữ nút BOOT, nhấn EN/RST, giải phóng BOOT — chip vào chế độ tải xuống. Thử lại.

### Cấu trúc dự án

```
LuxDMX-v2/
├── src/
│   ├── main.cpp              ← điểm nhập mỏng (setup + loop)
│   ├── cfg/                  ← engine cấu hình: schema, core, serial, NVS migration
│   │   ├── config_schema.cpp ← BẢNG TRƯỜNG duy nhất (một hàng cho mỗi cài đặt)
│   │   ├── config_core.cpp   ← NVS tải/lưu, giải quyết mẫu
│   │   ├── config_serial.cpp ← console serial (giao diện key=value)
│   │   └── nvs_migrate.cpp   ← chuyển đổi NVS V1→V2
│   ├── drv/                  ← driver phần cứng cấp thấp
│   │   ├── dmx_rmt.h         ← engine truyền DMX RMT
│   │   ├── uart_rx.h         ← UART RX-only cho phản hồi RDM
│   │   └── gpio_dir.h        ← điều khiển GPIO DE/RE cho RDM
│   ├── core/                 ← lõi giao thức DMX/RDM
│   │   ├── dmx_buffer.cpp    ← bộ đệm DMX với seqlock
│   │   ├── output_init.cpp   ← khởi tạo kênh RMT + UART, guard an toàn
│   │   ├── merge_engine.cpp  ← hòa trộn nguồn HTP/LTP
│   │   ├── sender_tracker.cpp← theo dõi nguồn hoạt động + phát hiện xung đột
│   │   ├── rdm_engine.cpp    ← controller RDM E1.20 (RMT-TX + UART-RX)
│   │   ├── rdm_disc.cpp      ← khám phá DISC_UNIQUE_BRANCH
│   │   └── stats.cpp         ← thống kê thời gian thực
│   ├── net/                  ← giao thức mạng
│   │   ├── artnet.cpp        ← Art-Net nhận + cây bridge RDM
│   │   ├── sacn.cpp          ← nhận sACN/E1.31 multicast
│   │   ├── ethernet.cpp      ← W5500/DM9051 SPI + LAN8720 RMII
│   │   ├── net_state.cpp     ├── WiFi STA/AP, kết nối AP mạnh nhất
│   │   ├── websocket.cpp     ← WebSocket đẩy sống (khung nhị phân + JSON)
│   │   ├── ws_frame.cpp      ← bộ tạo khung nhị phân
│   │   └── web_server.cpp    ← đăng ký tuyến route AsyncWebServer
│   ├── sys/                  ← task nền / nền tảng
│   │   ├── tasks.cpp         ← tạo FreeRTOS task (pinned core)
│   │   ├── led_status.cpp    ← driver LED trạng thái
│   │   ├── display.cpp       ← driver OLED/SPI
│   │   ├── ota.cpp           ← cập nhật OTA khởi động + init
│   │   └── sys_platform.cpp  ← danh tính board, lên lịch khởi động lại
│   ├── app/                  ← lớp ứng dụng
│   │   ├── input_map.h       ← bản đồ encoder/nút → sự kiện nav
│   │   ├── menu.h            ← hệ thống menu trên màn hình
│   │   └── enc_decode.h      ← bộ giải mã quadrature
│   ├── pages/                ← tệp HTML giao diện web (HTML thuần)
│   ├── assets/               ← hình ảnh/CSS phục vụ bởi ESP32
│   └── generated/            ← tự động tạo lúc build (được gitignore)
├── include/                  ← header công khai (config_schema.h, output.h, seqlock.h, ...)
├── templates/                ← giá trị mặc định cho từng board (_base.ini, luxdmx_4uni.ini)
├── lib/EmbeddedConfig/       ← engine cấu hình mô-đun có thể tái sử dụng (NVS + console)
├── tools/                    ← công cụ build/phát triển (gen_config_templates.py)
├── test/native/              ← test round-trip cấu hình phía máy chủ
└── platformio.ini
```

> **Pipeline build:** Trước mỗi `pio run`, `extra_scripts.py` chuyển `src/pages/*.html` và `src/assets/*` thành mảng C `PROGMEM` trong `src/generated/*.h`, và nhúng `templates/*.ini` vào `src/generated/config_templates.gen.h`. Các giá trị động sử dụng token `{{PLACEHOLDER}}` được thay thế tại thời điểm yêu cầu.

---

## 🖥️ Giao diện web

Máy chủ HTTP và WebSocket được phục vụ bởi ESPAsyncWebServer (không chặn), vì vậy giao diện web không bao giờ làm treo đầu ra DMX. Các trang được nén gzip.

### Trang

| URL | Phương thức | Chức năng |
|---|---|---|
| `/` | GET | Trạng thái sống + lưới 512 kênh DMX |
| `/config` | GET / POST | Thay đổi vũ trụ, giao thức, chế độ hòa trộn, chính sách mất tín hiệu, IP tĩnh, hostname, mật khẩu OTA, cấu hình LED, chân DMX, điều khiển trên thiết bị |
| `/reset` | GET / POST | Xóa thông tin WiFi, khởi động lại vào chế độ AP |
| `/reboot` | POST | Khởi động lại thiết bị (**POST only**) |
| `/rdm` | GET | Trang điều khiển RDM |
| `/setup` | GET / POST | Cổng thiết lập lần đầu |

### REST API

| Endpoint | Phương thức | Chức năng |
|---|---|---|
| `/dmx.json` | GET | Tất cả 512 giá trị, fps, rssi, uptime, heap, cờ chế độ thủ công |
| `/senders.json` | GET | Các nguồn Art-Net / sACN đang hoạt động |
| `/log.json` | GET | Các mục nhật ký thay đổi DMX gần đây |
| `/version.json` | GET | Phiên bản firmware hiện tại + cờ cập nhật có sẵn |
| `/info.json` | GET | Cài đặt + trạng thái hiện tại (SSID, IP, vũ trụ, board, ...) |
| `/labels.json` | GET | Nhãn kênh |
| `/labels` | POST | Lưu toàn bộ đối tượng nhãn |
| `/rdm.json` | GET | Trạng thái điều khiển RDM + thiết bị đã khám phá (TOD) |
| `/rdm/discover` | GET | Kích hoạt quét khám phá RDM |
| `/rdm/setaddr` | GET | Đặt địa chỉ bắt đầu (`?uid=...&addr=1..512`) |
| `/rdm/identify` | GET | Bật/tắt xác định (`?uid=...&on=0/1`) |
| `/rdm/merge` | GET | Đặt chế độ hòa trộn ra (`?out=0&mode=0/1/2`) |
| `/led/bright` | GET | Độ sáng panel 5-LED (`?r=&g=&b=&y=&w=`, `&save=1` để lưu, `&test=1` để hiệp thức) |
| `/ota/upload` | POST | Tải lên và flash `firmware.bin` cục bộ |
| `/ota/github` | POST | Cài đặt một bản phát hành (`version=latest` hoặc `1.0.N`) |
| `/ota/url` | POST | Cài đặt `.bin` từ bất kỳ URL nào (`url=http://host/firmware.bin`) |
| `/ota/status` | GET | Tiến trình cài đặt đang diễn ra (`{phase,pct}`) |
| `/autoupdate` | POST | Bật/tắt tự động cập nhật (`enabled=0/1`) |
| `/health` | GET | Kiểm tra sức khỏe với trạng thái từng kênh, thông tin mạng, cảnh báo |
| `/diag/soak-stats` | GET | Thống kê giám sát kiểm tra kéo dài (DRAM/PSRAM, thời gian hoạt động) — chỉ với build `LUXDMX_SOAK_TEST` |
| `/config/export` | GET | Xuất cấu hình dưới dạng JSON (`?include_credentials=1` để bao gồm mật khẩu) |
| `/config/import` | POST | Nhập cấu hình từ JSON (`config=<json>`) |

### WebSocket (`ws://<device>/ws`)

Khung nhị phân trạng thái/DMX đẩy ~10×/s:

```
Bytes  0–1    fps × 10           uint16 big-endian (tổng hợp)
Bytes  2–3    metric liên kết        int16  (≤0 = WiFi RSSI dBm, ≥10 = tốc độ dây Mbps, 1 = AP)
Bytes  4–7    heap trống          uint32 big-endian
Bytes  8–11   thời gian hoạt động (s)         uint32 big-endian
Byte   12     số nguồn hoạt động uint8
Byte   13     trạng thái nguồn       uint8 (0 = bình thường, 1 = xung đột, 2 = hòa trộn)
Bytes  14–15  jitter × 10 (ms)   uint16 big-endian
Bytes  16–527 DMX kênh 1–512       uint8[512]
Bytes  528…   fps × 10 từng kênh uint16 × số kênh ra
```

Trình duyệt &rarr; ESP32 (lệnh JSON text):

```json
{ "type": "set",      "ch": 1,  "val": 200 }
{ "type": "mode",     "manual": true       }
{ "type": "blackout"                       }
{ "type": "identify", "ch": 5              }
{ "type": "viewout",  "out": 1             }
{ "type": "rdm",      "action": "discover" }
{ "type": "rdm",      "action": "setaddr",  "uid": "4c5812345678", "addr": 1 }
{ "type": "rdm",      "action": "identify", "uid": "4c5812345678", "on": true }
{ "type": "rdm",      "action": "setpers",  "uid": "4c5812345678", "pers": 2 }
{ "type": "rdm",      "action": "setlabel", "uid": "4c5812345678", "label": "My Fixture" }
```

---

## 🔧 Cấu hình (V2 Schema-Driven)

V2 sử dụng hệ thống cấu hình DRIVER TRÊN SCHEMA. Mỗi cài đặt được lưu trữ được mô tả một lần dưới dạng hàng trong `src/cfg/config_schema.cpp`, và bảng này điều khiển:

- NVS tải/lưu (`cfgcore::load()` / `save()`)
- Form web `/config` (tự động sinh từ schema)
- Console serial (`cfgserial::poll()`)
- Test phía máy chủ (kiểm tra round-trip)

**Thứ tự giải quyết:** giá trị trung gian (từ ràng buộc) &rarr; template board hoạt động (`templates/*.ini`) &rarr; giá trị NVS đã lưu.

### Template board

Giá trị mặc định nằm trong `templates/*.ini`, chọn thời gian biên dịch bởi `-DDEFAULT_TEMPLATE=...`. Template của từng board mở rộng `_base`:

| Template | Board | Ghi chú |
|---|---|---|
| `_base` | Tất cả | Mặc định toàn cầu: hostname, LED, mạng, chân W5500/RMII |
| `esp32dev` | ESP32 DevKit | LED GPIO2, GPIO17/16 DMX, W5500 trên VSPI |
| `esp32s3dev` | ESP32-S3 DevKitC-1 | WS2812 GPIO48, GPIO17/16 DMX |
| `wt32eth01` | WT32-ETH01 | RMII LAN8720, GPIO4/5 DMX |
| `luxdmx_v6` | PCB LuxDMX v6 | Panel 5-LED, W5500 SPI3, RTS/DE=8 |
| `luxdmx_4uni` | LuxDMX-4uni | 4 vũ trụ, 8MB PSRAM, panel 5-LED |

Một chủ sở hữu v6/4uni flash bản build `esp32s3dev` / `esp32s3_n16r8_eth` tổng quát và chọn template board khớp trong `/config &rarr; Hardware board` để có bản đồ chân đầy đủ.

### Cài đặt lưu trữ (NVS)

| Danh mục | Khóa | Mặc định | Khởi động lại? |
|---|---|---|---|
| **Định danh** | hostname | `dmx-gateway` | Trực tiếp |
| | boardSel | (được phát hiện) | Khởi động lại |
| | otaPassword | `dmxota` | Khởi động lại |
| | protocol | `Cả hai (Art-Net + sACN)` | Khởi động lại |
| **LED trạng thái** | ledPin | board mặc định | Khởi động lại |
| | ledType | board mặc định | Khởi động lại |
| | ledBrR/G/Y/B/W | 255 / 255 / 255 / 255 / 255 | Trực tiếp |
| **Mạng** | useEthernet | false | Khởi động lại |
| | wifiMode | STA (client) | Khởi động lại |
| | wifiSsid / wifiPsk | (none) | Khởi động lại |
| | staticIp | false | Khởi động lại |
| | ip / gateway / subnet / dns | (DHCP) | Khởi động lại |
| | linkLossMode | keep retrying | Khởi động lại |
| | ipProg | off | Khởi động lại |
| | apPassword | (none) | Khởi động lại |
| **RDM** | artnetRdm | true | Khởi động lại |
| | rdmMaxDev | 0 (auto) | Khởi động lại |
| **Cập nhật** | autoUpdate | false | Khởi động lại |
| **DMX Output A** | enabled | on | Khởi động lại |
| | universe | 0 | Trực tiếp |
| | port | 1 | Khởi động lại |
| | txPin | 17 | Khởi động lại |
| | rxPin | 16 | Khởi động lại |
| | rtsPin | -1 | Khởi động lại |
| | mergeMode | off | Trực tiếp |
| | lossMode | hold | Trực tiếp |
| | txRate | 40 fps | Trực tiếp |
| | txStyle | Continuous | Trực tiếp |
| | txStyleSrc | Local | Trực tiếp |
| | mode | DMX only | Khởi động lại |
| | net | 0 | Khởi động lại |
| | subnet | 0 | Khởi động lại |
| | sacnUniverse | 0 (auto) | Khởi động lại |

---

## 🔌 Đầu ra DMX

LuxDMX V2 điều khiển lên đến **4 kênh ra DMX** — mỗi kênh là một vũ trụ độc lập, kênh RMT, và bộ truyền RS485 — có thể cấu hình thời gian thực tại **Cài đặt &rarr; DMX Outputs** (không cần biên dịch lại).

| Kênh ra | Kênh RMT | UART (RDM RX) | GPIO TX | GPIO RX | RDM DE/RE | Chế độ |
|---|---|---|---|---|---|---|
| A | 0 | UART1 | 17 | 16 | 8 (v6/4uni) | DMX / RDM-full |
| B | 1 | UART2 | 16 | 15 | 7 (4uni) | DMX / RDM-full |
| C | 2 | — | 5 | — | — | DMX-only |
| D | 3 (DMA) | — | 6 | — | — | DMX-only |

### Cài đặt từng kênh ra

| Cài đặt | Mặc định (A) | Mặc định (B) | Mặc định (C/D) | Mô tả |
|---|---|---|---|---|
| Enabled | on | off | off | Kênh có điều khiển dây DMX hay không |
| Universe | 0 | 1 | 2 / 3 | Vũ trụ Art-Net (sACN = vũ trụ + 1) |
| UART port | 1 | 2 | 0 | Số UART cho RX RDM |
| TX pin | 17 | 16 | 5 / 6 | GPIO ra dữ liệu |
| RX pin | 16 | 15 | -1 | GPIO vào dữ liệu (cần cho RDM) |
| RTS / DE pin | -1 | 7 | -1 | Điều khiển hướng (bắt buộc cho RDM) |
| Merge mode | off | off | off | off / HTP / LTP |
| Signal-loss | hold | hold | hold | hold / blackout / stop |
| TX rate | 40 fps | 40 fps | 40 fps | 20 / 25 / 33.3 / 40 / 41.7 fps |
| TX style | Continuous | Continuous | Continuous | Continuous (chạy tự do) / Delta (theo vào) |
| Output mode | DMX only | DMX only | DMX only | DMX only / RDM full (DE/RE). Đặt chân RTS tự động bật RDM. |
| Style source | Local | Local | Local | Local (web/serial) / Art-Net (bảng điều khiển đẩy) |

### Hướng dẫn GPIO an toàn

**ESP32-S3 — GPIO tự do cho kênh bổ sung:** 5, 6, 7, 8, 15, 18, 21

**Tránh:** 26–37 (SPI flash / PSRAM — sẽ crash), 19/20 (USB), 43/44 (serial), 0/45/46 (strapping), 48 (WS2812 LED).

**WT32-ETH01:** GPIO16 là nguồn LAN8720 — tránh. DMX trên GPIO4/5.

### Nâng cấp & An toàn khi lỗi

- **Thiết bị đơn vũ trụ không bị ảnh hưởng.** Di chuyển cấu hình đầu ra duy nhất sang Output A và để B/C/D tắt.
- **Không có cấu hình nào có thể brick thiết bị.** Bộ đếm lỗi trong NVS vô hiệu hóa tiến triển các kênh nếu init gặp panic — giữ A &rarr; tất cả tắt cho đến khi giao diện web khả dụng.

---

## 🌐 Chế độ mạng

### WiFi (Client / STA)

Khi khởi động lần đầu (hoặc sau khi đặt lại WiFi, hoặc với BOOT được giữ), LuxDMX mở access point thiết lập của chính nó và captive portal:

- **SSID:** `LuxDMX-setup` (mở, first-run truy cập vật lý)
- Chọn **Tham gia WiFi của tôi** hoặc **Sử dụng làm access point**
- Thiết bị lưu lựa chọn và khởi động lại vào chế độ đã chọn

**Mesh / đa-AP:** LuxDMX quét tất cả kênh và kết nối **mạng mạnh nhất** cho SSID của bạn.

### Chế độ AP độc lập

```
WiFi mode = AP
truy cập tại 192.168.4.1
```

### Ethernet dây

- **W5500 (SPI):** CS / SCK / MOSI / MISO / INT / RST có thể cấu hình trong `/config`. Chân mặc định khớp với ESP32 VSPI cổ điển.
- **LAN8720 (RMII):** WT32-ETH01 (mặc định) hoặc bất kỳ ESP32 nào + PHY RMII. PHY family, địa chỉ, MDC/MDIO/RST/GPIO0 CLK đều có thể cấu hình.
- **Chính sách mất kết nối dây:** giữ kết nối lại / mở WPA2 AP / khởi động lại / chuyển về WiFi. Watchdog thời gian thực áp dụng ngay cả khi cáp bị tháo giữa chạy.

### Cấu hình IP từ xa qua Art-Net (ArtIpProg)

Thiết bị kết nốa trên một địa chỉ không thể truy cập được có thể được cấu hình lại qua Art-Net `ArtIpProg` (opcode `0xf800`) từ một bảng điều khiển. **Tắt mặc định** — Art-Net không có xác thực, vì vậy trong khi bật, bất kỳ ai trên mạng có thể thay đổi địa chỉ.

---

## 🎛 Điều khiển trên thiết bị

Encoder quay tùy chọn + lên đến 4 nút điều khiển menu trên màn hình nhỏ để cài đặt vũ trụ, giao thức, v.v. mà không cần điện thoại hay PC. Tất cả lựa chọn dây chuyền được tổng hợp thành một bảng chữ cái điều hướng hữu ích:

| Đầu vào vật lý | Nhấn ngắn | Nhấn dài |
|---|---|---|
| Quay encoder | Tăng / Giảm | — |
| Nhấn encoder | ENTER | BACK |
| Nút Next | Tăng | ENTER |
| Nút Prev | Giảm | ENTER |
| Nút Enter | ENTER | BACK |
| Chỉ có 1 nút | Tăng | ENTER |

Cài đặt: chân encoder A/B/nhấn, số bước/hướng, chân nút + active-high/low, vai trò nút (off / enter / back / next / prev). Menu luôn mang một mục **Exit**, vì vậy ngay cả một nút duy nhất cũng có thể điều hướng ra ngoài.

---

## 🖼 LED trạng thái

| Trạng thái | WS2812 RGB | Panel 5-LED | GPIO thường |
|---|---|---|---|
| Khởi động | nhấp nháy trắng | dải chuyển Knight-Rider | nhấp nháy |
| Kết nối | xanh (đèn sáng) | xanh (đèn sáng) | bật |
| DMX đến | xanh, nhấp nháy 2s | xanh, nhấp nháy 2s | nhấp nháy chậm |
| Hoạt động RDM | xanh + xanh lam = xanh lam | xanh + xanh lam bật | bật |
| Dự phòng WiFi | cam (đèn sáng) | vàng (đèn sáng) | bật |
| Không mạng | đỏ (đèn sáng) | đỏ | tắt |
| Cổng thiết lập | tím | tím | bật |

**Độ sáng panel 5-LED:** PWM từng màu độc lập (`ledbrr`/`ledbrg`/`ledbry`/`ledbrb`/`ledbrw`, 0–255). Điều chỉnh trực tiếp qua `/led/bright?test=1`.

---

## 📱 Cấu hình qua Serial (Khẩi phục)

Nếu board không thể kết nối mạng, cắm USB, mở monitor serial ở 115200 baud, gõ `help`:

| Lệnh | Thực hiện |
|---|---|
| `dump` | In mọi cài đặt dưới dạng `key=value` (mật khẩu được che) |
| `key=value [key=value ...]` | Đặt một hoặc nhiều trường |
| `get <key>` / `set <key> <value>` | Đọc / ghi một trường |
| `save [reboot]` | Lưu vào NVS, tùy chọn khởi động lại |
| `wifi <ssid> [pass]` | Đặt thông tin WiFi và kết nối lại |
| `reboot` / `factory` | Khởi động lại / xóa cấu hình và khởi động lại |

---

## 🧪 Kiểm thử

```bash
cd docs && npm install && npx playwright install chromium
LUXDMX_HOST=dmx-gateway.local npm test
```

Các bài kiểm tra Playwright end-to-end điều khiển một **thiết bị thực** — gửi gói Art-Net/sACN thực qua mạng, kiểm tra REST API, WebSocket, và giao diện web. Test native round-trip cấu hình (`test/native/`) biên dịch engine cấu hình phía máy chủ với các shim nhỏ — không cần framework.

---

## 📦 Phần cứng

> ### 🛠 PCB tùy chỉnh — LuxDMX v6
>
> Nguồn mở PCB 4 lớp: ESP32-S3 với WiFi + Ethernet (W5500), **hai vũ trụ DMX cách điện hoàn toàn**,
> 802.3af PoE hoặc USB-C, panel LED 5 màu, nút BOOT/RST, màn hình OLED/TFT tùy chọn. Handset nhỏ gọn và có thể sản xuất tại JLCPCB với vài đồng.
>
> **→ Thiết kế đầy đủ, BOM, gerber & hướng dẫn JLCPCB: [`hardware/`](hardware/README.md)**

Phiên bản đơn giản hơn **breadboard / module** (ESP32 DevKit + mô-đun RS485 cách điện) hoàn hảo để bắt đầu. Xem README gốc để có sơ đồ mạch hoàn chỉnh và bảng BOM.

---

## 🔄 Hướng dẫn chuyển đổi: V1 &rarr; V2

V2 là một bản **rewrite hoàn toàn mô-đun**. Phần nên ánh xạ mọi khái niệm V1 sang V2 tương đương để bạn có thể so sánh hai phiên bản cạnh nhau, và giải thích chính xác những gì thay đổi (và giữ nguyên) khi flash build V2 lên thiết bị đang chạy firmware V1.

### Kiến trúc: Độc lập &rarr; 5 lớp mô-đun

Firmware V1 gốc là một tệp `main.cpp` ~5.000 dòng duy nhất mist tất cả: định nghĩa chân, lưu trữ config, phân tích Art-Net/sACN, I/O DMX, web server, RDM, và console serial — tất cả trong một đơn vị biên dịch. V2 chia thành một kiến trúc 5 lớp ngăn chặn, mỗi lớp sở hữu một mối quan tâm riêng và có thể kiểm thử độc lập.

| Lớp | V1 (`main.cpp`) | V2 (mô-đun) | Tệp chính |
|---|---|---|---|
| **drv** (driver) | UART TX qua thư viện `esp_dmx` | RMT hardware TX + UART RX-only + GPIO DE/RE | `src/drv/dmx_rmt.h`, `src/drv/uart_rx.h`, `src/drv/gpio_dir.h` |
| **cfg** (config) | Macro `#define DEF_*` cho giá trị mặc định; NVS read/write inline | Bảng schema điều khiển NVS tải/lưu, console serial, form web | `src/cfg/config_schema.cpp`, `src/cfg/config_core.cpp`, `src/cfg/nvs_migrate.cpp` |
| **core** (giao thức DMX/RDM) | Bộ đệm DMX + merge inline trong `main.cpp` | Bộ đệm khung bảo vệ bởi seqlock, engine hòa trộn, theo dõi nguồn, router khung, engine RDM | `src/core/dmx_buffer.cpp`, `src/core/merge_engine.cpp`, `src/core/sender_tracker.cpp`, `src/core/rdm_engine.cpp`, `src/core/rdm_disc.cpp` |
| **net** (mạng) | Thư viện `ArtnetWifi` + WiFi/Ethernet inline | Tự cài đặt Art-Net/sACN + W5500/RMII bản địa + AsyncWebServer + WebSocket + OTA | `src/net/artnet.cpp`, `src/net/sacn.cpp`, `src/net/websocket.cpp`, `src/net/ota.cpp` |
| **sys/app** (hệ thống) | FreeRTOS task inline + LED/màn hình inline | Task pinned core-0/core-1, crash-guard, soak monitor, OTA rollback | `src/sys/tasks.cpp`, `src/sys/led_status.cpp`, `src/sys/soak_monitor.cpp` |

**`main.cpp` (V2)** là một tệp wiring mỏng ~130 dòng: gọi `nvs_migrate::migrateNvsKeys()`, `cfgcore::load()`, `outputInitAll()`, `startSacn()`, `webRegisterRoutes()`, rồi `createTasks()`. Tất cả logic thực chất sống trong các lớp.

### Truyền thức DMX: UART &rarr; RMT

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Peripheral** | UART + GPTimer ISR (qua thư viện `esp_dmx`) | **Phụ kiện RMT** — luồng symbol phần cứng, không có vòng lặp timing CPU |
| **Lỗi được sửa** | Cạnh tranh DMA của core-0 WiFi/lwIP trì hoãn ISR break/MAB, gây khung lỗi dưới tải nặng (issue #64) | RMT đồng hồ hoàn toàn trong phần cứng; nếu ISR refill chậm, dây chỉ ngồi yên (một dấu phụ bình thường) — xem `src/drv/dmx_rmt.h:2-9` |
| **Phụ thuộc thư viện** | `someweisguy/esp_dmx` | Không có — tự sở hữu `dmx_rmt.h` |
| **Vận chuyển RDM** | Cùng UART, chuyển half-duplex (chân DE/RE được bật/tắt, hướng được cấu hình lại thời gian chạy) | RMT-TX cho yêu cầu + **UART RX-only riêng** cho phản hồi (`src/drv/uart_rx.h`); không bao giờ được giải phóng giữa khung, DMX chạy liên tục giữa các thao tác RDM |

### Đầu ra: 2 &rarr; 4 vũ trụ

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Số kênh tối đa** | 2 (ESP32 có 3 UART; UART0 là console) | 4 (kênh RMT 0–3; ESP32-S3 `#error` nếu >4 — xem `include/config_schema.h:69`) |
| **Kênh RMT** | Không có (dựa trên UART) | 0–3; chỉ kênh 3 có DMA trên S3 — các kênh khác dùng ISR refill (xem `src/drv/dmx_rmt.h:101`) |
| **Kênh A** | UART1, GPIO17/16, RDM nếu có chân DE/RE | RMT ch 0, UART1 RX cho RDM, chân DE/RE có thể cấu hình |
| **Kênh B** | UART2, GPIO16/15, RDM nếu có chân DE/RE | RMT ch 1, UART2 RX cho RDM, chân DE/RE có thể cấu hình |
| **Kênh C/D** | Không có | RMT ch 2 / ch 3 (có DMA trên S3), DMX-only (không UART RX) |
| **Chế độ kênh** | Ngầm định (tự động hướng RS485 hoặc RDM dựa trên chân) | Rõ ràng `enum output_mode_t`: DMX-only vs RDM-full (`include/output.h:11`); đặt chân RTS tự động bật RDM (`resolveOutputMode()` tại `include/output.h:28`) |

### Cấu hình: Macros &rarr; Schema-driven templates

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Nguồn giá trị mặc định** | Macro `-DDEF_*` trong flag build `platformio.ini` | Tệp `templates/*.ini` chọn bởi `-DDEFAULT_TEMPLATE=...` (nhúng vào firmware bởi `extra_scripts.py`) |
| **Thứ tự giải quyết** | Giá trị macro, ghi đè bởi NVS | Trung gian (từ ràng buộc) → template board hoạt động → giá trị NVS đã lưu |
| **Bảng trường** | `Preferences` get/put inline trong `main.cpp` | Một bảng duy nhất trong `src/cfg/config_schema.cpp` (`CONFIG_FIELDS[]` + `OUTPUT_FIELDS[]`) điều khiển NVS, console serial, form web, và test native |
| **Khóa kênh** | `o0_tx`, `o0_uni`, `o1_tx`, `o1_uni` (2 kênh) | `a_tx`, `a_uni`, `b_tx`, ... `d_tx` (4 kênh, tiền tố chữ cánh) |
| **Di chuyển NVS** | Không có — truy cập khóa trực tiếp | `src/cfg/nvs_migrate.cpp:13` — một lần: `o0_*`→`a_*`, `o1_*`→`b_*`, `apfb`→`fbmode` |
| **Template board** | `#ifdef` cứng nhắc mỗi môi trường | `templates/_base.ini` mở rộng bởi template board — 33 board trong danh mục trực tuyến |
| **Chính sách mất kết nối** | `apFallback` (bool: true = mở AP WiFi) | `linkLossMode` (enum: 0=giữ kết nối, 1=mở WPA2 AP, 2=khởi động lại, 3=kết nối WiFi) — không bao giờ mở AP không bảo mật |

### Stack mạng

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Art-Net** | Thư viện `rstephan/ArtnetWifi` | Tự cài đặt; dispatch opcode đầy đủ (ArtPoll, ArtPollReply, ArtAddress, ArtIpProg, ArtSync, ArtNzs, ArtTod*, ArtRdm) trong `src/net/artnet.cpp` + `src/net/artnet_bridge.cpp` |
| **sACN** | Đường dẫn sACN của thư viện `ArtnetWifi` | Tự cài đặt trong `src/net/sacn.cpp` |
| **Phụ thuộc thư viện** | ArtnetWifi, Adafruit NeoPixel, Adafruit GFX, Adafruit SSD1306, Adafruit SH110X, Adafruit SSD1351 | **Chỉ** `ESP32Async/AsyncTCP` + `ESP32Async/ESPAsyncWebServer`; không Adafruit, không ArtnetWifi |
| **AsyncTCP** | Cấu hình nền tảng mặc định (core 0 hoặc 1, queue nhỏ) | Pin trên core 0 với stack 16KB + queue 128 (`platformio.ini:43-47`) để không bao giờ làm xung đột RDM trên core 1 |
| **Ethernet W5500** | Hỗ trợ W5500 qua `ETH_PHY_W5500` | Tương tự — nhưng các chân SPI module có thể cấu hình thời gian chạy (trước đây là thời gian build) |
| **Ethernet RMII** | Chỉ WT32-ETH01 | W5500 SPI + LAN8720 RMII + IP101/RTL8201/DP83848/KSZ8081/JL1101 — chọn tại thời gian chạy |

### Lịch task & Core Affinity

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Task DMX** | Dựa trên `loop()`, chạy trên core 0 (chung với WiFi/lwIP) | Task `dmxTxTask` chuyên dụng — **core 1, ưu tiên 19**, tick 1ms qua `vTaskDelayUntil` (`src/sys/tasks.cpp:82`) |
| **Task mạng** | `ArtnetWifi` + callback AsyncTCP trên core 0 | Task `netRxTask` chuyên dụng — **core 0, ưu tiên 5**, giới hạn 64 gói/tuần gọi (`tasks.cpp:144`) |
| **Dịch vụ RDM** | Gọi từ `loop()` | Được phục vụ trong `dmxTxTask` ở mỗi tick 1ms (không chỉ trên mỗi khung DMX) (`tasks.cpp:139`) — giữ tốc độ khám phá nhanh ngay cả trên look tĩnh |
| **LED/màn hình** | Inline trong `loop()` | Task `ledTask` / `displayTask` ưu tiên thấp |

### RDM

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Bộ PID controller** | DISC_UNIQUE_BRANCH, DEVICE_INFO, DMX_START_ADDRESS, IDENTIFY_DEVICE | Thêm: DEVICE_MODE, DEVICE_MODES, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_DEFINITION/VALUE/RECORD, STATUS_MESSAGE (`src/core/rdm_typed.cpp:138-242`) |
| **Liệt kê thiết bị phụ** | Không triển khai | `rdmSubDeviceCount()` truy vấn DEVICE_INFO; giới hạn thiết bị RDM tùy chọn qua `rdmMaxDev` (tự động mặc định) |
| **Vận chuyển** | UART half-duplex (DE/RE được bật/tắt) | RMT-TX + UART RX-only (không chuyển hướngng, DMX không bao giờ bị gián đoạn) |
| **Lịch khám phá** | Một giao dịch trên mỗi khung DMX | Một giao dịch trên mỗi khung DMX; khám phá là tìm kiếm nhị phân (`DISC_UNIQUE_BRANCH`) với ngân sách 8 giây (`src/core/rdm_disc.cpp:66`) |

### Phong cách truyền & Tốc độ kênh ra

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Tốc độ kênh ra** | Tĩnh 40 fps chạy tự do trên tất cả kênh | Từng kênh có thể cấu hình: 20 / 25 / 33.3 / 40 / 41.7 fps (`enum txRate`) |
| **Phong cách khung** | Chỉ chạy tự do (đồng hồ ở tần suất đã cấu hình bất kể đầu vào) | **Continuous** (chạy tự do) hoặc **Delta** (một khung DMX cho mỗi gói tin nhận được, giới hạn 22.76ms, tự động chuyển về chạy tự do sau 800ms không hoạt động) |
| **Theo dõi nguồn phong cách** | Không có | `txStyleSrc`: theo dõi phong cách được đặt cục bộ (web/serial) hay bởi bảng điều khiển (Art-Net `ArtAddress`) |
| **Hòa trộn nguồn** | HTP / LTP từng kênh, `SOURCE_TIMEOUT_MS = 4000` | Tương tự, nhưng bây giờ trong `src/core/merge_engine.cpp` |

### Giao diện web & Vòng đời cấu hình

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Trang web** | Chuỗi HTML inline `PROGMEM` trong `main.cpp` | `src/pages/*.html` chuyển thành `PROGMEM` bởi `extra_scripts.py` → `src/generated/*.h` |
| **Giá trị động** | `String::replace()` trên token `{{PLACEHOLDER}}` | Cơ chế tương tự, nhưng form `/config` là **tự động sinh từ bảng schema** |
| **Áp dụng cấu hình** | Khởi động lại cho tất cả thay đổi | **Trực tiếp** cho: vũ trụ, chế độ hòa trộn, chính sách mất tín hiệu, tốc độ TX, phong cách TX, độ sáng LED, giao thức, hostname; **Khởi động lại** cho: chân GPIO, loại LED/màn hình, cổng UART, cài đặt mạng (do flag `CFG_LIVE` vs `CFG_REBOOT` trong `config_schema.cpp`) |
| **Nhập/xuất cấu hình** | Lệnh serial `dump` + NVS | `/config/export` (JSON, với `&include_credentials=1`), `/config/import` (POST JSON), serial `dump`/`save` |
| **OTA** | ArduinoOTA + `httpUpdate` từ luxdmx.org | ArduinoOTA + tải lên web + GitHub release + cài đặt URL + **firmware ký Ed25519** (`src/net/ota_sign.cpp`, `OTA_SIGN_ENABLED` cho sản phẩm) |
| **Kiểm tra kéo dài** | Không có | Cờ build `LUXDMX_SOAK_TEST` — watchdog bộ nhớ 60 giây trên `esp32s3_n16r8_eth`, ghi nhận DRAM/PSRAM mỗi phút, khởi động lại nếu DRAM còn < 30KB (`/diag/soak-stats`) |

### An toàn khi lỗi

| Khía cạnh | V1 | V2 |
|---|---|---|
| **Khởi tạo kênh ra** | Inline trong `setup()`, panic brick thiết bị | Khởi tạo được bảo vệ (`dmxInitGuardBegin()`/`dmxInitGuardEnd()`) với đếm số lỗi NVS — vô hiệu hóa tiến triển kênh nếu init gặp panic (`src/sys/tasks.cpp:42`) |
| **Rollback OTA** | Bộ đếm `otatries`, tối đa 3 lần khởi động | Cơ chế tương tự, nhưng di chuyển sang `src/net/ota.cpp` với `OTA_BOOT_TRIES = 3` rõ ràng và zeroing sau 60s uptime ổn định |

### Thay đổi phá vỡ (danh sách kiểm tra nâng cấp)

| Thay đổi | Tác động |
|---|---|
| Thư viện `esp_dmx` bị xóa | Không có phụ thuộc biên dịch — DMX/RDM giờ đây là tự sở hữu (`src/drv/dmx_rmt.h`, `src/core/rdm_engine.h`); các kiểu cũ của `someweisguy/esp_dmx` được khai báo lại trong `include/rdm_types.h` như một bản thay thế trựng hợp |
| PlatformIO nền tảng được ghim xuống `pioarduino` v55.03.39 | Yêu cầu cho arduino-esp32 v3 / hỗ trợ ETH W5500; nền tảng `espressif32` chính thức bị kẹt ở v2.x |
| Build ESP32-S3 chạy từ nguồn | `CONFIG_ESP_BROWNOUT_DET=n` qua `custom_sdkconfig` — bộ giảm áp IDF được kích hoạt trước `setup()`, gây boot-loop trên phần cứng S3 thực |
| ENC28J60 không hỗ trợ | Sử dụng W5500 cho Ethernet (cùng bus SPI, stack TCP/IP phần cứng đầy đủ) |
| Thư viện `ArtnetWifi` bị xóa | Phân tích proocol Art-Net/sACN giờ đây tự cài đặt trong `src/net/artnet.cpp` + `src/net/sacn.cpp` |
| Thư viện Adafruit bị xóa | Driver hiển thị/LED là stub (`src/sys/led_status.cpp`, `src/sys/display.cpp`); hỗ trợ WS2812 và OLED sẽ quay lại như module tùy chọn |
| `MAX_OUTPUTS` tăng lên 4 | Kênh C và D chỉ là DMX-only (không RDM); cấu hình 2 kênh cũ tự động chuyển đổi sang A/B |
| `apFallback` (bool) &rarr; `linkLossMode` (enum) | 0 = giữ kết nối (trước `false`); 1 = mở WPA2 AP (trước `true`). Giá trị 2 (khởi động lại) và 3 (kết nối WiFi) là mới. AP **yêu cầu mật khẩu** — `linkLossMode=1` mà không `apPassword` sẽ chuyển về retry |

### Đường dẫn chuyển đổi

**Nâng cấp từ firmware V1:** Cấu hình hiện tại của bạn được chuyển đổi tự động khi khởi động lần đầu. `nvs_migrate::migrateNvsKeys()` (gọi tại `src/main.cpp:41`) thực hiện một lần chuyển đổi:

- Khóa kênh A: `o0_*` → `a_*` (ví dụ: `o0_tx` → `a_tx`) — `nvs_migrate.cpp:10-11`
- Khóa kênh B: `o1_*` → `b_*` (ví dụ: `o1_uni` → `b_uni`)
- `apfb` (bool) → `fbmode` (enum: `0` hoặc `2` nếu `apfb=false`, `1` nếu `apfb=true`) — `nvs_migrate.cpp:42-47`

Hàm `cfgcore::load()` cũng có fallback inline cho khóa kênh-0 cũ (`config_core.cpp:179`), vì vậy ngay cả NVS không đầy đủ đã bỏ qua `migrateNvsKeys()` vẫn giải quyết đúng. Không có dữ liệu bị mất; các kênh C và D khởi động ở trạng thái tắt và có thể bật trong `/config`.

Nếu gặp lỗi trong lúc khởi động V2 đầu tiên, bộ đếm crash-guard trong NVS (`namespace dmxgw`, khóa `dmxcrash`) sẽ tạm thời vô hiệu hóa các kênh theo thứ tự giảm dần. Xóa nó bằng cách khởi động lại thiết bị đến trạng thái ổn định (60 giây uptime đặt lại bộ đếm qua `dmxInitGuardEnd()`), hoặc qua trang `/reset` để đặt lại toàn bộ.

---

## 📄 Giấy phép

MIT — làm gì bạn muốn, đề nghị ghi công.

---

<p align="center">
  <sub>
    <a href="https://tombueng.github.io/LuxDMX/">⚡ Flash từ trình duyệt</a> &nbsp;·&nbsp;
    <a href="https://luxdmx.org/video">▶ Xem video demo</a> &nbsp;·&nbsp;
    <a href="hardware/README.md">🛠 PCB tùy chỉnh</a>
  </sub>
</p>