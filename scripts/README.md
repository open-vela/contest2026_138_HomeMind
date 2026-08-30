# HomeMind Ubuntu 脚本

脚本面向 Ubuntu 22.04 x86_64。目标主机为 i3-8100T、8 GB RAM、256 GB 存储，因此默认构建并发为 2。

完整迁移流程见 [../MIGRATION_UBUNTU.md](../MIGRATION_UBUNTU.md)。

## 环境初始化

```bash
./scripts/setup_ubuntu.sh
```

可选安装 ESP32 udev 规则：

```bash
./scripts/setup_ubuntu.sh --configure-udev
```

`setup_vm.sh` 仅保留兼容入口，会转发到 `setup_ubuntu.sh`。

## 环境检查

```bash
OPENVELA_ROOT=$HOME/work/openvela ./tools/verify-ubuntu.sh
```

## 构建入口

首次必须先用官方流程配置并构建 `esp32s3-eye:ai_agent`。项目入口面向已经配置好的 `nuttx` 树：

```bash
OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh check
OPENVELA_ROOT=$HOME/work/openvela ./scripts/build.sh deploy
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
```

`build` 会：

1. 部署 `network_manager.c`、`vela_tls.c`、`http_proxy.c` 三个 staging 文件；
2. 将 UDP 预分配连接池从 8 增至 16，并将 UDP/TCP write-buffer chain 池
   固定为 64/32；抓包确认 agent 运行时 DHCP DISCOVER 未发出，而退出
   agent 后立即发出并成功；UDP 写队列高于 64 会使 Wi-Fi 的 ESSID 配置超时，
   所以保留安全上限，IOB 经诊断保持 64；
3. 使用当前 OpenVela/NuttX 配置编译；
4. 复制 ELF 和板级 DIO BIN 到 `artifacts/`；
5. 更新 `artifacts/SHA256SUMS`。

当前不会自动部署 `staging-homemind_tools.c`、持久化 overlay 或系统提示词，这些仍是待集成项。

## 迁移包

Win11 PowerShell：

```powershell
# 默认：Ubuntu 固件主线，不含小程序和旧服务端
.\tools\create-migration-package.ps1

# 完整项目快照
.\tools\create-migration-package.ps1 -Profile FullRepository

# Win11 外壳 + 小程序备份
.\tools\create-win11-backup.ps1
```

## 安全要求

- 不在脚本、命令历史或日志中保存 Wi-Fi 密码和 MiMo Token；
- 新 staging 源码会把 PSK 命令日志替换为 `<redacted>`；
- 只有重编译烧录后，新固件才具备该脱敏修正；
- 烧录固定地址 `0x0`，不执行整片擦除；
- 串口设备名必须现场确认，不能照抄旧的 COM3 或 `/dev/ttyACM0`。
