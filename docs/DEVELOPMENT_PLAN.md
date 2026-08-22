# LuxDMX-v2 — Kế hoạch phát triển chuẩn hóa đến Advanced Feature Parity

**Phiên bản:** 1.0  
**Ngày lập:** 22-08-2026  
**Tác giả:** Manus AI  
**Phạm vi:** Firmware ESP-IDF/PlatformIO cho ESP32, WT32-ETH01 và ESP32-S3 + PSRAM; parity chức năng với LuũDMX/LuxDMX reference; **loại trừ toàn bộ schematic, PCB layout, BOM, Gerber, fabrication và electrical validation của PCB**.

---

## 1. Mục đích và cách sử dụng tài liệu

Tài liệu này là **development baseline** cho mọi thay đổi tiếp theo của LuxDMX-v2. Nó chuyển 16 GitHub issues thành một chương trình phát triển có thứ tự, dependency, work package, public contract, tiêu chí nghiệm thu, test evidence, tài liệu bắt buộc và điều kiện phát hành. Mỗi issue chỉ được đóng khi đáp ứng Definition of Done trong tài liệu này; việc đã có header, enum, config field, stub hoặc build thành công không được xem là đã hoàn tất.

Behavior của LuũDMX được sử dụng làm **reference knowledge**, còn kiến trúc ESP-IDF/C, giới hạn tài nguyên, board support và public contracts của LuxDMX-v2 được giữ làm implementation boundary. Không sao chép mù Arduino API hoặc giả định rằng một subsystem đã hoàn chỉnh chỉ vì tài liệu mô tả subsystem đó. Quy tắc này phù hợp với roadmap #16 và REFACTOR_PLAN: behavior phải có runtime consumer, test phù hợp và HIL khi chạm phần cứng [1] [2].

Tài liệu phải được đọc cùng `docs/SYSTEM_SPECIFICATION/`, `docs/REFACTOR_PLAN.md`, `codebase_index.md`, `Lessons_Learned.md` và issue tương ứng. Nếu code, spec và issue mâu thuẫn, áp dụng thứ tự ưu tiên ở mục 2; sau đó ghi decision record trong cùng commit với thay đổi behavior.

## 2. Nguồn sự thật và nguyên tắc bất biến

| Thứ tự | Nguồn | Vai trò | Cách xử lý khi có mâu thuẫn |
|---|---|---|---|
| 1 | Public safety/security contract đã được duyệt trong issue và spec | Bắt buộc đối với OTA, rollback, input validation, secrets, quyền điều khiển | Không được nới lỏng bằng implementation convenience |
| 2 | Các system specification 01–47 và `docs/websocket-protocol.md` | Định nghĩa port, timing, byte layout, state machine, config, error behavior | Code phải bám contract; nếu contract sai phải sửa spec và ghi lý do |
| 3 | Issue #1–#16 | Định nghĩa gap, phạm vi, acceptance criteria và dependency roadmap | Dùng để chia work package và đóng issue |
| 4 | `docs/REFACTOR_PLAN.md` | Định nghĩa phase order, component ownership và quality gates | Dùng làm kiến trúc và thứ tự triển khai |
| 5 | `codebase_index.md`, `Lessons_Learned.md` | Ghi nhận reality của code, các quyết định và pitfalls đã gặp | Phải cập nhật trong cùng commit behavior |
| 6 | Code và build manifest hiện tại | Ground truth của những gì thực sự tồn tại | Không giả định subsystem/documented file đã có nếu chưa kiểm tra |
| 7 | LuũDMX/reference repository | Nguồn tham khảo behavior, UX, test ideas và vận hành | Không sao chép framework hoặc kết luận phần cứng/PCB |

Các thông số nền tảng không được tự ý thay đổi gồm: Art-Net UDP 6454, sACN UDP 5568, HTTP/WebSocket TCP 80, mDNS hostname contract, DMX frame 513 byte, seqlock snapshot tối đa 8 lần, network task trên core 0, DMX TX task trên core 1, RDM response window 9 ms, ArtSync grace 1000 ms, sACN Stream-Sync grace 500 ms và sender failsafe mặc định 2500 ms. Các giá trị này được truy vết trong system specification index và các spec chuyên ngành [3].

## 3. Baseline hiện tại và mục tiêu cuối

Tại thời điểm lập kế hoạch, repository có 16 issue trong milestone `Advanced feature parity (PCB excluded)`. Issue #1 đã có runtime verifier/recovery, native policy test và host signing tools ở `main`, nhưng vẫn còn production-key provisioning, HIL và workflow signing cần hoàn tất. Các issue #2–#16 vẫn là backlog chức năng hoặc quality gate và không được đánh dấu hoàn tất trước khi có evidence tương ứng [4].

