# HomeMind Ubuntu 22.04 SSH / OpenVela 操作手册

> 原整理日期：2026-08-07  
> 文档定位：本文件只保留 Ubuntu、SSH、OpenVela 构建、烧录和网络诊断的专项操作；项目范围、优先级和总路线以工作区根目录 `HomeMind_项目总体计划.md` 为准。  
> 适用主机：Ubuntu 22.04，i3-8100T，8 GB 内存，256 GB 存储  
> 开发板：ESP32-S3-EYE  
> 工作方式：Win11 通过 SSH 连接 Ubuntu；Ubuntu 负责编译、烧录和串口调试

## 1. 目标和当前结论

Ubuntu 已安装完成，后续不需要为日常开发配置远程桌面。推荐的固定连接方式如下：

```text
Win11（SSH 客户端、UG、小程序）
        │
        │ 有线局域网 / SSH
        ▼
Ubuntu 22.04（OpenVela、ai_agent、Git、编译、烧录、串口日志）
        │
        │ USB 数据线
        ▼
ESP32-S3-EYE
```

当前工程已经完成：

- ESP32-S3-EYE 上 OpenVela/NuttX 和 `ai_agent` 的基线启动，历史记录表明可以进入 `nsh>` 和 `vela>`；
- 已保留一版带 Wi-Fi 状态机和 `[HM-WIFI]`、`[HM-NET]`、`[HM-TLS]`、`[HM-LLM]` 分层日志的固件；
- Ubuntu 初始化、环境校验、构建、烧录和迁移脚本已经整理；
- Wi-Fi PSK 日志脱敏已经进入 staging 源码，但必须在 Ubuntu 重新编译、烧录后才进入新固件；
- Win11 的 UG 外壳和微信小程序已经与 Ubuntu 固件主线分开管理。

当前 P0 阻塞是：**ESP32-S3-EYE 尚未稳定完成 Wi-Fi 关联、DHCP 和 IPv4 上网。**

在 `wlan0` 稳定获得 IPv4 以前，不把 TLS、MiMo 或小程序问题混进本轮调试。后续严格按以下顺序推进：

```text
SSH 与 Ubuntu 环境
  → 正式 OpenVela/Git 工作区
  → 官方 ai_agent 基线构建
  → HomeMind 诊断固件构建与烧录
  → wlan0 就绪
  → WPA2 关联
  → DHCP / IPv4
  → 网关 / DNS / TCP
  → TLS
  → MiMo 问答
  → 掉电持久化
  → 本地工具调用
  → 小程序与外壳收尾
```

---

## 2. 第一阶段：把 SSH 开发通道固定下来

### 2.1 Ubuntu 连接方式

Ubuntu 主机优先使用网线接入路由器。到路由器管理页面给 Ubuntu 的 MAC 地址设置 DHCP 地址保留，例如固定为 `192.168.1.50`。这比在 Ubuntu 中手写静态 IP 更容易维护。

在 Ubuntu 上确认地址和 SSH 服务：

```bash
hostname -I
ip -br address
systemctl status ssh --no-pager
```

若尚未安装 SSH 服务：

```bash
sudo apt update
sudo apt install -y openssh-server tmux
sudo systemctl enable --now ssh
```

成功标志：

- `hostname -I` 能看到局域网 IPv4；
- `systemctl status ssh` 显示 `active (running)`；
- 路由器重启或 Ubuntu 重启后，主机 IP 不改变。

### 2.2 从 Win11 测试登录

在 Win11 PowerShell 中执行，替换用户名和地址：

```powershell
ssh <ubuntu-user>@<ubuntu-ip>
```

首次连接会询问主机指纹，核对 IP 后输入 `yes`。登录成功后执行：

```bash
whoami
hostname
uname -a
```

### 2.3 配置 SSH 密钥

在 Win11 PowerShell 生成密钥；如果已经存在 `id_ed25519`，不要覆盖：

```powershell
ssh-keygen -t ed25519
```

把公钥追加到 Ubuntu：

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub" | ssh <ubuntu-user>@<ubuntu-ip> "umask 077; mkdir -p ~/.ssh; cat >> ~/.ssh/authorized_keys"
```

重新打开 PowerShell，确认能够使用密钥登录。**只有密钥登录验证成功后，才考虑关闭 SSH 密码登录；现阶段不必急着修改 `sshd_config`。**

可在 Win11 的 `%USERPROFILE%\.ssh\config` 中增加：

```sshconfig
Host homemind-ubuntu
    HostName <ubuntu-ip>
    User <ubuntu-user>
    ServerAliveInterval 30
    ServerAliveCountMax 3
