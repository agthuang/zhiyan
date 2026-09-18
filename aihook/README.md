# AI Hook

把 AI agent（目前 Codex）的生命周期状态接到本机工具，并可推到 **知言** 手持小屏。

当前实现在 `aihook/codex/`。Cursor 等其它产品可按同样模式另开子目录。

---

## 在另一台 Mac 上复现（Codex + 知言小屏）

### 前提

1. 已克隆本仓库，固件已刷到手持 + 接收端（含 OSD：`O #RRGGBB WORD`）。
2. 本机已安装 [Codex CLI / Codex app](https://github.com/openai/codex)，且能正常对话。
3. 接收端插入**这台电脑**的 USB，手持已配对（屏上有时钟，不是一直 `pair`）。
4. 系统 Python 3（macOS 自带 `/usr/bin/python3` 即可）。**不需要** `pyserial`：串口用标准库 `termios`。

### 一步：安装 hooks

在仓库根目录：

```bash
./aihook/codex/install_hooks.sh
```

脚本会：

- 备份已有的 `~/.codex/hooks.json` → `hooks.json.bak.<时间戳>`
- 写入新 hooks，命令指向**本机仓库里的绝对路径** `…/aihook/codex/dispatch.py`
- 同步更新仓库内 `aihook/codex/hooks.json`（方便对照）

换电脑或换了仓库路径后，**必须再跑一次** `install_hooks.sh`，否则仍指向旧路径。

### 二步：在 Codex 里信任 hooks

打开 Codex，执行 `/hooks`（或按客户端提示），信任本仓库的 `dispatch.py`。未信任则 hook 不会跑，小屏也不会变。

### 三步：验证

**只测串口 / 小屏（不依赖 Codex）：**

```bash
cd /path/to/vibe-key
python3 aihook/codex/push_osd.py WORKING
python3 aihook/codex/push_osd.py --clear
```

应看到手持出现青色 `WORKING`，清屏后回时钟。若报 `no Zhiyan receiver CDC found`：

- 确认插的是**接收端**（不是手持调试口）
- 关掉占用同一 CDC 的网页 keymap / 串口监视器
- 看 `ls /dev/cu.usbmodem*` 是否有端口

**测 Codex → 小屏：**

1. 接收端保持插着，手持已连上  
2. 在 Codex 里发一句简单问题  
3. 屏应出现 `WORKING`（青），结束后 `INPUT`（紫）；若弹出权限确认则短暂 `ASK?`（红粉），同意后回到 `WORKING`

可选：实时网页事件流

```bash
python3 aihook/codex/serve.py
# 浏览器打开 http://127.0.0.1:8777/
```

### 排查

| 现象 | 检查 |
|------|------|
| Codex 发了问题屏不动 | hooks 是否安装且 `/hooks` 已信任；看 `aihook/codex/data/dispatch_trace.log` 有没有新 `HOOK=` |
| trace 有 `OSD skip: no … CDC` | 接收端未插 / 串口被占 / 换机后端口变了（重插或关掉抢串口的程序） |
| 只有 status 文件在变、屏不变 | 旧版 hook 未含 OSD；确认跑的是本仓库最新 `dispatch.py`，并重跑 `install_hooks.sh` |
| 权限点完一直停在 `ASK?` | 需含 `PostToolUse → WORKING` 的新版 `dispatch.py` |

状态文件：`~/.codex/agent-status.json`（兼容 CodexBar）  
串口缓存：`~/.codex/vibekey-cdc-port`（上次成功的 CDC 路径，可删）

### 单测（无硬件）

```bash
cd aihook/codex
python3 tests/test_dispatch.py
python3 tests/test_osd_cdc.py
```

---

## Codex 目录文件

| 路径 | 作用 |
|------|------|
| `dispatch.py` | Codex hook 入口：写 status、追加 `data/events.jsonl`、推 OSD |
| `osd_cdc.py` | 自动找接收端 CDC，发 `O` / `O!` |
| `push_osd.py` | 命令行手动推词 / `--watch` / `--clear` |
| `install_hooks.sh` | 安装到 `~/.codex/hooks.json` |
| `hooks.json` | 安装脚本生成的模板（绝对路径） |
| `serve.py` + `web/` | 可选：SSE 实时事件页 |
| `data/` | 本地日志（`events.jsonl`、`dispatch_trace.log`），勿当配置提交 |
| `tests/` | 单元测试 |

## 状态 → 小屏

| Codex `state` | 屏上 | 颜色 |
|---------------|------|------|
| idle | 清掉 → 时钟 | — |
| thinking | `THINK...` | 琥珀 |
| developing | `WORKING` | 青 |
| confirming | `ASK?` | 红粉 |
| completed | `INPUT` | 淡紫 |

CDC 原文（接收端）：

```text
O #RRGGBB WORD   # WORD ≤ 8，A–Z 0–9 ?!._-
O!               # 清除
```