| Trạng thái được phép ghi trong `codebase_index.md` | Ý nghĩa bắt buộc |
|---|---|
| `Missing` | Chưa có implementation hoặc public contract |
| `Scaffold/schema-only` | Có header/config/stub nhưng chưa có runtime consumer đáng tin cậy |
| `Implemented host-only` | Logic chạy trên native/mock nhưng chưa chứng minh trên target |
| `Implemented + exercised` | Runtime đã tích hợp, test phù hợp đã xanh và không còn acceptance gate bắt buộc |
| `Hardware-verified` | Đã có HIL evidence cho timing, packet, reboot, heap hoặc electrical behavior tương ứng |
| `Release-ready` | Đã qua toàn bộ PR/tag/release gates, security review và artifact verification |

Mục tiêu cuối của roadmap là một firmware gateway có data plane DMX ổn định, RDM physical/gateway hoạt động, Ethernet/WiFi runtime đúng policy, scene/loss modes thực thi, WebSocket/REST/UI phản ánh trạng thái thật, OTA có ký và rollback, release có manifest/checksum/signed artifact, cùng test matrix chứng minh parity. “Parity-complete” chỉ được công bố khi #12 có coverage matrix cho các behavior mục tiêu và #16 được cập nhật [1].

## 4. Dependency graph và thứ tự triển khai bắt buộc

```text
M0 Governance/tooling baseline
 ├── #12 test infrastructure bootstrap (xuyên suốt)
 └── #1 secure OTA gate ───────────────┐
                                      │
M1 Physical data plane                 │
 └── #2 RDM transport → #3 Art-Net RDM gateway
                                      │
M2 Runtime correctness                 │
 ├── #4 Ethernet runtime ──→ #15 Art-Net IP programming
 ├── #5 runtime/build metadata ───────┼──→ #9 OTA GitHub/URL orchestration
 └── #11 per-output DMX scheduler      │
                                      │
M3 Operator surface                    │
 ├── #6 live WebSocket diagnostics     │
 ├── #7 diagnostic REST/operator API ← #2/#3/#4
 ├── #8 scene engine/loss modes ← #11
 └── #10 display/on-unit controls (song song, không phụ thuộc PCB)
                                      │
M4 Release ecosystem                   │
 ├── #13 unified frontend assets       │
 └── #14 hosted browser flasher ← #5/#9/#13
                                      │
M5 Final verification                  │
 └── #12 full protocol/e2e/HIL gate → #16 parity closeout
```

Dependency không có nghĩa các nhóm phải tuần tự tuyệt đối ở cấp file. Có thể phát triển song song những work package không chia sẻ ownership, nhưng merge vào branch chính phải tuân theo gate. Cụ thể, #10 có thể bắt đầu sau khi board capability contract được chốt; #13 có thể làm song song với #6–#10; #12 phải bootstrap sớm nhưng chỉ đóng sau khi toàn bộ feature matrix có test evidence.

## 5. Roadmap theo milestone

### M0 — Governance, repository truth và test bootstrap

M0 không tạo thêm behavior người dùng; nó làm cho quá trình phát triển deterministic. Trước khi viết feature mới, phải đồng bộ `AGENTS.md` với branch/repository reality hiện tại, xác nhận ESP-IDF version thực tế, lập board capability/pin table canonical, xác nhận partition table dual-slot, kiểm tra generated-file policy và định nghĩa trạng thái parity register. Các file generated như `sdkconfig.*`, build output và managed components không được hand-edit ngoài cơ chế documented; rollback defaults phải đi qua cấu hình tracked có thể tái tạo được.

| Work package | Kết quả cần giao | Gate |
|---|---|---|
| M0.1 Repository truth | Cập nhật `AGENTS.md`, `codebase_index.md`, component map và ESP-IDF/PlatformIO version | Không còn hướng dẫn mâu thuẫn với code/build hiện tại |
| M0.2 Board contract | Một bảng board/pin/peripheral canonical cho 3 target; capability flags rõ ràng | Mọi driver lấy default từ cùng một nguồn |
| M0.3 Test bootstrap | Native runner, CTest, host shims, fixture helpers và naming convention | Read-only native gate chạy được trên PR |
| M0.4 CI baseline | PR lint/docs/native/three firmware builds; artifact log khi fail | Không merge nếu gate đỏ |
| M0.5 Parity register | Bảng issue → spec → code → test → HIL → release status | Không có claim “done” thiếu evidence |

**Exit M0:** native suite xanh; ba development firmware profiles build xanh; generated-file check deterministic; branch policy và commit policy được ghi trong docs.

### M1 — Physical data plane: #2 và #3

M1 phải chứng minh RDM không còn là wire-format-only scaffold. #2 được làm trước vì #3 cần transport thật để relay ArtRdm/Tod. RDM task chạy trên core 1 với priority theo spec; network/web chỉ enqueue request và đọc response bounded. Không được để RDM transaction block DMX transmit task hoặc kéo heap allocation vào hot path.