```

以后直接执行：

```powershell
ssh homemind-ubuntu
```

### 2.4 使用 tmux 防止 SSH 断线中止编译

登录 Ubuntu 后：

```bash
tmux new -s homemind
```

在 tmux 中进行同步和编译。按 `Ctrl+B`，松开后按 `D`，可以退出但不停止任务。重新连接后恢复：

```bash
tmux attach -t homemind
```

---

## 3. 第二阶段：传输并校验当前工程

如果当前工程尚未复制到 Ubuntu，使用本节。已经完成传输且校验通过时，可以从第 4 节继续。

当前 Win11 已生成的最新 Ubuntu 核心包：

```text
transfer/ubuntu/HomeMind-ubuntu-core-20260807-171744.tar.gz
transfer/ubuntu/HomeMind-ubuntu-core-20260807-171744.tar.gz.sha256
transfer/ubuntu/HomeMind-ubuntu-core-20260807-171744.tar.gz.manifest.txt
```

归档文件预期 SHA-256：

```text
97a8dc22d1ef385bce966d47104b70167ff42aa1ab4a9b6cdcbb4381c85f63e9
```

注意：现有 manifest 标记了 `operator_review_required=yes` 和 `secrets_included=not_checked`。传输前需要人工确认压缩包中没有 Wi-Fi 密码、MiMo Token、Authorization Header 或未脱敏日志。

### 3.1 Ubuntu 创建接收目录

```powershell
ssh homemind-ubuntu "mkdir -p ~/transfer"
```

### 3.2 Win11 发送三个文件

在 `C:\Old\HomeMind` 打开 PowerShell：

```powershell
cd C:\Old\HomeMind

