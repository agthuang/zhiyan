# 知言 — 固件编译与烧录（Mac / Windows）

把手持端、接收端都刷上本仓库固件，并完成首次配对。  
两条板子都是 **ESP32-S3**，推荐用官方 **ESP-IDF**（≥ 5.1，建议 5.3+）。

```text
手持端 ──ESP-NOW──► 接收端 ──USB──► 电脑（键盘 + 麦克风 + 串口）
```

| 板子 | Flash | 平时连电脑的口 | 烧录时注意 |
|------|-------|----------------|------------|
| 手持端 `firmware/handheld` | **4 / 8 / 16MB**（按模组选配置，见 §4.0） | USB（供电 / 调试，USB-Serial/JTAG） | Voice 键 = **GPIO0 = BOOT**，进下载模式或复位时别误按住 |
| 接收端 `firmware/receiver` | **4MB** | **USB OTG**（枚举成 Zhiyan Receiver） | 调试串口是 **UART0 @ 115200**，不是 OTG 口 |

---

## 0. 你需要准备什么

**硬件**

- 手持板 + 接收板（已焊好、能上电）
- 两条可用的 USB 数据线（能传数据，别用纯充电线）
- 接收端若有独立 **BOOT / RESET** 键，烧录时会用到

**软件（两端电脑通用）**

- Git
- Python 3.8+（IDF 安装器会带一份，也可本机自备）
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)（下文分系统写）
- 可选：串口监视器（`idf.py monitor`、PuTTY、串口助手等）

**克隆仓库**

```bash
git clone <本仓库 URL> vibekey
cd vibekey
```

---

## 1. 安装 ESP-IDF

### 1.1 macOS

1. 安装依赖（Homebrew 示例）：

```bash
brew install cmake ninja dfu-util
# 若还没有：brew install python git
```

2. 拉取 IDF（路径可自定，下文以 `~/esp/esp-idf` 为例）：

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

3. **每次新开终端**，先导入环境再编译：

```bash
. ~/esp/esp-idf/export.sh
idf.py --version
```

也可把上一行写进 `~/.zshrc`，但注意会改 `PATH`；更稳妥是用时再 `source`。

### 1.2 Windows

推荐用乐鑫 **ESP-IDF Tools Installer**（图形安装）：

1. 打开：  
   [ESP-IDF 入门 — Windows](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html)
2. 下载并运行 **Offline / Online Installer**，勾选目标芯片 **ESP32-S3**。
3. 装完后从开始菜单打开 **「ESP-IDF PowerShell」** 或 **「ESP-IDF CMD」**（不要用没导入 IDF 的普通终端）。
4. 确认：

```bat
idf.py --version
```

**驱动：** 多数 ESP32-S3 开发板用内置 USB-Serial/JTAG，Win10/11 即插即用。若设备管理器里是未知设备，按板子芯片安装对应 USB 驱动（CP210x / CH340 / 官方 USB JTAG 等）。

**路径：** 仓库尽量放在较短路径（如 `C:\src\vibekey`），避免中文路径、过深目录导致个别工具抽风。

---

## 2. 找到串口名

烧录前只插**当前要刷的那块板**，避免认错口。

### macOS

```bash
ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null
```

常见形态：`/dev/cu.usbmodem14101`、`/dev/cu.usbmodem00011` 等。

### Windows

- 设备管理器 → **端口 (COM 和 LPT)** → 看 `COMx`
- 或在 ESP-IDF 终端里：

```bat
mode
```

后面命令里把 `COMx` 换成你的口（如 `COM5`）。`idf.py` 写 `-p COM5` 即可，一般**不用**写成 `\\.\COM5`。

---

## 3. 进入下载模式（Download）

ESP32-S3 在能自动复位时，`idf.py flash` 往往能直接刷。失败时再手动进下载模式。

### 3.1 手持端

- Voice 键接 **IO0（BOOT）**。
- **典型操作：** 按住 **Voice（BOOT）** → 点一下复位（或拔插 USB 上电）→ **松开 Voice**。
- 正常开机、配对时**不要**按住 Voice，否则可能一直停在下载模式。

### 3.2 接收端

`tools/flash_receiver.sh` 注释里的标准手法：

1. **按住 BOOT**
2. **点一下 RESET**
3. **松开 BOOT**
4. 再执行 `idf.py flash`