#### #2 — Physical RDM transport

| Hạng mục | Tiêu chuẩn implementation |
|---|---|
| API | Tạo `rdm_transport` interface với `init`, `send`, `receive`, `cancel/reset`, `get_status`; mọi API có length, timeout và error out-parameter |
| TX | RMT tạo break/MAB/data đúng contract; hỗ trợ 8N2/250 kbaud và start code RDM; không dùng delay busy-loop trong task critical |
| RX | UART RX-only thu response; bảo vệ short frame, overrun, framing error và timeout |
| Half duplex | DE/RE GPIO có pre-TX, post-TX và settle khoảng 90 µs theo spec; trạng thái bus luôn fail-safe khi task dừng |
| Transaction | TX → DE release → RX window khoảng 9 ms; checksum kiểm tra trước khi đưa response vào queue; retry policy bounded |
| Scheduling | RDM priority 18/core 1; DMX TX priority 19/core 1; network enqueue core 0; lock duration bounded |
| Persistence/UI | Fixture table bounded, UID/address/label/capability rõ ràng; UI chỉ expose capability khi transport healthy |

Test phải bao phủ loopback/fixture simulator, valid GET/SET, discovery, timeout, collision, checksum error, short reply, bus unavailable, queue overflow và reset giữa transaction. HIL phải lưu timestamp TX/RX, capture waveform, turnaround latency, error count và ảnh/trace fixture simulator. **Gate #2:** discovery và GET/SET chạy trên ít nhất một fixture thật hoặc rig đáng tin cậy; DMX frame rate không giảm quá ngưỡng đã định trong lúc RDM hoạt động [5] [6].

#### #3 — Art-Net RDM gateway và response queue

Implement opcode validation cho ArtRdm, ArtTodRequest/Control theo decision record, ArtRdm request/response mapping, bounded response queue, source/sequence preservation, NACK/error behavior và ArtPollReply metadata runtime. Queue phải có overflow counter, drop policy, duplicate/late response handling và correlation ID hoặc sequence đủ để ghép response với request. Không được thay đổi DMX data plane khi thêm RDM gateway.

Acceptance gồm ArtTod discovery và ArtRdm GET/SET round-trip qua simulator, response queue overflow test, WiFi/Ethernet packet test, runtime node metadata không còn ESTA placeholder, và policy rõ ràng cho ArtAddress/ArtIpProg. #3 chỉ được đóng sau #2; `/rdm.json`, RDM operator routes và UI liên quan vẫn phải feature-gated cho tới khi cả hai gate xanh [7].

### M2 — Network và runtime correctness: #4, #5, #11, #15

M2 bảo đảm các option config có runtime consumer thật và scheduler không phụ thuộc tình cờ vào tốc độ packet. Network abstraction phải tách link management khỏi protocol parser, còn DMX scheduler phải tách source clock khỏi per-output wire clock.

#### #4 — Ethernet RMII/W5500 và link-loss policy

Tạo interface thống nhất cho WiFi, RMII LAN8720 và SPI W5500: init, DHCP/static, hostname, link events, reconnect, teardown/re-init và health counters. Runtime selection phải dựa trên board capability/config; không khởi tạo peripheral không tồn tại. Link loss không được làm mất DMX output đang chạy; policy phải xác định rõ khi nào retry, fallback WiFi/AP, giữ last frame, reset network hoặc reboot.

Work package bắt buộc gồm PHY reset/clock validation, event-to-net-state mapping, DHCP timeout 30 s, link timeout 15 s, exponential backoff có cap, cable pull/reconnect, static IP persistence và `/info.json` counters. HIL phải chạy trên WT32-ETH01 và W5500 target với Art-Net/sACN, đo reconnect time, heap, packet loss và output continuity. PCB vẫn ngoài phạm vi; chỉ kiểm tra board runtime trên phần cứng được cung cấp [8] [9].

#### #5 — Runtime/build metadata

Tạo một `firmware_version`/build metadata contract lấy version, git commit, build profile, board, build timestamp hoặc reproducible identifier từ CI/compile flags và `esp_app_desc`, không hardcode trong web route. JSON serializer phải escape đúng, không lộ PSK/OTA password/webhook secret, và tất cả response phải phản ánh phase/recovery/network thật.

Mỗi profile phải trả đúng board/profile/version/commit. `/version.json` phải có OTA phase thật; `/info.json` phải có PSRAM, network backend, recovery state, feature flags và counters. Test schema phải kiểm tra missing/long strings, escaping, secret masking và mismatch giữa dev/release build. #5 phải xong trước #9/#14 vì manifest và browser flasher cần nhận diện artifact an toàn [10].

#### #11 — Per-output DMX rate, Delta/Continuous và failsafe