scp .\transfer\ubuntu\HomeMind-ubuntu-core-20260807-171744.tar.gz homemind-ubuntu:~/transfer/
scp .\transfer\ubuntu\HomeMind-ubuntu-core-20260807-171744.tar.gz.sha256 homemind-ubuntu:~/transfer/
scp .\transfer\ubuntu\HomeMind-ubuntu-core-20260807-171744.tar.gz.manifest.txt homemind-ubuntu:~/transfer/
```

### 3.3 Ubuntu 校验和解压

```bash
cd ~/transfer
sha256sum -c HomeMind-ubuntu-core-20260807-171744.tar.gz.sha256
tar -tzf HomeMind-ubuntu-core-20260807-171744.tar.gz | less
mkdir -p ~/transfer/homemind
tar -xzf HomeMind-ubuntu-core-20260807-171744.tar.gz -C ~/transfer/homemind
chmod +x ~/transfer/homemind/contest2026_138_HomeMind/scripts/*.sh
chmod +x ~/transfer/homemind/contest2026_138_HomeMind/tools/*.sh
```

成功标志：`sha256sum` 显示 `OK`。如果校验失败，删除 Ubuntu 上这一份损坏的接收文件并重新传输，不要继续解压使用。

---

## 4. 第三阶段：建立正式 OpenVela 和 Git 工作区

迁移包不是完整 OpenVela 工作区，也不带有效 Git 元数据。必须先同步正式工作区，再把 HomeMind 文件合并进去。

### 4.1 检查主机资源

```bash
free -h
df -h /
nproc
```

要求和建议：

- OpenVela 放在 Ubuntu 本地 ext4 文件系统，例如 `~/work/openvela`；
- 不要在 NTFS、SMB、U 盘或网络共享目录中直接编译；
- 256 GB 磁盘建议给 OpenVela 预留 80～100 GB，并长期保留至少 20 GB 空闲；
- 8 GB 内存先固定 `repo sync -j2`、`JOBS=2`；
- 如果内存不足，先检查 swap 和失败日志，不要直接提高并发数。

### 4.2 安装同步工具

```bash
sudo apt update
sudo apt install -y git git-lfs repo rsync ca-certificates curl
git lfs install
```

### 4.3 同步比赛工作区

建议在 tmux 中执行：

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

同步完成后检查：

```bash
test -f ~/work/openvela/nuttx/Makefile && echo "nuttx OK"
test -f ~/work/openvela/packages/ai_agent/fix_esp32s3.sh && echo "ai_agent OK"
test -d ~/work/openvela/contest2026_138_HomeMind/.git && echo "contest git OK"
repo status
```

如果官方仓库要求比赛账号或 fork 权限，按大赛账号流程处理。不要在空目录中手工伪造 `.git`。

### 4.4 合并 HomeMind 当前文件

```bash
rsync -a \
  --exclude .git \
  ~/transfer/homemind/contest2026_138_HomeMind/ \
  ~/work/openvela/contest2026_138_HomeMind/
```

该命令没有使用 `--delete`，不会删除正式仓库原有文件。完成后检查差异：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
git branch --show-current
git remote -v
git status --short
git diff --stat
```

在确认差异、秘密文件和生成物以前，不要执行 `git add .`、`git reset --hard` 或覆盖性恢复命令。

建议为当前固件问题创建独立分支：

```bash
git switch -c feature/firmware-wifi-recovery
```

---

## 5. 第四阶段：初始化 Ubuntu 构建和串口环境

### 5.1 执行项目初始化脚本

```bash
cd ~/work/openvela/contest2026_138_HomeMind
./scripts/setup_ubuntu.sh
```

脚本会安装构建依赖，在项目中创建 Linux `.venv/`，并安装 `esptool 5.3.1` 和 `pyserial`。不要把 Win11 的 `.venv-tools` 复制到 Ubuntu。

需要安装项目 udev 规则时执行：

```bash
./scripts/setup_ubuntu.sh --configure-udev
```

### 5.2 串口权限

初始化脚本会尝试把当前用户加入 `dialout`。检查：

```bash
groups
```

没有 `dialout` 时：

```bash
sudo usermod -aG dialout "$USER"
```

然后退出当前 SSH 会话并重新登录。不要长期使用 `sudo` 打开串口或烧录。

### 5.3 验证环境

```bash
cd ~/work/openvela/contest2026_138_HomeMind
OPENVELA_ROOT=$HOME/work/openvela ./tools/verify-ubuntu.sh
```

寻找 ESP-IDF 环境脚本：

```bash
find "$HOME" -path '*/esp-idf/export.sh' -type f 2>/dev/null
```

记录实际路径，后续执行：

```bash
source /实际路径/esp-idf/export.sh
```

环境检查必须能识别 Git、OpenVela、NuttX、交叉编译器、项目 `.venv` 和已有固件产物。缺什么补什么，不要带着环境缺失直接修改业务源码。

---

## 6. 第五阶段：先复现官方基线，再构建 HomeMind

### 6.1 保存上游版本

```bash
cd ~/work/openvela
git -C contest2026_138_HomeMind rev-parse HEAD
git -C packages/ai_agent rev-parse HEAD
git -C nuttx rev-parse HEAD
repo status
```

把三个 commit ID 和日期写入本轮测试记录。以后每个固件结果都必须能对应到源码版本。

### 6.2 第一次只构建官方 `ai_agent` 基线

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

成功后记录：

```bash
sha256sum ~/work/openvela/nuttx/nuttx.elf
```

如果官方基线失败，先解决工具链、配置或上游版本问题；此时不要修改 HomeMind Wi-Fi 状态机。

### 6.3 构建 HomeMind 修改版

官方基线成功后：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
source /实际路径/esp-idf/export.sh

OPENVELA_ROOT=$HOME/work/openvela \
JOBS=2 \
./scripts/build.sh build
```

脚本会部署以下三个 staging 文件：

- `tools/staging-network_manager.c`；
- `tools/staging-vela_tls.c`；
- `tools/staging-http_proxy.c`。

构建日志：

```text
~/work/openvela/logs/homemind/build.log
~/work/openvela/logs/homemind/fix.log
```

构建失败时先查看最后 100 行：

```bash
tail -n 100 ~/work/openvela/logs/homemind/build.log
tail -n 100 ~/work/openvela/logs/homemind/fix.log
```

构建成功后校验：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
sha256sum -c artifacts/SHA256SUMS
```

成功标志：`nuttx.bin` 和 `nuttx.elf` 均显示 `OK`，并且哈希与旧的 2026-07-27 固件不同。

---

## 7. 第六阶段：连接开发板、烧录和打开串口

### 7.1 识别真实串口

先不插开发板执行一次，再插入 USB 数据线执行一次：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
dmesg --ctime | tail -n 30
```

不要照抄旧的 `COM3` 或默认 `/dev/ttyACM0`，必须以本机实际结果为准。

如果不知道哪个进程占用串口：