刷完后接收端应通过 **USB OTG** 重新枚举。若电脑只看到串口、没有「键盘 + 麦克风」，确认插的是 OTG/Device 那路 USB，而不是仅 UART 下载口（视你的 PCB 布线而定）。

---

## 4. 编译并烧录手持端

### 4.0 Flash 容量：按模组选 4MB / 8MB / 16MB

手持端模组有多大 Flash 就用对应配置，**不要统一压成 4MB**。程序约 1.2MB，当前分区表落在 4MB 以内，4/8/16MB 模组都能跑；镜像头里的容量应和芯片一致，以后扩展 OTA / 存储时也能用满。

| 模组示例 | Flash | PlatformIO 环境 | sdkconfig 叠加 |
|----------|-------|-----------------|----------------|
| N4R8 等 | 4MB | `flash_4mb` | `sdkconfig.flash.4mb` |
| N8R2（Controller-RevB 默认） | 8MB | `flash_8mb`（也是 `esp32s3`） | `sdkconfig.flash.8mb` |
| 16MB 模组 | 16MB | `flash_16mb` | `sdkconfig.flash.16mb` |

**推荐烧录（自动认容量）：**

```bash
./tools/flash_handheld.sh /dev/cu.usbmodemXXXX
# 或手动指定：./tools/flash_handheld.sh /dev/cu.usbmodemXXXX 8MB
```

脚本会 `flash_id` 读芯片，再 `pio run -e flash_4mb|flash_8mb|flash_16mb` 并按该容量写入。

**PlatformIO 手动编译：**

```bash
cd firmware/handheld
pio run -e flash_8mb          # 或 flash_4mb / flash_16mb
pio run -e flash_8mb -t upload --upload-port /dev/cu.usbmodemXXXX
```

**ESP-IDF：** 把对应的 `sdkconfig.flash.*` 内容合并进 `sdkconfig`（或 `sdkconfig.defaults`），`idf.py build` 后再 flash；勿把 16MB/8MB 镜像写到更小的芯片上。

固件不用 PSRAM；八线 PSRAM 占的 IO33–37 这块板没接。

在已 `export` / 已打开 ESP-IDF 终端的前提下：

### macOS / Linux

```bash
cd /path/to/vibekey/firmware/handheld
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

需要看日志时：

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
# 退出监视：Ctrl+]
```

### Windows（ESP-IDF PowerShell / CMD）

```bat
cd C:\src\vibekey\firmware\handheld
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash
```

或：

```bat
idf.py -p COM5 flash monitor
```

`set-target` **每个工程做一次**即可；换电脑或删了 `build/` / `sdkconfig` 后再做。

成功标志：烧录过程无报错结束；手持上电后屏幕有画面（未配对时多为 `pair` 一类界面）。

---

## 5. 编译并烧录接收端

**先拔掉手持（或确认串口是接收端）**，再刷接收端，避免口搞混。

### macOS / Linux

```bash
cd /path/to/vibekey/firmware/receiver
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

### Windows

```bat
cd C:\src\vibekey\firmware\receiver
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash
```

接收端工程会关掉 USB-Serial/JTAG 次要控制台，把 USB PHY 留给 TinyUSB（键盘 + 麦 + CDC）。这是预期行为。

**控制台：** 若要用 UART0 看 log，波特率 **115200**。OTG 枚举成功后，**CDC 串口**也会打日志，键位网页也是连这个 CDC。

---

## 6. 首次上电与配对

1. 接收端插入电脑 USB（OTG 口）。
2. 系统应出现：
   - 键盘设备
   - 麦克风设备
   - 串口（CDC）  
   设备名：**Zhiyan Receiver**（厂商 Zhiyan，VID `0x303A` / PID `0x1003`）。
3. 给手持开机（**不要**按住 Yes；按住 Yes ≥1.5s 会进蓝牙媒体模式 **Beta**，那是另一条路——鸿蒙可用，苹果/安卓常需自行映射键位）。
4. 手持屏先显示配对相关界面，成功后进入空闲时钟。
5. **校时一次**（空闲时钟默认按 **UTC+8** 显示；两端会把上次同步记在 NVS，重启后仍在，直到再同步）。

### macOS — 校时

```bash
# 把端口换成接收端 CDC（不是手持）
printf 'T%s\n' "$(date +%s)" > /dev/cu.usbmodem00011
```

或使用仓库脚本：

```bash
./tools/set_time.sh /dev/cu.usbmodem00011
```

CDC 应回复类似：`time set <epoch>`。

### Windows — 校时

PowerShell 示例（管理员通常不需要；端口改成你的 COM）：

```powershell
$port = New-Object System.IO.Ports.SerialPort COM5,115200,None,8,One
$port.Open()
$epoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$port.WriteLine("T$epoch")
Start-Sleep -Milliseconds 200
$port.Close()
```

也可用任意串口助手：打开接收端 CDC、115200、发送一行文本：

```text
T1735689600
```

把数字换成当前 UTC Unix 时间戳（秒）。

6. 试按键：
   - 按住 **Voice** → 系统麦有声音推流  
   - 点 **Yes** → 默认 Enter  
   - 点 **No** → 默认 Backspace  

7. 重新配对：手持上 **Yes+No 同时按住约 2.5s** 清配对。  
8. 多套设备：一次只配一对；接收端开机约 **45s** 后锁定已绑定手持。换绑：对接收端 CDC 发 `P!`，再配对；`P?` 查当前 peer。

---

## 7. 键位网页（可选）

改 Voice / Yes / No 在电脑上的快捷键：

```bash
cd web/keymap
python3 -m http.server 8766
```

浏览器打开 <http://localhost:8766> →「连接接收端」→ 选 **Zhiyan Receiver** 的串口。  
需 Chrome / Edge；Web Serial 要求 `https://` 或 `http://localhost`。局域网部署见 [`web/keymap/README.md`](../web/keymap/README.md)。