Thiết kế scheduler mỗi output độc lập với rate 40, 41.7, 33.3, 25 và 20 fps theo contract; output này không được block output khác. Continuous phát theo clock cấu hình; Delta phát khi frame/source thay đổi; silence fallback sau 800 ms; RDM tick không phụ thuộc số DMX frame. RMT idle/hold/stop phải được định nghĩa để không phát waveform ngoài policy.

Test native phải kiểm tra deadline calculation, rollover, independent clocks, style transitions và loss modes. HIL dùng logic analyzer/oscilloscope để đo break/MAB/slot/frame period với tolerance công bố. Phải test Hold, Zero, Stop, Preset, Home khi source mất, recovery khi source trở lại và live/reboot config semantics. Không đóng #11 nếu chỉ có helper tính rate mà chưa có consumer scheduler [11].

#### #15 — Art-Net control và secure remote IP programming

Tách read-only inquiry khỏi write path. ArtAddress/ArtIpProg write phải off mặc định, feature flag rõ ràng, validation chặt, rate-limited và không phản hồi khi capability bị tắt. Persist static/DHCP/hostname vào NVS, áp dụng ở boot kế tiếp hoặc theo policy được ghi rõ; không reset network giữa transaction gây mất DMX ngoài dự kiến.

Security contract phải cảnh báo Art-Net không có authentication native. Write command chỉ được cho phép trong provisioning/explicit operator policy, có audit log, source validation và test replay/malformed packet. ArtPollReply Status2, port types, runtime IP, board name và firmware metadata phải lấy từ state thật. #15 cần #4 nếu validation dùng Ethernet HIL [12].

### M3 — Operator và diagnostic surface: #6, #7, #8, #10

M3 chỉ expose operator capability khi backend đã functional. Tất cả HTTP/WebSocket payload lớn phải bounded hoặc streamed; read-only route và write route phải tách trong test, có no-store/cache policy thích hợp và không làm lộ secret.

#### #6 — Live WebSocket diagnostics và browser commands

Chốt schema binary frame đúng layout 2095 byte, big-endian, sequence, full/delta snapshot, changed-universe bitmap, RDM tail, sender/FPS/conflict/jitter/log metadata và tối đa 12 client. `ws_frame.c` là serializer thuần, không tự đọc singleton; handler chịu subscription, client lifecycle, backpressure, reconnect và command authorization.

Work package gồm: nối DMX buffer/stats/sender tracker; changed bitmap; full frame sau reconnect; delta suppression; bounded send queue; slow-client drop; sequence skip; command parser với allowlist; command-result relay; manual/blackout/identify policy. Test phải kiểm tra từng byte/offset/endian, 512 channel, delta không gửi sai universe, reconnect, nhiều client soak, malformed command và low-heap. E2E browser dùng native mock trước khi flash target [13] [14].

#### #7 — Diagnostic REST, labels, RDM và operator routes

Lập API table cho từng route, gồm method, path, auth/feature gate, request schema, response schema, status codes, max payload, side effects và audit behavior. Triển khai từng nhóm theo thứ tự: health/version/info; DMX/sender/log/metrics; labels; network/config; RDM inventory/operation; operator controls. `/rdm.json` chỉ xuất hiện khi RDM transport và gateway pass gate.

`/dmx.json` phải chịu được low heap và nhiều client; log/labels có persistence bounded, truncation và escaping; write routes phải validate range, method, content length, CSRF-equivalent local policy nếu cần và reboot semantics. Read-only E2E chạy mỗi PR; write tests opt-in hoặc HIL. Không tạo route “trang trí” cho backend chưa tồn tại [15].

#### #8 — Scene engine và loss modes

Thay `scene_stub.c` bằng scene storage bounded, schema version, checksum hoặc atomic record, index validation, home scene, preset recall, fade theo 1 ms tick và output scope. Scene apply phải đi qua seqlock/merge boundary, không viết đồng thời vào live DMX buffer. Loss policy phải có precedence rõ giữa Hold/Zero/Stop/Preset/Home, sender timeout và recovery.

Test gồm empty scene, invalid index, corrupted NVS, power-cycle persistence, concurrent loss/recovery, fade interrupted by new frame, multiple outputs và no-block DMX schedule. Nếu một mode chưa thể implement đúng, phải ẩn enum/config/UI hoặc ghi formal cut decision; không để config “functional-looking” nhưng chỉ log warning [16].

#### #10 — Display, LED status và on-unit controls

Tách HAL cho display, encoder, buttons và LED status. Board capability flags phải cho phép build target không có display/encoder; code không được truy cập GPIO không tồn tại. Encoder decode phải chống bounce, xử lý CW/CCW/press/long-press và event queue bounded. Menu chỉ thay đổi field có quyền, hiển thị live/reboot/secret semantics.

