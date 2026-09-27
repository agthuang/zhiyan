# 知言 — Firmware

ESP-IDF ≥ **5.1**（建议 5.3+）。目标芯片：**ESP32-S3**。

```text
firmware/
  components/common/   # 协议、UI、按键、HID 策略（可主机单测）
  handheld/            # 手持（Controller-RevB）
  receiver/            # USB 键盘 + 麦 + CDC
```

**编译 / 烧录 / 配对 / 校时（Mac + Windows）：** 见 [`docs/flashing.md`](../docs/flashing.md)。  
仓库总览与 GPIO：根目录 [`README.md`](../README.md)。  
键位网页：[`web/keymap/README.md`](../web/keymap/README.md)。  
AI 推屏：[`aihook/README.md`](../aihook/README.md)。

手持 Flash 按模组选 **4 / 8 / 16MB**（`flash_4mb` / `flash_8mb` / `flash_16mb`，或 `./tools/flash_handheld.sh` 自动识别）。细节见烧录文档 §4.0。

---

## 主机单测（无需硬件）

```bash
cd firmware/components/common/tests
make test
```

---

## 屏幕

0.96" ST7735，横屏 **160×80**，深色空闲时钟 + 电量，默认背光约 25%。  
偏色/偏移时改 `handheld/main/board.h` 里的 `LCD_X_GAP` / `LCD_Y_GAP` / `LCD_MADCTL`。

接收端控制台默认 **UART0 @ 115200**（不是 OTG 口）；OTG 枚举后 CDC 也会打日志。

---

## 蓝牙媒体（Beta）

开机按住 Yes ≥1.5s 进入。鸿蒙可用；苹果 / 安卓多数需自行映射键位。

---

## 授时

接收端没有电池 RTC：断电后时间会停在 NVS 里的旧值。需要先从电脑授一次时：

```bash
./tools/set_time.sh                 # 一次
./tools/set_time.sh --watch         # 插着接收端时自动保持
```

键位网页连接时也会自动授时。  
手持端连上接收端时（配对 ACK / 重新上线）会自动收下接收端当前时间；电脑给接收端授时后也会立刻推到手持。

---

## 接收端 CDC 速查

```text
K? / K mm:cc,... / K!     # 快捷键查询 / 设置 / 恢复默认
B? / B <0-255>            # 手持背光 PWM
I? / I 0|1                # 闲置息屏（约 20s）
T? / T<unix>              # 授时（并推到手持）
S?                        # S=linked,bat,flags,unix
P? / P!                   # 绑定手持查询 / 清绑定重配
O #RRGGBB WORD / O!       # OSD 推词 / 清除（WORD ≤ 8）
```

手持 **Yes+No** 同按约 2.5s 清配对。接收端开机约 45s 后锁定已绑定手持。
