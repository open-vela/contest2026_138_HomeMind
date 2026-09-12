# HomeMind 快速开始

完整说明见 [MIGRATION_UBUNTU.md](MIGRATION_UBUNTU.md)。本页只保留迁移完成后的常用命令。

## Ubuntu 主机

配置建议：Ubuntu 22.04、i3-8100T、8 GB RAM、256 GB 存储，初始使用 2 个并发任务。

### 1. 进入项目并检查

```bash
cd ~/work/openvela/contest2026_138_HomeMind
OPENVELA_ROOT=$HOME/work/openvela ./tools/verify-ubuntu.sh
git status --short
```

### 2. 加载 ESP-IDF 环境

```bash
source /实际路径/esp-idf/export.sh
```

### 3. 构建 HomeMind 固件

前提：官方 `esp32s3-eye:ai_agent` 已配置并至少成功构建一次。

```bash
OPENVELA_ROOT=$HOME/work/openvela \
JOBS=2 \
./scripts/build.sh build
```

### 4. 校验产物

```bash
sha256sum -c artifacts/SHA256SUMS
```

### 5. 烧录

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
```

### 6. 串口

```bash
./.venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

### 7. 当前 Wi-Fi 验证重点

```text
nsh> ai_agent
vela> set_wifi <ssid> <password>
vela> net_status
```

不要把真实 `set_wifi` 命令复制到日志或报告。观察：

- `ifup`、`mode`、`psk`、`essid` 是否成功；
- 是否真实完成关联；
- `renew` 是否执行；
- 30 秒后是否获得 IPv4；
- 有 IPv4 后默认网关和 DNS 是否可用。

## Win11

Win11 只继续两项工作：

- `C:\Old\HomeMind\外壳\`：UG 外壳；
- `C:\Old\HomeMind\contest2026_138_HomeMind\miniprogram\`：微信小程序。

备份：

```powershell
cd C:\Old\HomeMind
.\contest2026_138_HomeMind\tools\create-win11-backup.ps1
```

Ubuntu Core 迁移包：

```powershell
.\contest2026_138_HomeMind\tools\create-migration-package.ps1
```