LED state machine phải thống nhất network connected, AP, DMX activity/loss, RDM transaction, identify, error, OTA verify/reboot và recovery. Display refresh 200 ms không được chặn DMX/RDM. Test native kiểm tra event mapping/debounce; HIL kiểm tra input thực và display/LED timing. Không đưa PCB-specific claim vào DoD [17] [18].

### M4 — Secure OTA và release ecosystem: #1, #9, #13, #14

#### #1 — Secure OTA signing và boot-retry recovery

#1 là safety baseline trước production OTA. Local upload và future fetch phải dùng cùng transaction contract: stream vào inactive partition, `esp_ota_end`, validate size/layout, hash partition theo chunk 1 KiB, đọc signature 64 byte cuối, verify PSA PureEdDSA trên SHA-256 của image không gồm signature, sau đó mới commit boot target. Mọi failure phải giữ running partition, clear/abort staging phù hợp và expose error có thể chẩn đoán.

ESP-IDF pending-verify/rollback lifecycle dùng NVS `dmxgw/boottry`, cho phép đúng ba pending boots, mark healthy sau service graph và bounded stability window, rollback khi vượt cap. Development bypass phải explicit; release profile fail closed. Production public key phải được provision thật; private key chỉ ở secret store. Native test không được giả vờ chứng minh reboot/HIL. DoD còn mở cho đến khi có HIL signed image, bad signature và crash/reboot recovery [19] [20].

#### #9 — GitHub/URL OTA fetch và versioned orchestration

Thiết kế worker background không block HTTP, WebSocket, DMX hoặc RDM. Các endpoint `/ota/github` và `/ota/url` cần schema/error/status contract, allowlist/HTTPS/certificate policy, timeout, redirect policy, max image size, zero-size/short-read handling, cancellation và single-flight lock. Release selection phải kiểm tra board, profile, version, manifest checksum và signature trước reboot.

GitHub source cần pin repository/tag/asset policy, tránh tải arbitrary artifact. URL source phải HTTPS mặc định, allowlist rõ và không follow redirect sang host ngoài policy. Auto-update là opt-in, có backoff, cooldown, rollback-aware status và không được tự bật trong dev. Test dùng local HTTP fixture cho success/tamper/timeout/short-read/HTTP error; HIL kiểm tra reboot và boot target [21].

#### #13 — Unified frontend assets

Chọn `webui/` làm canonical source hoặc quyết định chính thức khác; tuyệt đối không duy trì hai source có thể lệch. Generator phải deterministic: cùng input tạo byte-identical C arrays/filesystem assets, gzip metadata ổn định, sort input, không phụ thuộc timestamp/máy build. Generated output có header marker và CI check bằng cách regenerate rồi `git diff --exit-code`.

HTML/JS/CSS phải được syntax-check/lint; bundle size và firmware flash/heap delta phải được report. Các route `/`, `/config`, `/setup`, `/firmware` phải phục vụ đúng bundle. UI test dùng browser mock REST/WS; firmware test xác nhận generated asset mapping, content type, gzip/cache headers và no-copy heap behavior [22].

#### #14 — Hosted browser flasher và release smoke tests

Hosted flasher chỉ consume release artifact đã live, không build lại firmware trong browser. Manifest phải chứa board, profile, version, commit, image path, byte size, SHA-256, partition/bootloader metadata và signed status. UI bắt buộc map board/profile chính xác, không cho chọn nhầm image, hiển thị checksum/progress/error và giữ `/firmware` device-local độc lập.

Pages deploy chỉ sau signed release package, manifest validation và smoke tests. Smoke suite kiểm tra HTTP 200, asset presence, manifest schema, checksum, content type, no accidental secret, correct board/profile selection và fallback khi latest release thiếu asset. #14 phụ thuộc #5, #9 và #13 [23].

### M5 — Verification infrastructure và parity closeout: #12 và #16

#12 là workstream xuyên suốt, nhưng chỉ đóng sau M4. Test infrastructure có bốn tầng: native unit/policy; protocol E2E; device/Unity; HIL/soak. Mỗi tầng có mục đích khác nhau và không được dùng shim để tuyên bố hardware pass.

| Tầng | Chạy ở đâu | Bắt buộc kiểm tra | Khi nào chạy |
|---|---|---|---|
| T1 Native | Linux host/CTest | Pure policy, parser, serializer, config, queue, error paths | Mỗi PR |
| T2 Protocol E2E | Host fixture/network namespace | UDP Art-Net/sACN, HTTP, WS, OTA fixture, manifest | Mỗi PR read-only; write test theo label |
| T3 Device | ESP-IDF/Unity trên target | NVS, FreeRTOS timing, heap, reset, driver binding | PR/release candidate khi runner có board |
| T4 HIL/soak | Bench fixture, logic analyzer, Ethernet/WiFi rig | DMX/RDM waveform, packet interoperability, link loss, OTA reboot/rollback, 60 s+ soak | Release candidate và sau driver/timing change |