---

## 8. 可选：PlatformIO 脚本（主要方便 Mac / Linux）

仓库里 `tools/flash_handheld.sh`、`tools/flash_receiver.sh` 走的是本机 PlatformIO + esptool，**不是** ESP-IDF 主路径。若你已装 [PlatformIO Core](https://platformio.org/)：

```bash
# 手持先进入下载模式
./tools/flash_handheld.sh /dev/cu.usbmodemXXXX

# 接收端：按住 BOOT → 点 RESET → 松 BOOT，再执行
./tools/flash_receiver.sh /dev/cu.usbmodemXXXX
```

Windows 上更建议直接用上文的 `idf.py`；若坚持 PlatformIO，在 PIO 终端里对 `firmware/handheld`、`firmware/receiver` 执行 `pio run -t upload`，并自行指定 `upload_port = COMx`。

手持脚本会按芯片 **自动选 4/8/16MB**（也可手写容量）；接收端固定 **4MB**。手持 / 接收端镜像勿混用。

---

## 9. 常见问题

| 现象 | 排查 |
|------|------|
| 找不到串口 | 换能传数据的线；只插一块板；Windows 看设备管理器是否缺驱动 |
| `idf.py` 不是命令 | 没进 ESP-IDF 终端 / 没 `source export.sh` |
| 烧录超时 / Connecting… | 手动进下载模式；降速：`idf.py -p PORT -b 460800 flash` |
| 手持一直像没启动 | 复位时是否按住了 Voice（BOOT） |
| 电脑没有键盘/麦 | 接收端是否插对 OTG；是否刷的是 `receiver` 工程；换口或换线重试 |
| 有串口但网页连不上 | 用 Chrome/Edge；用 localhost 或 HTTPS；确认连的是接收端 CDC |
| 两台手持抢一台接收端 | 45s 锁定；对新接收端发 `P!` 后重新只配一对 |
| 时钟不对 | 再发一次 `T<unix>`；确认时区按 UTC+8 理解显示 |
| macOS 权限 | 系统设置里允许终端使用串口 / 本地网络（视系统版本而定） |

清除编译缓存重来：

```bash
cd firmware/handheld   # 或 receiver
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

---

## 10. 建议顺序（清单）

1. 安装 ESP-IDF（Mac：`install.sh esp32s3` / Windows：官方安装器）  
2. `export` 或打开 ESP-IDF 终端  
3. 只插手持 → 进下载模式（如需要）→ 编译烧录 `firmware/handheld`  
4. 拔手持，只插接收端 → 进下载模式（如需要）→ 编译烧录 `firmware/receiver`  
5. 接收端留在电脑上，手持正常开机配对  
6. 对接收端 CDC 校时 `T<unix>`  
7. 试 Voice / Yes / No；需要再开 `web/keymap`  

更细的协议、CDC 命令、蓝牙媒体模式见 [`firmware/README.md`](../firmware/README.md) 与根目录 [`README.md`](../README.md)。
