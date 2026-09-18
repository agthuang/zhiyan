# 知言 — 快捷键配置页

用浏览器（Chrome / Edge）通过 **Web Serial** 连接 **接收端** USB-CDC，自定义 Voice / Yes / No 在电脑上的按键。

> Web Serial **必须**在[安全上下文](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts)下：`https://…` 或 `http://localhost`。  
> 局域网 `http://192.168.x.x` 默认没有 `navigator.serial`——这是浏览器限制，网页改不掉。

## 本机打开

```bash
cd web/keymap
python3 -m http.server 8766
```

浏览器打开 <http://localhost:8766>。

## 局域网部署（推荐 HTTPS）

```bash
./tools/serve_keymap.sh          # 默认 HTTPS，监听 0.0.0.0:8766
# 或
./tools/serve_keymap.sh http     # 纯 HTTP（需配合下面的 Chrome 标记）
```

手机/另一台电脑访问 `https://<服务器局域网IP>:8766`。自签证书首次会提示不安全，点「继续访问」即可；通过后 Web Serial 可用。

### 备选：仍用局域网 HTTP

在 **使用串口的那台电脑** 的 Chrome 中：

1. 打开 `chrome://flags/#unsafely-treat-insecure-origin-as-secure`
2. 填入完整源，例如 `http://192.168.1.20:8766`
3. 重启浏览器后再打开该地址

仅建议内网开发机使用。

## 使用

1. 接收端插入**打开网页的那台电脑**，点「连接接收端」，选择 **Zhiyan Receiver** 串口。
2. 点选 Voice / Yes / No。
3. **推荐点选组合**（不怕系统抢键）：
   - Windows：点「右 Ctrl」「右 Alt」等；可用预设「右Ctrl+右Alt」
   - Mac：点「右 ⌘ / 右 ⌥ / 右 Ctrl」；可用「右Ctrl+右⌥」或「豆包（右Ctrl+右⌘）」
4. 点一下修饰键会立刻写到当前键；改完后点「保存到设备」。
5. 「键盘录制」可选。若某键已被其它软件占用，浏览器收不到，请改用点选。

串口在打开网页的机器上；不能把接收端插在 A 电脑、却在 B 电脑的浏览器里连串口。

## CDC 命令（调试）

完整列表见 [`firmware/README.md`](../../firmware/README.md)。本页常用：

```text
K? / K mm:cc,... / K!     # 快捷键
B? / B <0-255>            # 背光
T? / T<unix>              # 授时（连接时网页会自动授时）
S?                        # 状态
P? / P!                   # 绑定查询 / 清绑定
```

两套设备：一次只配一对；接收端开机约 45s 后锁定已绑定手持。换手持用 `P!` 再配。
手持 OSD 可用 `aihook/codex/push_osd.py`（与本页抢串口时先断开网页）。

连接后网页会：授时 → 读键位/背光 → 轮询 `S?` 更新小屏电量与手持在线点。底栏可随时读取 / 保存。
