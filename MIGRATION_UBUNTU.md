# HomeMind 迁移到 Ubuntu 22.04 主机

本文对应以下分工：

- Ubuntu 22.04 主机：OpenVela、ai_agent、固件源码、编译、烧录和串口调试；
- Win11：UG 外壳和微信小程序；
- 两端最终通过 Git 分支同步，不靠反复覆盖整个目录。

目标 Ubuntu 主机为 i3-8100T、8 GB 内存、256 GB 存储。该配置可以开发，但需要控制并发和磁盘占用：首次使用 `repo sync -j2`、`JOBS=2`，建议为 OpenVela 预留至少 80～100 GB 空间，并保留 20 GB 以上空闲空间。

## 1. Win11 上已经整理好的边界

Ubuntu Core 迁移包默认包含固件主线、脚本、文档、基线产物和比赛 manifest，但不包含：

- `.venv-tools/` 等 Windows Python 环境；
- `miniprogram/`；
- 工作区上级 `外壳/`；
- 冻结的 FastAPI、QuickApp 和占位 app/board 源码；历史文档仍随包保留在 `docs/legacy/`；
- Git 元数据、构建缓存和本地运行数据；
- Wi-Fi 密码、MiMo Token 等秘密。

外壳和小程序可单独备份，仍在 Win11 编辑。

## 2. Win11 生成两个包

在 `C:\Old\HomeMind` 打开 PowerShell：

```powershell
cd C:\Old\HomeMind

# Ubuntu 固件开发迁移包
.\contest2026_138_HomeMind\tools\create-migration-package.ps1

# Win11 外壳 + 小程序备份
.\contest2026_138_HomeMind\tools\create-win11-backup.ps1
```

输出位置：

```text
transfer/ubuntu/HomeMind-ubuntu-core-<时间>.tar.gz
transfer/ubuntu/HomeMind-ubuntu-core-<时间>.tar.gz.sha256
transfer/ubuntu/HomeMind-ubuntu-core-<时间>.tar.gz.manifest.txt

transfer/win11/HomeMind-win11-design-<时间>.tar.gz
transfer/win11/HomeMind-win11-design-<时间>.tar.gz.sha256
```

如需完整仓库快照而不是 Ubuntu Core 包：

```powershell
.\contest2026_138_HomeMind\tools\create-migration-package.ps1 -Profile FullRepository
```

生成后查看 manifest，确认没有秘密文件，再复制到 Ubuntu。

## 3. 准备 Ubuntu 22.04

先更新系统并安装传输、Git 和 SSH 基础工具：

```bash
sudo apt update
sudo apt install -y git git-lfs repo rsync openssh-server ca-certificates curl
sudo systemctl enable --now ssh
git lfs install
```

检查资源：

```bash
free -h
df -h /
nproc
hostname -I
```

建议：

- OpenVela 放在 Ubuntu 的 ext4 本地磁盘，例如 `~/work/openvela`；
- 不要放在 NTFS、SMB、虚拟机共享目录或 U 盘文件系统中直接编译；
- 8 GB 内存先使用 2 个构建任务；
- 如果系统没有 swap，可在确认磁盘空间后自行配置 4～8 GB swap；不要在空间不足时盲目创建。

让当前用户可以访问串口：

```bash
sudo usermod -aG dialout "$USER"
```

执行后注销并重新登录。

## 4. 把迁移包传到 Ubuntu

在 Win11 PowerShell 中，把文件名替换成刚生成的实际文件：

```powershell
scp .\transfer\ubuntu\HomeMind-ubuntu-core-<时间>.tar.gz `
  <ubuntu-user>@<ubuntu-ip>:~/transfer/

scp .\transfer\ubuntu\HomeMind-ubuntu-core-<时间>.tar.gz.sha256 `
  <ubuntu-user>@<ubuntu-ip>:~/transfer/

scp .\transfer\ubuntu\HomeMind-ubuntu-core-<时间>.tar.gz.manifest.txt `
  <ubuntu-user>@<ubuntu-ip>:~/transfer/
