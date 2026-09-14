# HomeMind 串口操作手册

> 不含敏感信息。Wi-Fi 密码、API Key 等由用户在设备端输入。

## 1. 环境准备

### 1.1 打开串口

```powershell
# Windows PowerShell
.\.venv-tools\Scripts\python.exe -m serial.tools.miniterm COM3 115200
```

### 1.2 进入 ai_agent

```
nsh> ai_agent
```

进入后提示符变为 `vela>`。

## 2. 首次配置

### 2.1 配置 Wi-Fi

```
vela> set_wifi <your_ssid> <your_password>
```

设备会：
1. 连接 Wi-Fi（WPA2）
2. 获取 DHCP IP 地址
3. 保存凭据到持久存储

### 2.2 配置 LLM

```
vela> set_llm mimo <your_token_plan_key>
```

可选后端：`mimo`、`kimi`、`qwen`、`deepseek`、`glm`、`openai`、`claude`、`openrouter`

### 2.3 切换模型（可选）

```
vela> set_llm model mimo-v2-pro
```

## 3. 验证命令

### 3.1 查看网络状态

```
vela> net_status
```

预期输出：已连接，显示 IP 地址。

### 3.2 测试网络连通性

```
vela> net_test
```

预期输出包含：
```
[HM-NET] DNS resolve: ...
[HM-NET] TCP connect: ...
[HM-TLS] Handshake OK: ...
[HM-LLM] HTTP response status: 200
[HM-NET] net_test PASSED
```

### 3.3 查看配置

```
vela> config_show
```

API Key 会脱敏显示（前4位****后4位）。

### 3.4 测试对话

```
vela> ask 你好，请介绍一下你自己
```

预期：收到 MiMo 的文字回答。

## 4. 设备控制

### 4.1 查看设备状态

```
vela> ask 查看设备状态
```

### 4.2 控制灯光

```
vela> ask 打开灯
vela> ask 关闭灯
```

### 4.3 切换场景

```
vela> ask 切换到睡眠模式
vela> ask 切换到回家模式
```

## 5. 重启后验证

设备重启后：
1. 自动连接 Wi-Fi（无需手工操作）
2. LLM 配置保留
3. 可直接使用 `ask` 命令

```
vela> net_status
vela> ask 你好
```

## 6. 诊断日志说明

所有关键日志带固定前缀：

| 前缀 | 含义 |
|------|------|
| `[HM-WIFI]` | Wi-Fi 关联和认证 |
| `[HM-NET]` | 网络连接（DHCP、DNS、TCP） |
| `[HM-TLS]` | TLS 握手和加密 |
| `[HM-LLM]` | HTTP 请求和 LLM 调用 |

## 7. 故障排查

### Wi-Fi 连接失败

检查日志中的 `[HM-WIFI]` 和 `[HM-NET]` 前缀：
- `wapi psk ret=` 非 0 → 密码错误
- `DHCP failed` → 路由器 DHCP 问题
- `No saved WiFi credentials` → 需要先执行 `set_wifi`

### TLS 握手失败

检查 `[HM-TLS]` 前缀：
- `TCP connect FAILED` → 网络不通或防火墙
- `Handshake FAILED: ret=-0x` → TLS 版本或证书问题
- `socket errno: 11` → 超时（EAGAIN）

### LLM 调用失败

检查 `[HM-LLM]` 前缀：
- `HTTP response status: 401` → API Key 无效
- `HTTP response status: 403` → 权限不足
- `HTTP response status: 429` → 请求过于频繁
- `HTTP response status: 5xx` → 服务器错误

## 8. 常用命令速查

| 命令 | 说明 |
|------|------|
| `set_wifi <ssid> <pass>` | 配置 Wi-Fi |
| `set_llm <preset> <key>` | 配置 LLM |
| `set_llm model <name>` | 切换模型 |
| `net_status` | 网络状态 |
| `net_test` | 网络连通性测试 |
| `config_show` | 查看配置（脱敏） |
| `ask <text>` | 对话 |
| `wifi_reconnect` | 手动重连 Wi-Fi |
| `config_reset` | 重置所有配置 |
| `log_show` | 查看持久日志 |
| `log_clear` | 清除日志 |