```bash
fuser /dev/ttyACM0
```

### 7.2 烧录

关闭 miniterm 等串口程序后执行：

```bash
cd ~/work/openvela/contest2026_138_HomeMind
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
```

项目脚本将固件写入 `0x0`，不会自动执行整片擦除。除非已经确认分区和恢复方案，不要随意增加全片擦除操作。

### 7.3 打开串口

```bash
cd ~/work/openvela/contest2026_138_HomeMind
./.venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

确认能够看到启动日志并进入 `nsh>`；启动 `ai_agent` 后应进入 `vela>`。

### 7.4 日志安全

- 第一次输入 `set_wifi <ssid> <password>` 时不要开启公开日志录制；
- 不要把真实命令粘贴到 README、Issue、提交信息或比赛报告；
- 原始串口日志放在仓库外，例如 `~/logs/homemind-private/`；
- 分享前搜索并删除 SSID、密码、Token 和 Authorization Header；
- 新固件必须确认 PSK 日志只显示 `<redacted>`。

---

## 8. 第七阶段：当前 P0——Wi-Fi 分层诊断与修复

本阶段只回答一个问题：ESP32-S3-EYE 为什么没有稳定获得可上网的 IPv4？

### 8.1 已知高概率原因

当前源码和状态记录给出的优先怀疑项：

1. 发送 `wapi essid` 后仅等待约 5 秒就进入 DHCP，早于历史手工测试所需的约 20 秒；
2. `WIFI_ASSOCIATED` 目前只是软件状态名，没有验证开发板是否真的完成 WPA2 关联；
3. `ifup`、`wapi mode`、`wapi psk`、`wapi essid` 即使返回失败，状态机仍可能继续进入 DHCP；
4. `renew` 只在 DHCP 状态的第一个调度点调用，存在错过首次执行的可能；
5. DHCP 只触发一次并等待约 15 秒，首次请求过早或路由器响应慢时缺少同轮重试；
6. `CONFIG_NETDEV_LATEINIT=y` 时，`ai_agent` 可能早于 `wlan0` 完全就绪；
7. 如果已经获得 IPv4，但仍不能访问域名，问题应转到默认网关或 DNS，而不是继续修改 WPA2 参数。

### 8.2 每次测试的固定顺序

在 `vela>` 中现场输入凭据：

```text
set_wifi <ssid> <password>
```

等待至少 30 秒，再执行：

```text
net_status
```

返回 `nsh>` 后按固件实际可用命令检查：

```text
ifconfig wlan0
wapi status wlan0
```

重点记录以下信息，不记录密码：

- `wlan0` 出现时间；
- `ifup`、`mode`、`psk`、`essid` 各自返回值；
- 关联等待实际用了多少秒；
- 是否真实 Associated；
- `renew` 是否真的执行；
- DHCP 是否分配非 `0.0.0.0` 的 IPv4；
- 失败发生在 `[HM-WIFI]` 还是 `[HM-NET]` 层。

### 8.3 按结果分流，不跨层猜测

| 观测结果 | 所在层 | 下一步 |
| --- | --- | --- |
| 没有 `wlan0` | 驱动/初始化 | 检查 defconfig、late init、启动时序 |
| `wlan0` 存在，但 `wapi` 命令失败 | 接口/命令 | 保留返回码，停止状态迁移，修正参数或初始化 |
| `essid` 已发送，但仍 Not-Associated | WPA2 关联 | 核对 2.4 GHz、SSID/密码、路由器加密方式和等待时间 |
| 已 Associated，但没有 IPv4 | DHCP | 手工验证 `renew wlan0`，增加可确认的 DHCP 重试 |
| 已有 IPv4，但 `net_test` DNS 失败 | DNS/网关 | 检查默认路由、DNS 地址和路由器上网能力 |
| DNS 和 TCP 成功，TLS 失败 | TLS | Wi-Fi 已通过，转入 TLS 专项，不再修改 Wi-Fi |

路由器侧先使用兼容性最高的测试配置：

- 开启 2.4 GHz；
- 使用普通 WPA2-PSK/AES；
- 暂时关闭 WPA3-only、企业认证、Portal 网页认证和 AP 隔离；
- SSID 暂时使用简单 ASCII 名称，排除空格和特殊字符转义问题；
- 确认 DHCP 地址池未耗尽、没有 MAC 黑名单；
- 可用手机热点做对照测试，但一次只更换一个变量。

### 8.4 推荐的代码修复顺序

主要修改位置：

```text
~/work/openvela/contest2026_138_HomeMind/tools/staging-network_manager.c
```

按以下顺序逐项修改和验证：

1. 启动状态机前等待并确认 `wlan0` 存在；
2. 检查 `ifup`、`mode`、`psk`、`essid` 返回值，失败时不进入下一状态；
3. 把固定 5 秒等待改为带超时的真实关联轮询，建议上限 30 秒；
4. 只有确认 Associated 后才进入 DHCP；
5. 进入 DHCP 状态时使用明确的“一次性已启动标志”调用 `renew`，不要依赖 `elapsed == 0`；
6. 在同一连接轮次中增加有限次数 DHCP 重试，并记录每次原因和间隔；
7. 获得 IPv4 后分别验证网关、DNS 和 TCP；
8. 确认所有 PSK 日志保持 `<redacted>`。

每次只改一个可验证问题，执行：

```bash
git diff --check
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
sha256sum -c artifacts/SHA256SUMS
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
```

### 8.5 Wi-Fi 阶段验收标准

以下条件全部完成，才能宣布 Wi-Fi 问题解决：

- 冷启动 5 次，5 次都能在 60 秒内获得 IPv4；
- 关闭再打开路由器或热点 3 次，设备能够自动恢复；
- 错误密码不会误报 `READY`，日志能明确停在关联层；
- DHCP 首次失败后能够按设计重试，不会永久卡住；
- `net_status` 和 `ifconfig wlan0` 的结果一致；
- 串口日志不出现明文 Wi-Fi 密码；
- 保存每次测试对应的固件 SHA-256 和三个上游 commit ID。

---

## 9. Wi-Fi 通过后的后续开发计划

### P1：DNS、TCP 和 TLS

前置条件：Wi-Fi 验收完成。

执行 `net_test`，观察日志必须依次出现：

```text
[HM-NET] DNS resolve ...
[HM-NET] TCP connect OK ...
[HM-TLS] TLS handshake starting ...
[HM-TLS] Handshake OK ...
[HM-NET] net_test PASSED
```

如果 TCP 成功而 TLS 因 NuttX socket 超时返回 `errno 11`，再检查自定义 mbedTLS BIO 是否把 `EAGAIN/EWOULDBLOCK` 正确转换为 `MBEDTLS_ERR_SSL_WANT_READ` 或 `MBEDTLS_ERR_SSL_WANT_WRITE`。TLS 修复不与 Wi-Fi 状态机修改放在同一次提交中。

验收：同一网络下连续执行 5 次 `net_test`，5 次通过，且失败路径有明确超时和错误码。

### P2：MiMo 真实文本问答

前置条件：`net_test` 连续通过。

- Token 仅在设备现场输入，不进入源码、命令历史、日志或文档；
- 先做一次最小非流式文本问答；
- 记录响应状态、耗时、内存变化和失败原因；
- 不使用代理或额外服务掩盖设备端直连问题。

验收：连续完成至少 3 次真实问答，重启后网络仍可重新建立。

### P3：配置掉电持久化

- 明确 ESP32-S3-EYE 可用分区和 NuttX 持久化接口；
- 保存 Wi-Fi 配置和 LLM 配置时分离秘密与普通配置；
- 增加版本、CRC/校验、恢复默认值和写入失败处理；
- 做断电、错误配置、清除配置和升级兼容测试。

验收：断电重启后能够自动联网；清除配置后不残留秘密；损坏配置不会造成启动循环。

### P4：HomeMind 本地工具闭环

- 将工具注册、系统提示词和最小 LED 控制真正集成进 ELF；
- 先确认 ESP32-S3-EYE 实际可安全控制的 LED/GPIO；
- 对 LLM 工具参数做白名单和范围校验；
- 保留用户确认和隐私保护边界。

验收：自然语言请求能够触发一次经过校验的本地 LED 动作，非法参数被拒绝。

### P5：Win11 小程序、UG 外壳和比赛材料

- Win11 只修改 `miniprogram/` 和工作区上级 `外壳/`；
- Ubuntu 只修改固件、构建脚本和固件文档；
- 固件稳定后再处理小程序语法、缺失图标和联动展示；
- UG 修改 `.prt` 后重新导出 `.STL`，并记录版本日期；
- 最终整理架构图、演示脚本、异常测试、固件哈希和已知限制。

---

## 10. 日常 SSH 开发流程

每次开始工作：

```powershell
ssh homemind-ubuntu
```

```bash
tmux attach -t homemind || tmux new -s homemind
cd ~/work/openvela/contest2026_138_HomeMind
git status --short
git pull --ff-only
source /实际路径/esp-idf/export.sh
```

修改后构建：

```bash
git diff --check
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
sha256sum -c artifacts/SHA256SUMS
```

烧录和调试：

```bash
SERIAL_PORT=/dev/ttyACM0 ./scripts/build.sh flash
./.venv/bin/python -m serial.tools.miniterm /dev/ttyACM0 115200
```

确认测试通过后提交：

```bash
git status --short
git diff
git add -- <本次明确修改的文件>
git commit -m "fix: improve wifi association and dhcp diagnostics"
git push -u origin feature/firmware-wifi-recovery
```

提交原则：

- 一个提交只解决一个问题；
- 不使用 `git add .` 混入无关文件；
- 不提交 `.venv/`、全量构建目录、秘密配置和原始敏感日志；
- 不直接提交每一个临时 BIN，只保留有明确验收结果的固件；
- 每次提交前执行 `git diff --check` 并查看 `git status --short`。

---

## 11. 故障快速处理

### SSH 无法连接

1. 在 Win11 执行 `ping <ubuntu-ip>`；
2. 到路由器确认 Ubuntu 是否在线、IP 是否变化；
3. 有临时显示器时，在 Ubuntu 本地执行 `ip -br address` 和 `systemctl status ssh`；
4. 不要在远程连接尚未验证时关闭密码登录或修改防火墙；
5. 系统和 SSH 恢复后再继续固件工作。

### SSH 断开后编译不见了

```bash
tmux list-sessions
tmux attach -t homemind
```

### 串口权限不足

```bash
groups
ls -l /dev/ttyACM0
sudo usermod -aG dialout "$USER"
```

注销并重新登录，不要用 `chmod 777` 作为长期解决方案。

### 找不到串口

```bash
lsusb
dmesg --ctime | tail -n 50
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