Mỗi test case phải ghi precondition, input, expected output, timeout, cleanup, artifact và failure diagnosis. Packet/timing tests phải lưu pcap/logic trace; firmware tests phải lưu board/profile/commit/sdkconfig summary; reboot tests phải lưu serial log từ trước update đến recovery. Fuzz tests áp dụng cho packet length/opcode/string/JSON; property tests áp dụng serializer/parser round-trip và checksum.

Coverage matrix của #12 phải map tối thiểu: mỗi acceptance bullet của #1–#15; mỗi state/error branch; mỗi spec timing constant; mỗi board/profile; mỗi feature gate. Không cho phép chỉ số coverage cao che mất một acceptance branch chưa được test.

#16 là release governance issue. Khi #1–#15 có evidence, cập nhật parity register, đóng các issue theo dependency, chạy full matrix, review unresolved OQ và lập roadmap PCB riêng nếu cần. #16 không tự động bao gồm PCB; electrical validation chỉ được mở trong issue sau [1].

## 6. Definition of Done chung cho từng issue

Một issue chỉ được đóng khi tất cả điều kiện sau đều đạt và được liên kết trong issue comment hoặc PR:

| Nhóm DoD | Điều kiện |
|---|---|
| Contract | Public API, state machine, bounds, timing, error codes/status và feature flags được ghi trong spec |
| Implementation | Runtime consumer hoàn chỉnh; không còn stub/log-only trên đường đi chính |
| Negative behavior | Malformed input, timeout, overflow, unavailable hardware, low heap và recovery path đã có test phù hợp |
| Integration | Chạy đúng ownership/lifecycle/core affinity; không phá subsystem hiện hữu |
| Testing | Native + protocol/device test tương ứng; HIL bắt buộc nếu đụng timing/peripheral/reboot |
| Security | Secrets masked, write path gated, URL/OTA validation, fail-closed và audit/log policy được review |
| Documentation | Spec, `codebase_index.md`, `Lessons_Learned.md`, user/operator docs cập nhật trong cùng logical commit |
| Build | Ba target board build xanh; release profile và generated-file gate xanh khi liên quan |
| Evidence | Log, pcap, waveform, heap, firmware metadata hoặc screenshot được lưu tùy acceptance criterion |
| Release status | Parity status chuyển đúng từ scaffold/host-only sang exercised/hardware-verified; không overclaim |

## 7. Tiêu chuẩn kiến trúc và coding

### 7.1 Ownership và dependency

`main/` chỉ wiring/lifecycle; không chứa protocol hoặc business logic. `lux_common` chỉ contract/header/pure helper. `lux_config` sở hữu schema, NVS namespace và migration. `lux_core` sở hữu DMX buffer, router, merge, scene, sender và RDM logic. `lux_drv` sở hữu RMT/UART/GPIO/display/LED. `lux_net` sở hữu socket, HTTP/WS, Ethernet state và OTA. `lux_sys` là nơi duy nhất tạo task và sở hữu crash guard/logging/alerts/soak. Components không được gọi ngược qua implementation private; dùng header contract và dependency list trong CMake.

Mọi task phải có owner, core, priority, stack budget, period/deadline, queue depth, stop/restart behavior và health counter. DMX/RDM timing-critical code không gọi malloc, filesystem, JSON serialization hoặc network I/O. Network/HTTP handlers chỉ validate/enqueue; worker xử lý phần nặng ngoài callback.

### 7.2 Memory, concurrency và error handling

Buffers cố định và bounded được ưu tiên. Mọi allocation phải nêu owner, lifetime, upper bound, failure behavior và cleanup. Seqlock chỉ bảo vệ vùng dữ liệu đã quy định; không dùng seqlock để thay mutex cho side effects. Queue overflow phải có counter và policy; không silent drop nếu acceptance yêu cầu observability.

Hàm C trả `esp_err_t` hoặc bool kèm error context; không nuốt lỗi quan trọng. ISR chỉ làm thao tác ISR-safe và notify/queue; không log nặng, cấp phát hoặc gọi API blocking. Mọi shared state có memory ordering được ghi rõ. Integer width, endian, signed wraparound và length validation phải được xử lý tường minh.

### 7.3 Configuration và compatibility

Mọi config field phải có type, range, default, `CFG_LIVE`/`CFG_REBOOT`, `CFG_SECRET`/`CFG_NOWEB`/`CFG_KEEPNE`, NVS key và migration rule. NVS namespace authoritative là `dmxgw`; migration phải idempotent và có version. Runtime phải đọc config thông qua schema/API, không đọc key rải rác. Capability không có trên board phải bị loại khỏi UI hoặc trả trạng thái unsupported; không ghi config “ảo”.