```

也可以使用 U 盘，但必须先完成 SHA-256 校验再解压。

## 5. Ubuntu 校验迁移包

```bash
mkdir -p ~/transfer/homemind
cd ~/transfer
sha256sum -c HomeMind-ubuntu-core-*.tar.gz.sha256
tar -xzf HomeMind-ubuntu-core-*.tar.gz -C ~/transfer/homemind
cd ~/transfer/homemind/contest2026_138_HomeMind
chmod +x scripts/*.sh tools/*.sh
```

校验必须显示 `OK`。失败时重新复制，不要继续使用损坏的包。

## 6. 同步正式 OpenVela 工作区

不要把迁移包直接当成完整 OpenVela。先使用比赛 manifest 建立正式工作区：

```bash
mkdir -p ~/work/openvela
cd ~/work/openvela

repo init \
  -u https://github.com/open-vela/contest2026_138_HomeMind \
  -b dev-ai-contest-2026 \
  -m contest2026_138_HomeMind.xml \
  --git-lfs

repo sync -c -j2
```

同步成功后应存在：

```text
~/work/openvela/nuttx/
~/work/openvela/packages/ai_agent/
~/work/openvela/contest2026_138_HomeMind/.git/
```

如果官方仓需要账号或 fork 权限，先按大赛账号流程处理，不要用空目录伪造 `.git`。

## 7. 合并当前 HomeMind 文件

迁移包没有 Git 元数据。把它覆盖到正式比赛仓时保留正式仓的 `.git`：

```bash
rsync -a \
  --exclude .git \
  ~/transfer/homemind/contest2026_138_HomeMind/ \
  ~/work/openvela/contest2026_138_HomeMind/
```

命令没有 `--delete`，不会删除正式仓原有的小程序或模板文件。完成后：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
git status --short
git branch --show-current
git remote -v
```

先保存差异，不要在未检查前执行 `git reset --hard` 或覆盖性恢复。

## 8. 安装完整构建依赖

```bash
cd ~/work/openvela/contest2026_138_HomeMind
./scripts/setup_ubuntu.sh
```

可选安装 ESP32 udev 规则：

```bash
./scripts/setup_ubuntu.sh --configure-udev
```

脚本会在项目中创建 Linux `.venv/`，安装 `esptool 5.3.1` 和 `pyserial`。不要把 Win11 的 `.venv-tools` 复制到 Ubuntu。

## 9. 检查 Ubuntu/OpenVela 环境

```bash
cd ~/work/openvela/contest2026_138_HomeMind
OPENVELA_ROOT=$HOME/work/openvela ./tools/verify-ubuntu.sh
```

还应确认 ESP-IDF 环境脚本位置：

```bash
find "$HOME" -path '*/esp-idf/export.sh' -type f 2>/dev/null
```

后续把实际路径替换到：

```bash
source /实际路径/esp-idf/export.sh
```

## 10. 第一次只构建官方 ai_agent 基线

第一次迁移不要直接混入新的 Wi-Fi 修改。先证明新主机能构建官方基线：

```bash
cd ~/work/openvela

cp packages/ai_agent/defconfigs/esp32s3-eye/esp32s3-eye_defconfig \
  nuttx/boards/xtensa/esp32s3/esp32s3-eye/configs/ai_agent/defconfig

source /实际路径/esp-idf/export.sh
export CCACHE_DISABLE=1

./build.sh esp32s3-eye:ai_agent distclean
bash packages/ai_agent/fix_esp32s3.sh &
./build.sh esp32s3-eye:ai_agent -j2
```

如果官方流程或当前分支参数发生变化，以当前比赛文档为准。成功后保存：

```bash
git -C packages/ai_agent rev-parse HEAD
git -C nuttx rev-parse HEAD
sha256sum nuttx/nuttx.elf
```

## 11. 构建 HomeMind 修改版

官方基线成功后再使用项目入口。它会部署三个 `staging-*.c` 网络文件到 `packages/ai_agent/src/infra/`，然后在已配置的 `nuttx` 树上编译：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
source /实际路径/esp-idf/export.sh

OPENVELA_ROOT=$HOME/work/openvela \
JOBS=2 \
./scripts/build.sh build
```

构建日志在：

```text
~/work/openvela/logs/homemind/build.log
~/work/openvela/logs/homemind/fix.log
```

产物会更新到项目 `artifacts/`。核验：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
sha256sum -c artifacts/SHA256SUMS
```

## 12. Ubuntu 烧录和串口

插入 ESP32-S3-EYE 后：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

关闭所有占用串口的终端，再烧录。端口按实际检测结果填写：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
```

烧录地址固定为 `0x0`，脚本不执行整片擦除。

打开串口：

```bash
./.venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

## 13. 迁移后的第一轮 Wi-Fi 调试

先只验证 Wi-Fi 和 IPv4，不同时处理后续协议：

1. 启动 `ai_agent`；
2. 现场输入 `set_wifi <ssid> <password>`，不要把命令复制进报告；
3. 观察脱敏后的 `[HM-WIFI]`、`[HM-NET]` 日志；
4. 等待至少 30 秒；
5. 检查 `ifconfig wlan0` 和 `net_status`；
6. 没有 IPv4 时排查 WPA2 关联和 DHCP；
7. 有 IPv4但域名不通时排查默认网关和 DNS。

当前 staging 源码已把 PSK 命令日志改为 `<redacted>`，但只有重新编译和烧录后新固件才会生效。

## 14. 双机 Git 工作流

迁移稳定后，Ubuntu 和 Win11 都应拥有同一官方比赛仓的有效 clone：

- Ubuntu 使用 `feature/firmware-*` 分支修改固件；
- Win11 使用 `feature/miniprogram-*` 分支修改 `miniprogram/`；
- 每次开始工作前拉取最新提交；
- 不在两端同时编辑同一文件；
- 外壳文件保留在 `../外壳/`，需要提交时再评估 Git LFS 或单独附件。

详细约定见 [docs/DEVELOPMENT_WORKFLOW.md](docs/DEVELOPMENT_WORKFLOW.md)。

## 15. 迁移完成标准

- Ubuntu 中 `repo status` 和比赛仓 `git status` 可正常使用；
- 官方基线可在 `JOBS=2` 下成功构建；
- HomeMind 修改版可构建、生成并校验 BIN/ELF；
- Ubuntu 可识别串口并完成一次烧录；
- 新固件日志不输出 Wi-Fi 密码；
- Win11 的 `外壳/` 和 `miniprogram/` 保持不变并已有独立备份；
- 后续开发只从 Ubuntu 固件仓和 Win11 小程序分支推进。
