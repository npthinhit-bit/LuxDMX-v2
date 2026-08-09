# Hướng Dẫn Build Dự Án LuxDMX-v2 & Cách Fix Lỗi

> Tài liệu này hướng dẫn cách build firmware LuxDMX-v2 bằng PlatformIO và cách khắc phục các lỗi thường gặp khi chạy `pio run`.

## Mục lục

1. [Cài đặt môi trường](#cài-đặt-môi-trường)
2. [Build dự án](#build-dự-án)
3. [Flash firmware](#flash-firmware)
4. [Cách fix lỗi thường gặp](#cách-fix-lỗi-thường-gặp)
5. [Cấu trúc tham chiếu nhanh](#cấu-trúc-tham-chiếu-nhanh)

---

## Cài đặt môi trường

### Yêu cầu

| Phần mềm | Phiên bản |
|---|---|
| **PlatformIO Core** | 6.x (mới nhất) |
| **Python** | 3.11 hoặc 3.12 (chỉ một phiên bản trên PATH) |
| **Git** | dùng để clone dependency từ GitHub |
| **Trình soạn thức USB** (CP210x / CH340 / FT232) | tùy board |

### Cài đặt PlatformIO

#### Cách 1 — Qua VS Code (đề xuất)

1. Cài đặt [Visual Studio Code](https://code.visualstudioy.com/).
2. Mở VS Code → Extensions → tìm **"PlatformIO IDE"** → cài đặt.
3. Khởi động lại VS Code. PlatformIO sẽ tự động tạo môi trường ảo Python (`penv`).

#### Cách 2 — Dòng lệnh (CLI)

```powershell
pip install platformio
```

> **Lưu ý Python:** PlatformIO tạo một `penv` (virtual environment) riêng để quản lý các dependency Python của nó (build tools, OpenOCD, ...). Nếu hệ thống có nhiều phiên bản Python (ví dụ 3.11 + 3.12), PlatformIO có thể báo lỗi "Python version mismatch" khi `penv` cũ được tạo bởi một interpreter khác với interpreter hiện tại.

---

## Build dự án

Mở PowerShell/cmd/terminal tại thư mục gốc dự án (`LuxDMX-v2`), sau đó:

### Build cho board của bạn

```powershell
# ESP32 (WROOM-32 / DevKit)
pio run -e esp32dev

# ESP32-S3 DevKitC-1
pio run -e esp32s3dev

# WT32-ETH01 (RMII Ethernet)
pio run -e wt32eth01

# ESP32-S3 với PSRAM 8 MB
pio run -e esp32s3_psram

# LuxDMX-4uni (4 vũ trụ, PSRAM, W5500)
pio run -e esp32s3_n16r8_eth
```

### Các mục tiêu (target) hữu ích

```powershell
pio run -e esp32s3dev --target upload      # Build + flash
pio run -e esp32s3dev --target upload --upload-port COM3
pio run -e esp32s3dev --target monitor     # Mở serial monitor (115200 baud)
pio run -e esp32s3dev --target clean       # Dọn build cache
pio run -e esp32s3dev --target envdump     # In cấu hình môi trường đã giải quyết
```

### Quy trình build

Trước khi biên dịch, `extra_scripts.py` tự động:

1. Cố định `PATH` toolchain Xtensa ESP-IDF 14.2 (xử lý thư mục lồ nhau).
2. Sinh các file header `PROGMEM` từ `src/pages/*.html` và `src/assets/*` thành `src/generated/*.h`.
3. Nhúng `templates/*.ini` vào `src/generated/config_templates.gen.h`.

Bạn **không cần chạy thủ công** — PlatformIO gọi script này tự động trước mỗi lần build.

---

## Flash firmware

### Lần flash đầu (qua USB)

1. **Vào chế độ download:**
   - Giữ nút **BOOT**, nhấn và giải phóng **EN/RST**, sau đó thả BOOT.
2. **Flash:**

```powershell
pio run -e esp32dev --target upload --upload-port COM3
```

> Thay `COM3` bằng cổng USB thực tế của bạn. Xem trong Device Manager.
>
> **ESP32-S3 DevKitC-1:** dùng cổng **USB-UART** (gắn nhãn trên board), không phải cổng USB-C native.

### OTA (sau lần flash đầu)

Mở comment 2 dòng cuối `platformio.ini` để bật ArduinoOTA:

```ini
upload_protocol = espota
upload_port     = dmx-gateway.local
upload_flags    = --auth=dmxota
```

---

## Cách fix lỗi thường gặp

### Lỗi 1: `Warning! Ignore unknown configuration option 'src_inc' in section [env]`

```
Warning! Ignore unknown configuration option `src_inc` in section [env]
```

**Nguyên nhân:** `src_inc` là một tùy chọn tùy chỉnh (custom variable) trong `[env]` của `platformio.ini`, dùng để chứa các flag `-I` (đường dẫn include header). PlatformIO không nhận diện `src_inc` là một tùy chọn chuẩn nên xuất cảnh báo. Tuy nhiên, **đây chỉ là cảnh báo (warning), không phải lỗi** — PlatformIO vẫn xử lý biến này và `${env.src_inc}` vẫn được thay thế đúng giá trị khi build.

**Cách fix / Khuyên:**

- **Bỏ qua** — đây là hành vi dự định thiết kế. `platformio.ini` sử dụng `src_inc` để tách các flag include (`-Iinclude`, `-Isrc/cfg`, `-Isrc/drv`, v.v.) ra khỏi `build_flags`. Lý do: ở các môi trường build from-source (ESP32-S3), ESP-IDF CMake build thừa nhận `build_flags` và một `-Isrc/*` có thể gây va chạm tên header (ví dụ với `esp-mqtt`'s `platform.h`).

- **Kiểm chứng** build thành công bằng `pio run -e esp32dev --target envdump` — xem phần `build_flags` có chứa đầy đủ `-Iinclude -Isrc/cfg -Isrc/drv ...` hay không. Nếu có, biến `src_inc` đang hoạt động và cảnh báo là vô hại.

> Nếu muốn loại bỏ hoàn toàn cảnh báo (không khuyến nghị), có thể dời các flag `-I` trực tiếp vào `build_flags` của từng env — nhưng nhớ xóa `${env.src_inc}` và chấp nhận rủi ro va chạm header ở môi trường S3 from-source.

---

### Lỗi 2: `Python version mismatch: penv has 3.11, current interpreter is 3.12. Recreating penv...`

```
Python version mismatch: penv has 3.11, current interpreter is 3.12. Recreating penv...
```

**Nguyên nhân:** PlatformIO lưu trữ môi trường ảo Python (`penv`) tại:

```
Windows:  %USERPROFILE%\.platformio\penv
macOS/Linux: ~/.platformio/penv
```

Khi bạn cài/cập nhật Python (ví dụ từ 3.11 lên 3.12), `penv` cũ vẫn được tạo bởi Python 3.11 nhưng bây giờ PlatformIO đang chạy bằng Python 3.12. Nó cố "recreate" penv nhưng thường thất bại ở bước cài dependency (xem lỗi 3, 4).

**Cách fix (áp dụng theo thứ tự):**

#### Bước 1 — Xóa penv và để PlatformIO tái tạo sạch

```powershell
# Xóa môi trường ảo Python của PlatformIO (an toàn — sẽ được tạo lại)
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\penv"

# Chạy lại build — PlatformIO sẽ tạo penv mới khớp với Python hiện tại
pio run -e esp32dev
```

#### Bước 2 — Đảm bảo chỉ một Python phiên bản trên PATH

Nếu có cả Python 3.11 và 3.12 cùng thời điểm:

```powershell
py -0p    # Liệt kê các bản cài Python khả dụng

# Đặt Python mặc định cho PlatformIO bằng cách dùng phiên bản cụ thể:
# Cách A: Dùng py launcher — tạo shortcut pio.bat
echo 'py -3.12 -m platformio '%*' > %USERPROFILE%\pio312.bat

# Cách B: Thêm Python desired vào đầu PATH hoặc dùng where python
where python
```

Nếu PlatformIO được cài qua VS Code extension, extension sẽ dùng Python được chọn trong VS Code ("PlatformIO: Select Interpreter"). Đảm bảo chọn một interpreter duy nhất và ổn định.

#### Bước 3 — Cài lại PlatformIO từ đầu (nếu Bước 1–2 không cứu được)

```powershell
# Gỡ cài đặt
pip uninstall platformio -y

# Dọn cache toàn bộ
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\penv", "$env:USERPROFILE\.platformio\.cache"

# Cài lại (dùng Python phiên bản muốn dùng)
py -3.12 -m pip install --upgrade pip
py -3.12 -m pip install platformio

# Build lại
pio run -e esp32dev
```

---

### Lỗi 3: `Error: uv installation via pip failed with exit code 106`

```
Error: uv installation via pip failed with exit code 106
Error: Failed to install Python dependencies into penv
```

**Nguyên nhân:** PlatformIO 6.1.x dùng `uv` (một trình quản lý gói Python nhanh) để cài các dependency Python vào `penv`. Exit code `106` thường có nghĩa là:

- **Directory not empty (`ENOTEMPTY`)** — thư mục `penv` bị "treo" nửa chín vì lần recreate trước đó bị gián đoạn.
- **Quyền truy cập** — trình chạy hiện tại không có quyền ghi/xóa trong thư mục `penv`.
- **Proxy/corporate firewall** — môi trường công ty chặn `uv` tải package từ PyPI.

**Cách fix:**

#### Bước 1 — Xóa penv + cache (giải pháp bắt buộc)

```powershell
# Dừng mọi tiến trình pio đang chạy (nếu có)
taskkill /f /im platformio* 2>$null

# Xóa penv và cache
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\penv" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\.cache" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\.cache\pacman" -ErrorAction SilentlyContinue

# Xóa cache pip/uv toàn cục (nếu có uv riêng)
pip cache purge 2>$null
uv cache clean 2>$null
```

#### Bước 2 — Cấp quyền & chạy lại

```powershell
# Mở PowerShell **không** ở chế độ Admin (tránh vấn đề quyền với penv)
# Đảm bảo thư mục .platformio có quyền ghi:
icacls "$env:USERPROFILE\.platformio" /grant %USERNAME%:F /T

# Chạy build lại — để platformio tái tạo penv sạch
pio run -e esp32dev
```

#### Bước 3 — Nếu do proxy/corporate firewall

```powershell
# Thiết lập biến môi trường proxy nếu cần
set http_proxy=http://user:pass@proxy:port
set https_proxy=http://user:pass@proxy:port

# Buộc PlatformIO dùng pip thay vì uv (nếu uv bị chặn)
pio config set general.penv_use_uv false

# Hoặc cài các dependency bằng pip trực tiếp
pip install -r "$env:USERPROFILE\.platformio\penv\requirements.txt"
```

#### Bước 4 — Nếu PlatformIO không tái tạo penv tự động

Đôi khi PlatformIO tự động cố gắng tạo penv nhưng bị lỗi nửa đường. Buộc tạo lại bằng tay:

```powershell
# Tạo penv thủ công bằng pip
python -m venv "$env:USERPROFILE\.platformio\penv"
& "$env:USERPROFILE\.platformio\penv\Scripts\activate"
pip install --upgrade pip
pip install -e "C:\path\to\platformio\core"   # hoặc pip install platformio
deactivate

# Sau đó chạy pio run
pio run -e esp32dev
```

---

### Lỗi 4: `Error: Failed to install Python dependencies into penv`

```
Error: Failed to install Python dependencies into penv
```

**Nguyên nhân:** Đây là hậu quả trực tiếp của lỗi 3 — PlatformIO không thể cài các dependency Python (như `esptool`, `openocd`, `adafruit-blinka`, ...) vào môi trường ảo. Kết quả là các công cụ biên dịch/linker (toolchain, esptool.py) không khả dụng.

**Cách fix:** Áp dụng toàn bộ các bước ở **Lỗi 3** (xóa penv + cache → chạy lại với quyền phù hợp → kiểm tra proxy). Sau khi `penv` được tạo lại thành công, build sẽ tự động tải toolchain và các package cần thiết.

**Kiểm tra sau khi fix:**

```powershell
# penv đang hoạt động → phiên bản Python ổn định
pio --version

# Xem penv đã cài dependency chưa
pio pkg list
```

---

## Cách xử lý nhanh tổng hợp (pha pháp)

Nếu gặy mọi lỗi trên cùng lúc, làm theo trình tự này:

```powershell
# 1. Dừng mọi tiến trình PlatformIO
taskkill /f /im platformio* 2>$null ; taskkill /f /im python* 2>$null

# 2. Xóa sạch penv + cache
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\penv" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\.cache" -ErrorAction SilentlyContinue

# 3. (Tùy chọn) Cài lại PlatformIO
python -m pip install --upgrade pip
pip install --upgrade platformio

# 4. Build lại
pio run -e esp32dev
```

Nếu `src_inc` warning vẫn hiện — **bỏ qua**. Đó là hành vi thiết kế và build sẽ thành công.

---

## Cấu trúc tham chiếu nhanhan

```
LuxDMX-v2/
├── platformio.ini          ← Cấu hình build (envs, build_flags, src_inc, extra_scripts)
├── extra_scripts.py        ← Tự sinh header PROGMEM + cố định PATH toolchain
├── src/
│   ├── main.cpp            ← Entry point mỏng (setup + loop wiring)
│   ├── cfg/                ← Engine cấu hình schema-driven
│   ├── drv/                ← Driver RMT/DMX, UART RX, GPIO
│   ├── core/               ← Merge engine, seqlock, RDM controller
│   ├── net/                ← Art-Net, sACN, Ethernet, WebSocket, OTA
│   ├── sys/                ← FreeRTOS tasks, LED, OTA
│   ├── app/                ← Menu encoder/button
│   ├── pages/              ← HTML giao diện web
│   ├── assets/             ← Hình ảnh, CSS
│   └── generated/          ← TỰ TẠO lúc build (PROGMEM headers)
├── include/                ← Header công cộng
├── templates/              ← Mặc định per-board (.ini)
├── lib/EmbeddedConfig/     ← Engine config có thể tái sử dụng
├── test/native/            ← Test round-trip cấu hình (host-side)
└── docs/locales/           ← README đa ngôn ngữ (vi, en, fr, ja)
```

### Các env build

| Env | MCU | Build | Mạng | Kênh ra |
|---|---|---|---|---|
| `esp32dev` | ESP32 | Precompiled | WiFi | 2 (W5500 opt-in) |
| `esp32s3dev` | ESP32-S3 | From-source | WiFi | 2 |
| `wt32eth01` | ESP32 | Precompiled | RMII Ethernet | 2 |
| `esp32s3_psram` | ESP32-S3 | From-source | WiFi | 2 (8MB PSRAM) |
| `esp32s3_n16r8_eth` | ESP32-S3 | From-source | W5500 SPI | **4** |

---

## Tham khảo

- [PlatformIO Core docs](https://docs.platformio.org/)
- [pioarduino platform (W5500 support)](https://github.com/pioarduino/platform-espressif32)
- README tiếng Việt: [`docs/locales/README.vi.md`](../locales/README.vi.md)
- README tiếng Anh: [`README.md`](../../README.md)