### 7.4 Security baseline

Mọi input mạng được coi là hostile: validate method, content length, string termination, opcode, source, sequence, range, timeout và replay/duplicate behavior. Secrets không xuất hiện trong log, JSON, crash dump, artifact, pcap hoặc CI output. Art-Net write control phải off mặc định; OTA production fail closed; private signing key không bao giờ nằm trong Git. Mỗi security-sensitive change cần threat note và negative test.

### 7.5 Commit và review

Mỗi commit chỉ chứa một logical feature hoặc một enabling tool. Commit phải xanh ở test/build gate tương ứng, có spec/codebase/lessons update nếu behavior thay đổi. Không commit generated output bằng tay nếu chưa chạy generator; không dùng WIP commit trên branch chính. PR description phải có scope, dependency issue, test commands, board matrix, risk, rollback plan và evidence links.

## 8. Tiêu chuẩn test và release gate

### 8.1 PR gate

Mỗi pull request chạy formatting/lint, Markdown/link hygiene, generated assets check, native CTest sạch, host tools test và ba development firmware builds. Nếu thay đổi Kconfig, CMake source list, partition, networking, RMT/UART, OTA hoặc task priority thì chạy clean build liên quan. Nếu thay đổi public protocol, phải thêm vector test hoặc fixture.

### 8.2 Release-candidate gate

Release candidate chạy full dev + release profile matrix, protocol E2E, device smoke, config migration, power-cycle, network cable pull, WebSocket multi-client soak, DMX analyzer, RDM fixture simulator/bench và OTA signed/bad-signature/recovery. Mỗi board phải có serial log, firmware metadata, heap snapshot và test result.

### 8.3 Tag/release gate

Tag release chỉ được tạo artifact sau khi source commit đã pass CI. Signed profile được build riêng, signing key lấy từ protected secret, public key match được kiểm tra, signature được verify round-trip, manifest/checksum được tạo và artifact không chứa private key. Hosted Pages chỉ deploy sau package validation. Release notes phải nêu board/profile compatibility, upgrade path, rollback behavior, known limitations và test evidence.

### 8.4 Không được dùng các shortcut sau

Không dùng native shim để claim RMT waveform, UART turnaround, Ethernet link, display/LED electrical behavior hoặc reboot rollback. Không đóng issue vì compile success, route tồn tại, config field tồn tại, header tồn tại, UI có nút hoặc test chỉ kiểm tra happy path. Không thay đổi timing constant để làm test pass mà không cập nhật spec, đo lại và có decision record.

## 9. Rủi ro và biện pháp kiểm soát

| Rủi ro | Dấu hiệu sớm | Biện pháp bắt buộc | Owner/gate |
|---|---|---|---|
| RDM làm trễ DMX | Frame jitter/tần số giảm khi discovery | Core/priority separation, bounded queue, analyzer | #2/#12 |
| Ethernet option chỉ là schema | Board build pass nhưng không có packet | Runtime service + cable-pull HIL + counters | #4 |
| Release artifact sai board | Manifest/profile mismatch | #5 metadata contract + #14 selector test | #5/#14 |
| OTA brick thiết bị | Bad image set boot target hoặc boot loop | Verify-before-commit + pending verify + HIL rollback | #1/#9 |
| Key leakage | Private key trong log/artifact/git | Protected secret, scan artifact, runbook, fail CI | #1/#14 |
| Frontend drift | Embedded page khác `webui` | Deterministic generator + diff check | #13 |
| Low-heap reset | Multi-client/large JSON làm crash | Payload bounds, soak, heap watermark, backpressure | #6/#7/#12 |
| Config migration mất dữ liệu | Power cycle/legacy key mismatch | Versioned idempotent migration + fixture NVS | #5/#8/#12 |
| Stub bị overclaim | UI/API bật trước backend | Capability gate + parity register | Tất cả |
| Generated config drift | Local build khác CI | Tracked defaults/generator, clean rebuild | M0/#14 |
| PCB scope creep | Issue kéo vào schematic/layout | Giữ milestone boundary; mở roadmap PCB riêng | #16 |

## 10. Checklist triển khai cho mỗi work package

Trước khi code, người thực hiện phải viết một mini design note gồm mục tiêu, spec anchors, state/data model, API, ownership, timing, memory, error matrix, security impact, test plan và rollback plan. Trong khi code, phải thêm hoặc sửa test cùng logical change, cập nhật CMake/Kconfig/generator đúng ownership và tránh public API giả.

Trước khi merge, reviewer kiểm tra diff theo checklist: không có secret; không có unbounded input/allocation; task/core/priority đúng; error path không bị overwrite; route/enum chỉ expose khi backend functional; generated file reproducible; docs cùng commit; native test và firmware build đúng matrix; HIL label được đặt nếu cần. Sau merge, cập nhật parity register và issue comment với commit/evidence/status còn mở.

