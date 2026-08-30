# HomeMind 6 阶段实施 — 交付物清单

> 日期：2026-07-25

## 1. 修改过的源文件列表

### `tools/staging-network_manager.c` — Wi-Fi 状态机 + 诊断日志

**变更原因**：
- 原实现使用固定 `usleep(20000000)` 等待关联，不可靠
- 缺少诊断日志，无法定位连接失败原因
- 没有自动重连机制

**主要改动**：
1. 添加 `[HM-WIFI]` 和 `[HM-NET]` 诊断日志
2. 实现显式状态机：`IDLE → CONNECTING → ASSOCIATED → DHCP → READY / RETRY_WAIT / FAILED`
3. 递增退避重试（5/10/20/40s，上限 60s，最大 5 次）
4. 后台线程持续监控连接状态，断线自动重连
5. 启动时自动读取保存的凭据并重连

### `tools/staging-vela_tls.c` — TLS 直连 + 诊断日志

**变更原因**：
- TLS 握手失败（-0x004c）时缺少诊断信息
- 需要确认直连路径（无代理）工作正常

**主要改动**：
1. 添加 `[HM-TLS]` 和 `[HM-LLM]` 诊断日志
2. TCP 连接时输出目标地址/端口和结果
3. TLS 握手输出版本、密码套件、耗时
4. HTTP 请求/响应输出状态码
5. 错误时输出 mbedTLS 错误码和 socket errno

### `tools/staging-http_proxy.c` — 代理默认禁用确认

**变更原因**：
- 需要确认无代理配置时使用直连

**主要改动**：
1. 添加 `[HM-NET]` 日志显示代理状态
2. 无配置时输出 "No proxy configured — using DIRECT connection"

### `tools/staging-homemind_tools.c` — 设备工具实现（新增）

**变更原因**：
- Phase F 需要 LLM 可调用的本地设备工具

**实现**：
1. `device_status` — 返回网络状态、IP、堆内存、运行时间
2. `light_set` — 控制板载 LED（白名单 GPIO 引脚）
3. `scene_set` — home/sleep/away 三个场景

## 2. 新增文件

| 文件 | 用途 |
|------|------|
| `tools/deploy-to-vm.sh` | 部署修改后的文件到 VM |
| `tools/serial-operations.md` | 串口操作手册（不含敏感信息） |
| `tools/test-llm.md` | MiMo Token Plan 直连测试指南 |
| `tools/defconfig-persistent-storage.overlay` | 持久存储 Kconfig 配置 |
| `tools/persistent-storage-spec.md` | 持久存储与日志管理规格 |
| `tools/homemind-system-prompt.md` | LLM 系统提示词（设备工具说明） |

## 3. Ubuntu 构建命令

```bash
# 1. 进入 OpenVela 目录
cd $HOME/work/openvela

# 2. 部署修改后的文件
bash /path/to/tools/deploy-to-vm.sh

# 3. 编译
make -C nuttx -j4

# 4. 复制 ELF 到 Windows
scp nuttx/nuttx.elf windows-host:~/Desktop/HomeMind/contest2026_138_HomeMind/artifacts/
```

## 4. Windows 烧录命令

```powershell
# 确保串口工具已关闭
.\tools\flash.ps1 -Port COM3 -Firmware artifacts\nuttx.bin
```

## 5. 验证流程

### 5.1 Wi-Fi 自动连接

```
# 重启开发板
nsh> ai_agent
# 观察日志：
[HM-WIFI] State: IDLE -> CONNECTING
[HM-WIFI] Connecting to SSID='xxx' iface=wlan0
[HM-WIFI] ifup wlan0
[HM-WIFI] Setting mode=MANAGED(2)
[HM-WIFI] Setting WPA2 PSK
[HM-WIFI] Initiating association
[HM-WIFI] State: CONNECTING -> ASSOCIATED
[HM-WIFI] State: ASSOCIATED -> DHCP
[HM-NET] Starting DHCP on wlan0
[HM-NET] DHCP success, IP=192.168.x.x
[HM-WIFI] State: DHCP -> READY
```

### 5.2 TLS 直连测试

```
vela> net_test
[HM-NET] DNS resolve: ...
[HM-NET] Direct TCP connect to x.x.x.x:443
[HM-NET] TCP connect OK, fd=5
[HM-TLS] TLS handshake starting: host=...
[HM-TLS] Handshake OK: version=TLSv1.2, cipher=...
[HM-LLM] HTTP response status: 200
[HM-NET] net_test PASSED
```

### 5.3 MiMo 对话测试

```
vela> set_llm mimo <your_token_plan_key>
vela> ask 你好，请介绍一下你自己
[HM-LLM] HTTPS POST token-plan-cn.xiaomimimo.com:443/v1/chat/completions
[HM-NET] Direct TCP connect to x.x.x.x:443
[HM-TLS] Handshake OK
[HM-LLM] HTTP response status: 200
你好！我是 MiMo...
```

### 5.4 掉电保存测试

```
# 1. 配置 Wi-Fi 和 LLM
# 2. 重启开发板
# 3. vela> net_status  → 应自动连接
# 4. vela> ask 你好    → 应正常工作
```

## 6. 已知限制

1. **Flash 持久存储**：当前 `/data` 是 TMPFS，需要启用 LittleFS 才能掉电保存
2. **CLI 命令**：`config_reset`、`log_show`、`log_clear` 需要在 ai_agent CLI 模块中实现
3. **工具注册**：`homemind_tools.c` 需要集成到 ai_agent 的工具注册系统
4. **系统提示词**：需要将 `homemind-system-prompt.md` 内容添加到 SOUL.md

## 7. 下一步建议

1. 在 VM 上运行 `deploy-to-vm.sh` 部署修改后的文件
2. 编译并烧录到 ESP32-S3-EYE
3. 按验证流程逐项测试
4. 根据测试结果调整参数（超时、退避等）
5. 实现 Flash 持久存储（Phase E）
6. 集成 HomeMind 工具到 ai_agent（Phase F）
