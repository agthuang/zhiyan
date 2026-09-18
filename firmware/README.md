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

---

## 主机单测（无需硬件）

```bash
cd firmware/components/common/tests
make test
# 可选：导出屏预览到 docs/ui-preview/
make preview
```

---

## 屏幕

0.96" ST7735，横屏 **160×80**，深色空闲时钟 + 电量，默认背光约 25%。  
偏色/偏移时改 `handheld/main/board.h` 里的 `LCD_X_GAP` / `LCD_Y_GAP` / `LCD_MADCTL`。

接收端控制台默认 **UART0 @ 115200**（不是 OTG 口）；OTG 枚举后 CDC 也会打日志。

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