## 11. Trình tự đóng issue đề xuất

| Thứ tự | Issue | Điều kiện mở/đóng | Kết quả milestone |
|---:|---|---|---|
| 0 | #12 bootstrap | M0 test runner/fixture cơ bản trước mọi driver mới | Test infrastructure available |
| 1 | #1 | Runtime đã có; cần production key + HIL để đóng hẳn | Safety baseline |
| 2 | #2 | Không phụ thuộc #3; cần fixture/transport HIL | Physical RDM transport |
| 3 | #3 | Chỉ sau #2 | Art-Net RDM gateway |
| 4 | #4 | Board capability/pin map rõ | Ethernet runtime |
| 5 | #5 | Có build metadata injection | Reliable identity |
| 6 | #11 | Buffer/RMT baseline ổn | Deterministic DMX output |
| 7 | #15 | #4 nếu viết path cần Ethernet | Controlled Art-Net management |
| 8 | #6 | Data/stats contracts từ core/net ổn | Live diagnostics |
| 9 | #7 | #2/#3/#4 health gates | Operator REST surface |
| 10 | #8 | #11 scheduler và merge/loss boundary ổn | Functional scenes/failsafe |
| 11 | #10 | Board capability contract ổn; có thể song song M3 | On-unit operation |
| 12 | #9 | #1 + #5 + fixture tests | Remote OTA orchestration |
| 13 | #13 | Frontend canonical source được quyết định | Deterministic embedded UI |
| 14 | #14 | #5 + #9 + #13 + signed release | Hosted release flasher |
| 15 | #12 final | Coverage matrix toàn bộ behavior | Verification gate |
| 16 | #16 | Tất cả DoD và parity register complete | Advanced parity closeout |

## 12. Deliverables bắt buộc của chương trình

Khi roadmap hoàn tất, repository phải có: component map không mâu thuẫn; canonical board table; tracked build defaults; deterministic frontend generator; native/protocol/device/HIL test runners; protocol fixtures; OTA signing runbook và protected CI; release manifest/checksum; parity register; upgrade/rollback guide; và decision records cho mọi Open Question từng ảnh hưởng acceptance.

Mỗi feature release phải có một bảng traceability dạng `issue → spec → implementation files → tests → HIL evidence → release artifact`. Bảng này là điều kiện bắt buộc để phân biệt “implemented” với “verified”, đồng thời là cơ sở để sau này mở roadmap PCB mà không trộn lẫn feature firmware với electrical design.

## 13. References

[1]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/16 "LuxDMX-v2 issue #16 — Advanced feature parity roadmap"
[2]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/REFACTOR_PLAN.md "LuxDMX-v2 REFACTOR_PLAN.md"
[3]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/SYSTEM_SPECIFICATION/INDEX.md "LuxDMX-v2 system specification index"
[4]: https://github.com/npthinhit-bit/LuxDMX-v2/issues "LuxDMX-v2 GitHub issues"
[5]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/2 "Issue #2 — Physical RDM transport"
[6]: https://github.com/tombueng/LuxDMX/tree/master/RDM "LuũDMX RDM transport and rig reference"
[7]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/3 "Issue #3 — Art-Net RDM gateway"
[8]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/4 "Issue #4 — Ethernet runtime and link-loss policy"
[9]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/SYSTEM_SPECIFICATION/31-ethernet-spec.md "LuxDMX-v2 Ethernet specification"
[10]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/5 "Issue #5 — Runtime and build metadata"
[11]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/11 "Issue #11 — DMX rate, Delta/Continuous and failsafe"
[12]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/15 "Issue #15 — Art-Net control and IP programming"
[13]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/6 "Issue #6 — Live WebSocket diagnostics"
[14]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/SYSTEM_SPECIFICATION/23-websocket-protocol-spec.md "LuxDMX-v2 WebSocket protocol specification"
[15]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/7 "Issue #7 — Diagnostic REST and operator routes"
[16]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/8 "Issue #8 — Scene engine and loss modes"
[17]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/10 "Issue #10 — Display, LED and on-unit controls"
[18]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/SYSTEM_SPECIFICATION/36-led-status-spec.md "LuxDMX-v2 LED status specification"
[19]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/1 "Issue #1 — Secure OTA signing and boot-retry recovery"
[20]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/SYSTEM_SPECIFICATION/30-ota-sign-spec.md "LuxDMX-v2 signed OTA specification"
[21]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/9 "Issue #9 — GitHub/URL OTA orchestration"
[22]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/13 "Issue #13 — Deterministic frontend assets"
[23]: https://github.com/npthinhit-bit/LuxDMX-v2/issues/14 "Issue #14 — Hosted browser flasher"