更换确认支持数据传输的 USB 线和 USB 接口，再检查开发板启动/下载模式。

### 编译时内存或磁盘不足

```bash
free -h
df -h ~/work/openvela
du -sh ~/work/openvela/* 2>/dev/null | sort -h
```

保持 `JOBS=2`。清理任何目录前必须先确认它是可重建的构建缓存，不要删除源码、`.repo` 或尚未提交的修改。

---

## 12. 近期执行清单

按顺序完成，不跨项：

- [ ] Win11 能通过 `ssh homemind-ubuntu` 稳定登录；
- [ ] Ubuntu 固定局域网地址，重启后仍可连接；
- [ ] tmux 会话能够在 SSH 断开后恢复；
- [ ] 当前迁移包 SHA-256 校验显示 `OK`；
- [ ] 正式 OpenVela 工作区和比赛仓 Git 元数据有效；
- [ ] `./scripts/setup_ubuntu.sh` 和 `verify-ubuntu.sh` 通过；
- [ ] 官方 `esp32s3-eye:ai_agent` 基线构建成功；
- [ ] HomeMind 新固件构建、校验和烧录成功；
- [ ] 串口日志确认 PSK 已脱敏；
- [ ] 完成第一轮 `wlan0 → 关联 → DHCP → IPv4` 分层记录；
- [ ] 修正真实关联检测、命令失败处理和 DHCP 重试；
- [ ] 完成 Wi-Fi 冷启动及断网恢复验收；
- [ ] Wi-Fi 验收后再进入 `net_test` 和 TLS 修复。

## 13. 相关工程文档

- `contest2026_138_HomeMind/STATUS.md`：当前事实状态；
- `contest2026_138_HomeMind/MIGRATION_UBUNTU.md`：迁移细节；
- `contest2026_138_HomeMind/docs/DEVELOPMENT_WORKFLOW.md`：Ubuntu/Win11 双机分工；
- `contest2026_138_HomeMind/scripts/README.md`：构建脚本入口；
- `contest2026_138_HomeMind/tools/serial-operations.md`：串口命令和日志层次；
- `contest2026_138_HomeMind/docs/HomeMind_大赛技术进度报告_2026-08-07.md`：大赛技术状态和风险说明。

执行过程中如实更新 `STATUS.md`：只有完成了对应验收标准的项目才能标记为“已完成”，源码存在、能够编译和真实硬件通过是三个不同状态。
