# HomeMind 当前状态

> 整理时间：2026-09-02（当前事实截止 2026-09-02）
> 目标：ESP32-S3-EYE 独立运行 OpenVela/ai_agent，自动联网，直接调用 MiMo，并安全执行最小本地工具。

## 2026-09-02 Skill runtime completion（本地完成，未推送）

- **Skill 源文件**：将 `app/homemind/skills/home_security.md` 收口为 ai_agent 当前实现使用的平铺 `.md` 格式，并增加只调用现有白名单工具的单次 15 秒主动演示定义；`tools/validate-home-security-skill.py` 静态检查通过，文件 3185 字节、低于设备安装缓冲区限制。
- **真实缺陷修复**：修正 `install_skill` 对 HTTPS URL 的解析，使其拆分为 host、port、path 后再调用 TLS；此前完整 `https://...` 字符串会被错误传给 `getaddrinfo()`。
- **新固件**：增加受限串口 Skill 导入命令后重新完成干净构建、SHA 校验和烧录；BIN `d045e2c79159152cec4f8e9f2772feeae29ad7284d7a8c8d08cb9e6c2ec8ff2e`，ELF `7c05f2c9f513d17fc67b48c7c5a87ba153f78b66cdf3be9e23773850a60cdd16`，esptool v5.3.1 `Hash of data verified`。
- **设备加载证据**：设备实际技能目录为 `/data/ai_agent/skills/`；`ask list skills` 出现 `Home Security Skill`，证明自定义 `.md` 已被 loader 发现。
- **原文安装证据**：为绕开同网段客户端隔离，固件新增 `skill_write_begin` / `skill_write_hex` / `skill_write_commit` 受限路径；将仓库 `home_security.md` 原文分块导入后，设备明确返回 `Skill committed: /data/ai_agent/skills/home_security.md (3185 bytes)`，与静态校验的源文件字节数一致。写入使用 `.part` 临时文件，最后才原子改名为 `.md`。
- **主动执行证据**：在原文安装后，用户可见响应为“安防演示已启动”“15 秒后将自动闪烁提醒一次”“任务完成后会自动删除”；等待 25 秒后停止请求确认“定时任务：已清空、LED 灯：已关闭”。这次已按仓库定义接受为一次性主动场景；仍需提交视频和更完整的原始 Cron 行时，不能用本段文字替代。
- **原始日志补充**：NuttX `dmesg` 已抓到本次固件启动、`/data/ai_agent` 存储、工具注册、10 个内置 Skill、网络 IP 和 Stop 清理记录；板端没有 `grep/tail`，未伪造 `Cron job firing` 等缺失行。完整串口过程见 `logs/hardware-2026-09-02-skill-runtime.log`。
- **安装通道限制**：临时 HTTPS 安装曾到达设备 TLS 连接阶段，但设备到家庭 Ubuntu `192.168.31.251` 的同网段访问返回 `errno=101`，服务端无请求；本次设备文件由设备内置 `write_file` 生成，不能作为仓库源文件原文安装证据。
- **清理**：循环 cron 已清除；临时 HTTPS 服务、证书和 staging 文件已删除；家庭 `gateway_agent.py` 已恢复运行。完整过程见 `logs/hardware-2026-09-02-skill-runtime.log`。
- **推送状态**：本地 `contest-final` 仍未执行远程 push；Skill/主动场景本阶段已收口，但摄像头、离线唤醒、真实米家设备和最终小程序闭环仍未完成。

## 2026-09-01 最终长稳回归（本地已完成，未推送）

- **最终修复组合**：HTTP chunked body 识别终止块；未完成/超长 body 返回读取错误；请求使用 `Connection: close` 避免复用服务端主动关闭的 stale TLS socket；TLS header/body 写入增加 15 秒总超时；释放 socket 前设置 `SO_LINGER=0`。
- **构建配置**：干净工作区启用 `CONFIG_NET_SOLINGER=y`；同时确认 `CONFIG_NET_TCP_PREALLOC_CONNS=8`、`CONFIG_NET_TCP_ALLOC_CONNS=0`、`CONFIG_NET_TCP_WAIT_TIMEOUT=120`。
- **最终产物**：BIN `69f321439158c9af88a2fa53c7aba3d28cbe77f657dbedefcdb4ba7d0a7d124a`，ELF `3e599f21dd7f6068aa40d68eb48cbb2e53fcc0db77ffc19eeb04c8cb5b8f77a3`；`SHA256SUMS` 与本地两份 `artifacts/` 均一致。
- **烧录**：Ubuntu `/dev/ttyACM0` 使用 esptool v5.3.1 写入地址 `0x0`，`Hash of data verified`，hard reset 成功。
- **最终长稳门禁**：标准自然语言请求 10/10 PASS；每轮均捕获 HTTP `200`、Agent 回复，最终 IP 均为 `192.168.31.248`；日志为 `logs/hardware-2026-09-01-stability-ok-full.log`。
- **收尾设备状态**：`session_list` 为空；`net_status` 为 `Network connected: yes`，IP `192.168.31.248`。
- **中间方案对照**：仅保持 `keep-alive` 的版本在第 4 轮复现 stale pooled socket 路径；最终主动关闭方案在同一 10 轮门禁中 10/10 通过。
- **安全与推送**：完整 MiMo key、Wi-Fi 密码和 SSH 密码未写入仓库、文档或日志；本地提交已完成，远程 push 仍未执行。

**当前结论**：设备端固件、干净构建、烧录、10 轮长稳回归、自定义 Skill 原文安装和一次性主动 LED 场景均已有本地证据；本阶段新增提交尚未推送。大赛整体仍不能宣称全部完成：摄像头端侧推理、离线唤醒、真实米家设备、MQTT 状态闭环、微信小程序真实联调以及最终视频/平台材料仍是边界项。

## 2026-09-01 暂停点（TLS body framing 修复后）

- **根因定位**：`vela_tls.c` 的 HTTP 读取在 chunked response 未识别终止块时，会继续等待长连接上的下一次读取；同时请求复用板端 keep-alive socket，可能把 stale 连接放大为 Agent 超时。该路径与 2026-08-31 的“网络/IP 正常但 HTTP/Agent 不完整”现象一致。
- **代码修复**：在正式 overlay 与 staging 副本中加入 chunked 完整性判断、异常/超长 body 返回错误，并将 HTTP 请求改为 `Connection: close`，避免跨请求复用 stale TLS socket；`SOURCE_SNAPSHOT.json` 已更新为新哈希。
- **构建与烧录**：干净工作区构建成功，BIN `70fdae2e84b01587c87c143f2da99b912bb49dd2867a8fd80dfc883e7b76e87e`，ELF `2242cb231fef3987c2b444a4e3f10214b15d1143b33082bff2fff13124e16718`；`SHA256SUMS` 自校验通过，esptool v5.3.1 写入并校验通过。
- **冷启动回归**：5 轮均完成强 AP 选择、DHCP `ret=0`、TLS 握手、HTTP 200 和最终联网状态；自定义 token 提示被模型安全策略拒绝，故脚本语义计数为 0/5，但未再出现传输超时。
- **自然语言连续回归**：启动 PASS；第 1–7 轮均 PASS（HTTP 200、Agent 回复、最终 IP `192.168.31.248`）。用户要求暂停后已停止后续测试进程，故尚未宣称 10/10 完成；原修复前基线为 6/10。
- **安全与推送**：完整 API key、Wi-Fi 密码和 SSH 密码未写入仓库；本地官方仓尚未提交本次改动，远程未推送。

**明日第一步**：从当前固件继续/重跑标准自然语言长稳回归，补足 10 轮；随后复核受控 `wifi_reconnect`、异常路径和最终源码/日志提交，仍不推送远程，直至用户明确要求。

## 2026-09-01 续接回归结果（中间记录，已被最终回归覆盖）

- **暂停后直接续跑**：3 轮自然问答严格采集为 0/3；每轮 Agent 有输出、最终 IP 正常，但窗口内未捕获 HTTP 状态行。该结果已保留为异常采集证据，不计入通过。
- **原始单次诊断**：硬复位后会话列表为空；“只回复 OK”完整捕获 TLS 1.2、HTTP 200、`[Agent]: OK`，最终联网状态正常。
- **连续累积诊断**：从硬复位开始连续 4 轮自然问答 4/4 PASS；每轮均捕获 TLS 握手、HTTP 200、Agent 回复和最终 IP `192.168.31.248`。
- **受控重连诊断**：硬复位后执行 `wifi_reconnect` 再自然问答，3/3 PASS；每轮均包含内部 `ifdown_post_scan ret=0`、DHCP 恢复、HTTP 200、Agent 回复和最终 IP。
- **串口节点**：设备 USB 串口从 `/dev/ttyACM1` 重新枚举为 `/dev/ttyACM0`，已在测试脚本中显式指定；不是固件或网络故障。
- **当前结论**：上述为最终收口前的中间证据；完整 10/10 门禁结果以本文档顶部的最终回归章节为准。

## 2026-08-30 M0 合规收口（本提交）

- **官方仓收口**：从 Ubuntu 官方仓 bundle 建立干净工作副本（`dev-ai-contest-2026` 基础上新建本地分支 `contest-final`），旧 detached-HEAD 现场未做任何改动，可随时回退对比；本提交前**未推送远端**。
- **固件源码收口**：`firmware/ai_agent_overlay/`（16 个 ai_agent 文件精确 overlay + `SOURCE_SNAPSHOT.json` 溯源）与 `firmware/patches/0001-homemind-esp32s3-nuttx.patch`（相对 NuttX 基线 `dd92bcf4`，491 行，已在干净文件树 dry-run 通过，可重复执行幂等）。
- **构建产物**：原提交中的 `artifacts/nuttx.bin|elf` 与 `SHA256SUMS`（`418044b0…`/`439c0006…`）自校验一致；2026-08-31 已由干净工作区重建并更新为 `902ccdaa…`/`c10e1e64…`，详见下方构建记录。
- **秘密审计**：全树扫描无 API key、密码、私钥、PAT 等真实凭据（32 处命中均为 `<ssid> <password>` 类帮助占位符）；`fei` 为官方基线 feishu 组件名，非敏感。历史泄露过的 Wi-Fi/登录凭据**仍需轮换**。
- **假数据清理**：小程序固定温湿度/光照/噪声与假成功逻辑已移除，dashboard 以 `'--'` 占位并明确标注"未接入"；`backend/data/`、`server/data/` 等本地运行数据已加入 `.gitignore`。
- **官方模板**：`docs/submission/2026首届openvela_AI硬件开发者大赛_作品提交模板_官方原版.docx` 已下载并验证，并已建立"只填写事实"的工作副本 `HomeMind_作品提交模板_事实工作副本.docx`（信息表/摘要/AI-Native 表已填已验证事实，未确认项显式标注待补充）；官方示例日志占位（`logs/your-github-login/`）已删除，AI 日志导出待真实 GitHub 登录名确认后补齐。

## 2026-08-31 干净工作区真实复现（构建与真机验收完成）

**背景**：M0 文档收口已全部提交至本地分支 `contest-final`（`dc4caeb` M0 主提交 → `21fa4d7`/`e3f6920` 模板与状态 → `65b4f54` AI 日志 → `9050a39` 脚本可执行位）。**未推送远端**。AI 日志已导出 6 个真实 Codex 会话至 `logs/2760216167@qq.com/`（manifest 含 12 个整文件剔除记录及原因）。

**已核实的关键事实（Ubuntu 侧）**：
- 旧生产工作区 `~/work/openvela` = NuttX 基线 `dd92bcf` + 未提交的 HomeMind 修改；`vendor/openvela/boards` 为空占位（板级支持在 nuttx 树内）；apps 内 mbedtls/cJSON/littlefs 等第三方源码为**未跟踪的离线布置**（repo checkout 不会带出）；旧区 `nuttx/.git` 元数据损坏（rev-list 挂死），勿再对其做 git 操作。
- 干净工作区 `~/work/openvela-clean-20260830`：manifests 走本地 bare（`~/work/homemind-manifest-local.git`）；repo 相对 fetch `../open-vela/` 解析到 `/home/hfy/open-vela`，已建为 20 个项目镜像（objects/info/alternates 指向旧区对象库，零拷贝、旧区只读）；repo 工具经 `url.insteadOf` 重定向到本地镜像 `_git-repo.git`。repo sync 已完成 10 个 git 项目 + contest-final（65b4f54，经 bundle `~/work/transfer/contest-final.bundle` 接入）+ extras 补拷（mbedtls/cJSON 等，日志 `~/work/clean-extras.log`）。
- **部署已通过**：`tools/deploy-to-vm.sh` 在干净区执行成功——16 个 overlay 文件安装 + NuttX 补丁"已应用"校验通过（= 补丁与旧区树逐字节一致的复现性证据）。kconfig 全部调整与 TLS1.3 关闭已生效。
- **第三方离线依赖补齐**：目录同步遗漏了 `apps/crypto/mbedtls/v3.4.0.zip` 与 `apps/netutils/cjson/v1.7.12.tar.gz` 两个根目录归档；已从旧区复制到干净区并完成 SHA-256 校验。首次重跑因此失败；随后将两个残留解包目录按时间戳移出并保留备份，再从归档重新解包。
- **干净构建已完成**：在 `/home/hfy/work/openvela-clean-20260830` 使用 `OPENVELA_ROOT=~/work/openvela-clean-20260830 JOBS=2 ./scripts/build.sh deploy` 后执行 `build`，日志标记 `BUILD-OK` / `BUILD-ENTRY-DONE`。最终产物：BIN `902ccdaa2109d8a2b1ecc8d5247b58d4788078eb4689e7390ee4124ec115ef56`，ELF `c10e1e64bee0f989accd4dacf6fae6ce8327ead8d386aeda7b87835ab9fd8e87`。
- **哈希关系**：新干净构建与官方仓此前产物 `418044b0…` / `439c0006…`、STATUS 中历史烧录固件 `ee22ee60…` 均不一致；因此旧哈希视为历史构建/烧录记录，不能冒充本次干净构建。官方仓本地 `artifacts/` 已更新为本次构建结果并自校验一致。
- **MiMo 真机验收已完成**：用户提供的 key 仅通过远端临时串口命令写入设备；`config_show` 只显示 `sk-c****`，配置为 `mimo-v2.5` / `api.xiaomimimo.com`。一次真实 `ask` 完成 TLS 1.2（约 580 ms）、HTTP 200，并返回唯一短语 `homemind-mimo-ok`。
- **重启持久化已完成**：硬复位后等待网络稳定，`net_status` 仍为 `192.168.31.248`，`config_show` 仍显示 MiMo 配置；未重新输入 key 的 `ask` 完成 TLS 1.2、HTTP 200，并返回 `homemind-mimo-restart-ok`。
- **当前边界（修复前基线）**：完整 key 未写入仓库、状态文档或日志；本次新构建已通过 LED 开/关快速路径，冷启动 5/5 与受控重连 3/3 已通过；标准短回复长稳修复前为 6/10 在 75 秒内完成 HTTP 200 与最终 Agent 回复，4/10 未完成请求，且 10/10 轮最终网络/IP 均正常。2026-09-01 已完成 TLS body framing/stale socket 修复并得到 7/7 新轮次通过，长稳门禁待补足。

**下一步**：① 补足修复版标准自然语言长稳 10 轮；② 复核受控重连、本地 LED 工具及异常路径；③ 本地提交并检查脱敏记录；④ 推送 `contest-final` 至 GitHub 远端前仍需用户确认。

## 2026-08-31 连续性回归（修复前基线，未推送）

- **冷启动 5/5 PASS**：每轮 esptool 硬复位后自动启动 agent；最终 `net_status` 均为 connected，DHCP IP 均为 `192.168.31.248`，MiMo TLS/HTTP 200 与唯一 token 均通过。复位后首次立即采样可能仍显示 `no/0.0.0.0`，属于 DHCP 尚未完成；问答完成后的最终采样均恢复为 `yes`。
- **受控重连 3/3 PASS**：每轮硬复位并确认在线后执行受支持的 `wifi_reconnect`；三轮均提取到内部 `ifdown_post_scan`、DHCP 恢复、最终 connected/IP，以及 MiMo HTTP 200 与唯一 token。
- **外部断网边界**：`ifdown wlan0` 在 `vela>` 控制台返回 `Unknown command`，因此没有把它写入当前固件的外部断网通过结论；当前 3/3 是状态机自带的受控重新关联路径验收。
- **长稳观察（标准短回复，10 次）**：这是 2026-09-01 修复前基线：单次硬复位后，10 轮最终网络状态/IP 均正常；6/10 轮在 75 秒窗口内同时捕获 HTTP 200 和最终 Agent 回复，4/10 轮只出现工作状态，未完成 HTTP/Agent 回复。失败轮次不伴随 DHCP/IP 丢失。
- **自定义 token 对照**：45 秒窗口的唯一 token 校验为 5/10；延长至 75 秒的 3 轮均出现 HTTP 200，但模型未稳定复述指定 token。清空 `console` 会话后请求“只回复 OK”曾返回 HTTP 200 与 `[Agent]: OK`，故该现象不能单独归因于 Wi-Fi 或串口采集。
- 所有上述结果均来自本次干净构建 BIN `902ccdaa2109d8a2b1ecc8d5247b58d4788078eb4689e7390ee4124ec115ef56`；完整 API key、Wi-Fi 密码和 SSH 密码未进入仓库、文档或日志。

**访问方式**：`python transfer/sshx.py <cmd文件> [超时秒]`，需先设置 `HOMEMIND_SSH_PASSWORD` 环境变量（密码不落盘）。已知坑：paramiko 通道静默超时会掐断长命令，长任务一律 `nohup … & echo PID` 后轮询日志；`pgrep -f` 会自匹配，结束判断看日志标记（STAGE-DONE / BUILD-ENTRY-DONE / EXTRAS-DONE）。

## 总体判断

Ubuntu 主机、OpenVela 工作区、固定依赖、串口权限和服务器地址 `192.168.31.251` 均已验证可用。2026-09-01 最终修复版干净构建已烧录到 ESP32-S3-EYE：设备锁定强 AP `50:88:11:7a:02:69`，通过 DHCP 获得 `192.168.31.248`；冷启动传输链 5/5、连续自然问答 7/7 + 4/4、受控重连自然问答 3/3，以及最终标准自然语言长稳 10/10 均有通过证据。当前仅保留远程推送未执行这一项，等待用户明确要求。

## 2026-08-30 续接（小程序 BOM 修复；LCD 状态图标显示上线；C1.0 计划交付）

### 0. 屏幕泛白根因与修复(2026-08-30 补充)

用户反馈屏幕泛白以为屏幕坏了——实际是 `CONFIG_LCD_ST7789_INVCOLOR=y` 在这块
面板上把颜色整体反转(绘制的纯黑背景显示为白色,图标其实已正常渲染,只是反色)。
已关闭 INVCOLOR 并按用户要求改为**全屏蓝色背景**(RGB565 0x001F),气泡内三个
点改为白色提高对比。渲染验证:`rendered wifi=0→1, led=0` 正常。
当前固件:BIN `ee22ee60e064fbbef5662e9c4788ac9fbf4510f8f9e6da7512bc0f25cfb57440`。
→ 请看一眼屏幕:应为蓝色背景 + 三个图标(Wi-Fi 绿弧/ MiMo 绿气泡白点 / LED 灰圈)。

### 1. 小程序编译失败已修(等用户重新编译)

微信开发者工具报 `app.json SyntaxError: Unexpected token in JSON at position 0` =
**UTF-8 BOM**。此前校验用了 `utf-8-sig` 把 BOM 剥掉所以没抓到。已从小程序全部
17 个文本文件(json/wxml/wxss/js/md)剥离 BOM,JSON 重新校验通过。
→ 请在微信开发者工具中重新编译;若还有错误发截图。

### 2. 设备侧 LCD 状态图标显示上线

ST7789 240x240 屏此前完全空白(LVGL 未启用,qrcode 是死代码)。新增
`ui/hm_lcd_display.c`(无 LVGL,直接 /dev/fb0 + FBIO_UPDATE):

- 三个状态图标:**Wi-Fi**(绿弧+点=已连,红叉=断)、**MiMo 气泡**(绿=API key
  已配置,灰描边=未配)、**LED 圆点**(绿=点亮,灰圈=灭);
- 低优先级线程 5 秒轮询,**仅状态变化时重绘**(FBIO_UPDATE 全屏推送);
- dmesg 证据:`240x240 bpp=16 stride=480` → `rendered wifi=0→1` → `led=0→1`,
  三个状态迁移全部实测联动(Wi-Fi 自动重连、打开灯后图标变化)。
- 屏幕上现在应显示:绿 Wi-Fi 弧线 + 绿气泡 + 绿 LED 点(当前灯是亮的)。

### 3. C1.0 计划已交付(WORKBUDDY 执行)

新文档 [docs/C1.0-腾讯云资产与后端计划.md](docs/C1.0-腾讯云资产与后端计划.md):
资产确认清单(6 项)→ 最小云端部署(4 容器)→ 家庭网关 gateway-agent →
小程序联通,含接口契约与红线。需用户提供的资产:腾讯云实例/域名备案/证书/小程序 AppID。

### 当前固件

- BIN `284121222b618718dccbde9fd76349d1f1ec50122a3744e4bc517e07f0817962`
  ELF `92439878edcadb85d7a35bb1ffaf1b2cf3b72702219fd1e8adf913026c34d594`(LCD 显示版)。

## 2026-08-29 深夜续接 2（Win11 侧：C1 小程序恢复到"可编译就绪"）

按总体计划 5.5 节的小程序恢复顺序，本轮完成（Win11 本目录）：

1. **5 个页面脚本语法修复**（index/dashboard/devices/calendar/settings + app.js，共 11 处）：
   模板字符串丢反引号（`url: ${...}`、日期/时间拼接）、计算属性键被截断
   （`[ewCalendar.]`）、about 弹窗未加引号——均为早期 shell 转义损伤；
   全部通过 esprima JS 语法解析（parse OK 6/6）。
2. **sitemap.json 补齐**；**tabBar 10 个图标**（81x81 PNG，灰色/蓝色两套，
   home/dashboard/device/calendar/settings）用脚本生成到 `images/`，与 app.json 引用一一对应。
3. **删除硬编码局域网地址**：app.js 的 `192.168.1.100` HTTPS/WS 地址清空，
   改为启动时从设置页 Storage 恢复；上线前替换为微信后台登记的正式域名（计划 5.5.2 边界）。
4. 三个空组件目录（calendar-item/device-card/status-bar，无文件且无组件引用，
   仅同名 CSS 类）已移除；页面四件套齐全、全部 JSON 校验通过。
5. 小程序 README 已重写为当前状态 + C1 对接预留（含"小程序不持有
   AppSecret/证书私钥/MiMo Token"等安全边界）。

### 待用户动作

- 在微信开发者工具中打开 `contest2026_138_HomeMind/miniprogram/`，填入真实
  AppID（`project.config.json` 占位符 `your-appid-here`），完成首次干净编译；
- C1.0 云端资产确认仍需用户提供：腾讯云计算资源、域名备案状态、证书覆盖、
  小程序 AppID/AppSecret 主体。

### 已知不阻塞项

- index.wxml 的 `{{calendarItems}}` 与 data 的 `devices` 不匹配（渲染为空），
  属界面打磨，等 C1 接口定型一起做。

## 2026-08-29 深夜续接（本地工具扩展：get_device_info + get_button 验收通过）

- **get_device_info**（tool_device.c）：聚合运行秒数（CLOCK_MONOTONIC，规避 SNTP 跳变）、heap free/used（mallinfo）、当前 IP（network_get_ip）、固件版本串。LLM 调用实测回答："设备已运行约 **1 分钟 13 秒**（73 秒）"——与实际开机时间吻合。
- **get_button**：读 BOOT 键（GPIO0 上拉，板级注册为 `/dev/gpio1`，按下=0）。LLM 实测回答："BOOT 按钮当前状态：**未按下**（released）"。
- LED 快路径回归：`ask 打开灯` → `{"led":"on"}` 仍正常。
- 当前固件：BIN `aacdd22a58659454575736f843a5041ebb679cb362b0401f2f61c9de46b5e3f6`，ELF `2961ece89474ae1177a7732b112bf14dbaa41bbc8079748b2f380deb1ac09781`。
- 本地工具清单（均可被 LLM 自主调用）：led_control、get_device_info、get_button；其中 led_control 另有中文关键词快速路径。

## 2026-08-29 晚间续接（HomeMind 本地工具落地：LED 真实控制全链路打通）

### 实现

- **硬件**：ESP32-S3-EYE 板载 LED 在 **GPIO3**（`esp32s3_gpio.c` 注释明确 "GPIO3 is the LED"），板级驱动将其注册为 `/dev/gpio0`（输出，BOARD_NGPIOOUT=1）；BOOT 键 GPIO0 为 `/dev/gpio1`。启用 `CONFIG_DEV_GPIO` 即可，无其它依赖。
- **新工具 `led_control`**（`tool_led.c/.h`，注册进 tool_registry）：action 枚举 on/off/toggle/status，直接 open `/dev/gpio0` + `ioctl(GPIOC_WRITE/READ)`，返回 `{"led":"on"}` 等 JSON；设备不可用时返回明确 error。
- **NL 快速路径**（agent_loop 意图表）：新增 打开灯/开灯/关灯/关闭灯/turn on(off) the light|led 等关键词 → 直接调用 led_control，不经 LLM（与既有时间/电量快路径同模式）。
- 构建清单（ai_agent Makefile CSRCS）与 build.sh（CONFIG_DEV_GPIO）同步更新。

### 验收（终版固件 BIN `9c70d173416c46d34da3dcf0433caf3f766c3fd55985ab8f2a19eadbf78cb751`，ELF `5b4c2861e59e04712dda6b84151c981e8d4d5764765852ed5a2ebb9de79df5cb`）

1. `/dev/gpio0` 存在 ✓；
2. 快速路径：`ask 打开灯` → `[Agent]: {"led":"on"}`（真实点亮），`ask 关灯` → `{"led":"off"}` ✓；
3. **LLM 工具调用链路**：`ask please toggle the onboard LED and tell me its state now` → 模型自主调用 led_control 工具 → 硬件执行 → 回复 "LED 已切换，当前状态：**亮起** 💡" ✓。

至此"工具进入 ELF、注册并控制确认过的 LED"全部达成，且工具调用已验证可被 LLM 自主决策触发。

### 队列状态

- 已完成：MiMo 问答 P0 ✓、冻结竞态（诊断 printf + PSRAM 栈两案）✓、自动联网+Flash 持久化 ✓、冷启动 5/5 ✓、受控重连 3/3 ✓、wapi show 噪音 ✓、**本地工具(LED) ✓**
- 下一批：更多本地工具（BOOT 键 GPIO0 读取/get_device_info 等）、key/密码 flash 明文混淆（低优先级）、C0 冻结后推进 C1（腾讯云/小程序）。

## 2026-08-29 下午续接（历史记录；非当前固件证据）

> 本节保留旧现场记录。当前固件的连续性结论以 2026-08-31 上方章节为准；当时记录的 `ifdown wlan0` 入口不适用于当前 `vela>` 控制台。

### DHCP 冷启动 5/5 与断网恢复 3/3 —— 全部通过

测试方式（脚本 `continuity`/`recovery`，串口自动化）：

- **冷启动 5/5**：每轮 esptool 硬复位（模拟断电）→ `ai_agent` → 轮询 `net_status` 等自动联网（持久化凭据+自动 DHCP）→ `ask reply with just: cbN-ok` → 校验 MiMo 真实回复含唯一 token。**5/5 PASS**。
- **断网恢复 3/3**：agent 运行中 `ifdown wlan0` 强制断链 → 状态机 READY 态检测到断线自动重连（重新关联+DHCP）→ 轮询恢复 → 唯一 token ask 校验。**3/3 PASS**。
- 注：首轮恢复测试失败是测试脚本自身缺陷（Part 1 每轮 quit 后 agent 已退出、状态机不存在），修正后全过，固件无问题。

### wapi show ret=-1 误报清理

`wapi show` 在状态机里纯属诊断打印（返回值从未被使用），且 NuttX wapi 实现在成功打印后仍返回 -1，导致每次连接日志都出现 `[HM-WIFI] show ret=-1` 误报。已从 staging 的 ASSOCIATED 态移除该调用——关联成败的真实反馈本来就由紧随的 DHCP renew 结果给出（这也回答了"真实关联状态判断"：固定 2s 只是稳定延迟，成败判定靠 DHCP 结果+重试退避环，5/5+3/3 验证其可靠性）。

### 当前固件

- BIN `84c1b8c5a786c02ace69be09d6b7a4f016542058de2c7352afba96bdef5638ce`（含 wapi show 清理），烧录并冒烟通过（复位→自动联网→真实回复）。

### 队列状态

- ~~DHCP 冷启动 5/5~~ ✓、~~断网恢复 3/3~~ ✓、~~wapi show 误报~~ ✓、~~自动联网/持久化~~ ✓、~~冻结竞态~~ ✓、~~MiMo 问答 P0~~ ✓
- 下一批：HomeMind 本地工具注册（LED 等受控硬件）、key/密码 flash 明文混淆（低优先级）、C0 冻结后按总体计划推进 C1（腾讯云/小程序）。

## 2026-08-29 续接（Flash 持久化完成并验收通过，另破三案）

昨日遗留的三个问题全部定位修复，持久化端到端验收通过。

### 三个新根因（按发现顺序）

1. **ask 静默无回复 = agent_loop 从未启动**。`network_watch_task` 只等网络 30 秒，超时后放弃且永不重试；而"先启 agent 后配网"的场景（无保存凭据/手工 set_wifi 较晚）里 agent_loop 永远不启动，ask 消息在总线里石沉大海。旧 TMPFS 时代大家总先在 NSH 配网再启 agent，所以从未暴露。**修复**（agent_main.c，非 staging 管理）：超时后改为 5 秒轮询持续等待，网络一通即启动全部服务，并响应 shutdown。
2. **config_store 全量读改写文件的脆弱性**：`claw_config_set` 每次都 load_json→改→save_json；一次瞬时 fopen 失败返回空对象后，后续 save 会把整个文件覆盖成只剩新键（实测：Wi-Fi 凭据保存把刚写完的 LLM 配置全部抹掉）。**修复**（config_store.c）：增加 s_lock 保护的 RAM 缓存作为运行时事实源，init 时读文件一次，get/set/del 全走内存，文件只是持久化镜像；save 失败仅报错不清数据（RAM 保留真相，下次 set 重试落盘）。
3. **PSRAM 栈任务直接调 flash 写 = 整机冻死（解开全部历史"冻结"悬案）**。`CONFIG_MM_REGIONS=2` 使 kmm 堆横跨 DRAM+PSRAM，agent 的 16KiB 任务栈几乎必然落在 PSRAM；`esp32s3_spiflash_mtd.c` 在 `CONFIG_ESP32S3_SPI_FLASH_SUPPORT_PSRAM_STACK` 未开启时直接从调用任务调 `spi_flash_write/erase_range`，擦写窗口内 flash cache 挂起，调用者自己的 PSRAM 栈不可访问 → 整机死。NSH 任务（小栈在 DRAM）写文件从不复现，agent 任务写配置偶发复现，正是这个差异。**修复**：build.sh 开启 `CONFIG_ESP32S3_SPI_FLASH_SUPPORT_PSRAM_STACK`，驱动检测到 PSRAM 栈自动转投 DRAM 栈 work queue 执行 flash 操作。昨天的"config_show 冻死"（实为 config 写触发）及更早的部分无回显死机应均属此案。

### 配套改进

- **RAMLOG**：启用 `CONFIG_RAMLOG`+`CONFIG_RAMLOG_SYSLOG`（16KiB，替代仅 196 字节的 SYSLOG_BUFFER），NSH `dmesg` 可读 syslog——本轮取证的关键工具（`[askdbg]` 临时跟踪已撤除）。
- build.sh 增加 `make olddefconfig` 步骤（新启用符号的默认值物化）。
- littlefs v2.5.1 源码离线布置于 `nuttx/fs/littlefs/littlefs/`（jsDelivr 按文件取 + 手工 `git apply -p3 --directory=fs/littlefs/littlefs` 打两补丁 + 假 `.git` 跳过下载）。

### 验收结果（2026-08-29，终版固件）

- 当前烧录产物 SHA-256：BIN `3fa1ad09a36f7c67992068682a6ff42b7b2c395d0b4e79af58ba3d35591bbe1a`，ELF `e75ab3df1c94d41f57747c1ae4313fbf34d94e110614ea2c84d48aff0a1eb1c6`；
- **持久化回环**：断电复位（esptool 硬复位）→ `ai_agent` → net_watch 用持久化 Wi-Fi 凭据自动关联+DHCP（192.168.31.248）→ 用持久化 LLM 配置直接 `ask` → MiMo 真实回复 `roundtrip-final-ok`，全程零手工配置；
- `config.json` 持久化内容完整：wifi_ssid/wifi_pass/llm_backend_0/llm_host/llm_path/llm_port/api_key/model（注意：API key 与 Wi-Fi 密码在 flash 中为明文，比赛阶段可接受，报告需说明）；
- PSRAM 栈保护开启后，set_wifi/set_llm/凭据保存等全部 flash 写路径无一次冻死。

### 已知遗留

1. `restart` 软重启后 USB CDC 控制台可能不恢复（console 静默）——用 esptool chip-id 硬复位即可恢复；建议断电/复位验证一律走 esptool 流程；
2. 偶发的自动重连耗时可能超过 80s（本轮实测一次），属关联+DHCP+服务启动的正常波动，不是故障；
3. 上次烧录的净版固件 BIN `d2212532...` 由本版取代。

### 下一步（回到原队列）

1. DHCP 冷启动 5/5、断网恢复 3/3 连续性测试（自动联网已具备条件）；
2. 关联状态判定改为真实 ESSID/BSSID 判断、`wapi show` ret=-1 误报清理；
3. HomeMind 本地工具注册（LED 等受控硬件）；
4. MiMo API key / Wi-Fi 密码的 flash 明文存储可评估轻量混淆（比赛优先级低）。

## 2026-08-28 晚间续接（Flash 持久化推进中，被新冻结阻塞——任务暂停于此处，待续）

> 状态：**进行中**。净版固件（无持久化，BIN `d2212532...`）的冻结修复已验收；持久化固件功能可用但被一个**新的确定性整机冻死**阻塞，明天继续排查。

### 已完成

1. **Flash 持久化方案落地**：启用 `CONFIG_ESP32S3_MTD` + `CONFIG_ESP32S3_SPIFLASH` + `CONFIG_ESP32S3_SPIFLASH_LITTLEFS`，MTD 分区 0x180000..0x280000（1 MiB，4MB flash 内、固件 ~0xFD000 之后）；板级 `esp32s3_board_spiflash.c` 的 LITTLEFS 挂载点从 `/mnt/spif` 改为 `/data`（1 行改动，ai_agent 全部 `/data` 硬编码路径自动持久化，agent_main 的 tmpfs 兜底保留）。board bringup 首启 forceformat。
2. **build.sh 增加 olddefconfig 步骤**：新启用符号（ESP32S3_MTD/FS_LITTLEFS 子项）的默认值必须物化，否则编译报 CONFIG_ 未定义。
3. **littlefs v2.5.1 源码离线布置**：GitHub 直连失败（curl HTTP/2 报错），从 jsDelivr CDN 按文件取 `lfs.c/lfs.h/lfs_util.c/lfs_util.h` 放入 `nuttx/fs/littlefs/littlefs/`，手工 `git apply -p3 --directory=fs/littlefs/littlefs` 应用两个补丁，并放置假 `.git` 文件让 Make.defs 跳过下载。
4. **发现并修复 agent 内 DHCP argv bug（根因反转）**：NuttX `task_spawn()` 会自动把任务名作为 argv[0]（`nxtask_setup_stackargs` 把调用方 argv 排在其后），所以正确写法是 `{dev, NULL}`（renew_main 收到 argc=2 `{"renew", dev}`）；STATUS #25 那次"修复"加的 `"renew"` 前缀反而造成 argc=3 "Invalid number of arguments"。已在 staging + live 两处改回 `{dev, NULL}`。修复后 agent 内 `set_wifi` 实测拿到 `192.168.31.252`。

### 被阻塞点：新的整机冻死

- **触发**：agent 启动后执行 `config_show` → 整机冻死（USB CDC 控制台死透，须 esptool 硬复位恢复）。确定性复现：agent 启动后 ~17s、或静置 90s 后执行均死。
- **已排除**：net_watch 并发（staging 里临时 `return ERROR` 禁用 reconnect 后仍冻）；flash 写本身（NSH 下 `echo > /data/test.txt`、cat、rm 全部正常）；config_store 锁纪律（四函数均有配对 unlock）；并发 printf（净版固件已清）。NSH 单独使用 littlefs 一切正常。
- **当前固件 trace 证据**（临时诊断构建，config_store 全函数带 [CFG-TRACE]）：agent 启动初始化的 ~19 个 `claw_config_get`（fopen 打不开→空对象路径）全部正常完成；最后一次 trace 运行冻死点在 **config_show 第一个 key 的 get 打印中途**（"[CFG-TRACE] get fei" 半行即断）——控制台输出在打印中途死亡，像是 USB CDC/控制台底层死掉，而非应用层死锁。注意最后两次复现都发生在"flash + chip-id 双重复位"之后，**不排除 USB CDC 未枚举的假阳性**（STATUS 已知坑），明天复测时务必每次只做一次 chip-id 复位并先跑 probe_min.py 确认控制台活着。
- **遗留疑点**：`/data` 下有个 `ai_agent/` 目录（drwx------，agent_main 不创建，来源待查）；阶段 1 的 set_llm 写 config.json 实际未持久化成功（目录为空，save_json 失败被静默吞掉，"API key saved" 是假象）。

### 板上当前固件（临时诊断版，明天先重建净版）

- BIN `e954b8ac...` 之后的临时 trace 构建（含 CFG-TRACE + reconnect 禁用），**不可作为交付**。
- **明天第一步**：还原临时改动——①`fix_trace_cfg.py`/`fix_trace_cfg2.py` 加的 CFG-TRACE printf 全部撤销（config_store.c、nsh_commands.c）；②staging-network_manager.c 里 "TEMP: reconnect disabled" 恢复正常实现（grep TEMP 找到）。然后按净版流程重建。
- 板子已复位到 `nsh>`（console 第二次 capture 有响应；第一次 capture 0 字节是已知 USB CDC 复位时序问题）。

### 恢复后的验收路径（阶段 2）

1. 重建净版持久化固件 → 烧录 → 一次 chip-id 复位 → probe_min 确认 console 活；
2. `ai_agent` → `set_wifi <ssid> <password>`（修复后 agent 内 DHCP 应拿 IP）→ `ask` → `config_show`（若仍冻死，按上面疑点继续：先查 config_store 改用 open/read 而非 stdio、以及 `ai_agent/` 目录来源）；
3. `restart` → 重启后确认 /data 配置仍在 → `ai_agent` 自动重连拿 IP → `ask`（全程无手工 set_wifi/set_llm）→ 持久化验收完成；
4. 更新本文件并继续原队列（关联状态判定、DHCP 冷启动 5/5、本地工具注册）。

## 2026-08-28 日间（净版固件 + 冻结修复验收 + MiMo 复测通过）

### 板子/服务器状态

- 服务器 2026-08-28 凌晨重启过（uptime 7 分钟时接入），开发板随服务器断电重启；TMPFS 中 Wi-Fi/LLM 配置全部丢失（再次印证必须做 Flash 持久化）。
- Wi-Fi 配网流程（ifup -> wapi mode/psk/ap/essid -> renew）在新固件上一次性拿到 `192.168.31.252`。

### 冻结竞态根因（本轮定位，与此前观察全部吻合）

冻结 = "无回显死机"，即 USB CDC 控制台读/回显路径被打断。控制台是单一设备，多上下文并发 printf 是已知致死模式（agent_main.c 的 `g_stdout_lock` 注释即为此而设）。本轮确认固件里存在**四类非控制台任务上下文的异步 printf**：

1. `esp32s3_wlan.c`：`wlan_transmit()` 里每个发送报文打一条 `[HM-WLAN-TX] send len=...`，`wlan_tx_done()` 每次完成打 `[HM-WLAN-TX] tx_done`，ENOMEM 重排队与 60s TX 超时也各有一条——全部跑在 WLAN 工作队列上下文，任何后台流量（ARP/多播/TCP 重传等）都会随时触发；
2. `esp32s3_wifi_adapter.c`：`esp_wifi_sta_send_data()` 失败路径的 `[HM-WLAN-TX] internal_tx FAIL`；
3. `nuttx/net/tcp/tcp_send_buffered.c`：临时诊断把 ninfo 重定向为 `printf("[TCP-WRB] ...")`（CONFIG_NET_TCP_WRBUFFER_DEBUG），每个 TCP send/ACK/重传事件都在网络栈上下文打印（实测空闲时也异步打出过 "Lost connection"）；
4. `CONFIG_MBEDTLS_DEBUG`：mbedTLS 调试回调 `[HM-TLS-DBG]` 在 TLS 握手期间高频打印。

机制：空闲不敲命令时不碰控制台输入处理，所以"静置 60 秒不发命令不死"；一旦在后台报文打印的瞬间发送命令，读/回显与异步写相撞即死机；+3s 发 ask 只是恰好落在竞争窗口之外。概率性、偶发的表现与此完全一致。

### 修复（已全部落在源码 + build.sh）

1. 删除 esp32s3_wlan.c 三处、esp32s3_wifi_adapter.c 一处 `[HM-WLAN-TX]` printf；ENOMEM/TX 超时改为 `nerr()/nwarn()`（走 syslog 缓冲，不碰控制台）；
2. build.sh 配置步骤改为 `--disable CONFIG_MBEDTLS_DEBUG_C` / `--disable CONFIG_MBEDTLS_DEBUG`，并新增 `--disable CONFIG_NET_TCP_WRBUFFER_DEBUG`（tcp_send_buffered.c 里的重定向宏保留但失去开关即失效）；
3. agent_loop.c 三处 `[HM-LLM]` printf 改为 syslog（错误路径信息仍保留在 syslog 缓冲中）。

### 净版固件验收（2026-08-28）

- 当前烧录产物 SHA-256：BIN `d221253278384adbf5522e247b73c7a7a8c094088f573b1e7efdd91278033375`，ELF `494f8f895f12320d23e4c7b089f647788093b1fb755a211bd301bfbad051c4cd`；
- NSH 流程配网 -> DHCP `192.168.31.252` -> ai_agent -> `net_test www.baidu.com 443`：TLS 1.2（ECDHE-RSA-AES128-GCM-SHA256）560ms，HTTP 200；
- 全程串口零 `[HM-WLAN-TX]`、零 `[TCP-WRB]`、零 `[HM-TLS-DBG]`（此前每包刷屏已消失）；
- 冻结复现探针：agent 内静置 15s、40s 后各发一条命令，控制台均正常响应（本轮未复现冻结）；
- 本固件同时包含 2026-08-27 晚的三项修复（USBCDC 256 缓冲、mimo-v2.5 预置、单调时钟看门狗）与 DMA 池修复。

### 遗留与下一步

1. ~~MiMo ask 复测待 key~~ **已完成（见下）**；
2. Flash 持久化（保存 Wi-Fi/LLM 配置）优先级因此进一步上升——每轮断电都要手工重配；
3. 关联状态判定改为真实 ESSID/BSSID 判断、`wapi show` 误报、DHCP 冷启动 5/5 等按原队列推进。

### MiMo ask 复测 + 冻结修复最终确认（2026-08-28，净版固件）

用户提供新 key 后在净版固件（BIN `d2212532...`）上完成：

- `set_llm api.xiaomimimo.com mimo-v2.5 <key>` → `API key saved`；
- `ask` #1（立即）：TLS 1.2（ECDHE-RSA-CHACHA20-POLY1305）330ms 握手，POST /v1/chat/completions → 200，模型真实回复自我介绍（含能力清单）；
- `ask` #2（静置 15s 后）：回复 "5"，正确；
- `ask` #3（静置 40s 后）：回复 "6"，正确；
- 加强复测循环：静置 10s / 30s / 20s 后各一次 ask，均正常回复（ok1/ok2/ok3）；
- 全部 6 次静置>10s 的 ask 均无冻结，控制台全程存活；此前"静置 ~10s 以上发 ask 必触发无回显死机"的现象未再复现。

**结论：冻结竞态按"异步诊断 printf 与控制台读路径相撞"根因修复成立**（驱动每包打印 + TCP-WRB 每事件打印 + mbedTLS debug 回调全部移除/关闭）。旧的 2026-08-27 晚固件（BIN a2c504f1...）由本净版固件取代。

小观察：静置后首次 ask 会先出现一条 `[HM-TLS-RX] want=5 ret=-1 errno=11`（EAGAIN，空闲 TLS 连接 SO_RCVTIMEO 先到期），随后代理自动重建连接完成请求，属预期行为，无功能影响。

## 2026-08-27 晚间续接（MiMo 真实问答验收通过，P0 完成）

在下午 HTTPS/TLS 打通的基础上，配置 MiMo API Key 并完成真实非流式 `ask` 验收。过程中又排掉三个 bug：

### 新发现并修复的 bug

1. **USB CDC 控制台 64 字节行缓冲截断**（esp32s3_usbserial.c `ESP32S3_USBCDC_BUFFERSIZE=64`）：`set_llm mimo <50字符Key>` 恰好 64 字节含换行，换行被丢弃；fgets 阻塞等换行，下一条命令被粘连进同一行并作为 set_llm 的参数吞掉（表现为 ask 无响应、回显粘连）。已把缓冲升到 256 字节。
2. **MiMo 预置模型名失效**：cmd_llm.c 预置 `mimo-v2-flash` 已下线（服务器返回 400 "Unsupported model"）。`/v1/models` 实际可用 `mimo-v2.5`/`mimo-v2.5-pro`；cmd_llm.c 预置已改为 `mimo-v2.5`。
3. **LLM 看门狗用墙钟测延迟**（agent_loop.c）：板子无 RTC，NuttX 从 epoch 0 启动，网络起来后 SNTP 对时使 gettimeofday 跳变，一次成功的 LLM 调用（err=0、回复 198 字节）被算成 27 亿毫秒"超时"，正确回复被丢弃。已将 agent_loop.c 全部 10 处 gettimeofday 延迟测量改为 CLOCK_MONOTONIC 包装 mono_gettime()。

### 验收结果（2026-08-27 晚）

完整链路：冷启动 -> WAPI 配网锁定强 AP -> DHCP -> ai_agent -> `set_llm api.xiaomimimo.com mimo-v2.5 <key>` -> `ask hello, please introduce yourself in one short sentence` -> TLS 1.2 握手（580ms，ECDHE-RSA-CHACHA20-POLY1305）-> HTTP POST /v1/chat/completions -> 200 -> 模型真实回复：

  [Agent]: 你好！👋 我是 AI Agent，运行在 Vela 嵌入式设备上的 AI 助手。
  （随后输出能力清单：搜索/提醒/笔记/飞书文档/健康数据/系统诊断）

当前烧录固件（含 DMA 池 + USBCDC 256 + 单调时钟修复）SHA-256：BIN a2c504f11dceddc31bb45756130352a6c88e36396378140e01b22b95e1665c55，ELF 58a9d0747f0bfe5230aea36e60cbba99f9c50e97763568f78306736697294f84。

### 已知遗留

- **偶发整机冻结**：set_llm 完成后静置 ~10 秒以上再发 ask 会触发无回显死机（静置 60 秒不发命令则不死；+3s 发 ask 正常）。疑似后台定时任务与控制台/输入路径的竞态，待查。
- 诊断开关仍开启：CONFIG_MBEDTLS_DEBUG、驱动 [HM-WLAN-TX] printf、agent_loop [HM-LLM] printf、[HM-TLS-DBG]。console 多线程 printf 有断行/交错（不影响功能）。
- 板上 `/data` 仍是 TMPFS，Key 和配置重启即失；当前验收流程需每次重新 set_llm。
- cmd_llm.c 的 mimo 预置模型名修复在源码中，尚未随新构建烧录（下次构建生效）。

### 下一步

1. 排查"静置后 ask 冻结"竞态（优先级高，影响稳定性）；
2. 关闭诊断开关做净版固件并复测 ask；
3. agent 内 DHCP 自动化、Flash 持久化（保存 Key/配置）、本地工具注册按原队列推进。

## 2026-08-27 下午续接（HTTPS/TLS P0 已打通）

本轮从「观测 ESP32-S3 Wi-Fi 驱动 TX 返回值」继续，已完成根因定位与修复，**HTTPS/TLS 验收通过**：

### 根因（已实证）

1. Wi-Fi TX 缓冲由 esp32s3_wifi_adapter.c 的 esp_malloc_internal() 分配，要求 internal（DMA 可用）DRAM；该函数先 kmm_malloc()，若指针落在 PSRAM（esp32s3_ptr_extram）就释放并返回 NULL。板上 kmm 用户堆横跨 internal DRAM + PSRAM（CONFIG_MM_REGIONS=2）。
2. mbedTLS ssl_setup() 分配后，internal DRAM 的空闲块被耗尽/碎片化，454 字节的 TX 缓冲请求持续拿到 PSRAM 指针（日志 220 次 size=454 ptr=0x3c11xxxx in PSRAM -> REJECT，地址恒定），esp_wifi_internal_tx() 返回 ESP_ERR_NO_MEM (0x0101)，wlan_transmit() 以 -12 静默重排队——报文从未上射频，ClientHello 无人 ACK。这解释了全部历史现象（TLS 上下文存活期发送失效、释放即恢复、字节重放成功等）。
3. 排查中发现 syslog(LOG_INFO) 输出进 CONFIG_SYSLOG_BUFFER（196 字节 RAM 环形缓冲）而非串口，驱动层日志必须用 printf。

### 修复

1. 16 KiB 静态 internal-DMA 回退池（hm_wlan_pool_*，esp32s3_wifi_adapter.c）：esp_malloc/zalloc/calloc/realloc_internal 在 kmm 只能给出 PSRAM 时改用该池；esp_free() 先判池成员。池在 .dram0.bss（保证 internal），块分配器带惰性初始化、首次 fit、空闲块合并（注意：初版漏了惰性初始化导致关中断死循环冻结整板，已修复并加防御检查）。
2. 曾尝试上游 CONFIG_XTENSA_IMEM_USE_SEPARATE_HEAP（96 KiB）：板上 internal DRAM 空闲不足 96 KiB，up_allocate_heap() 的 DEBUGASSERT 在控制台初始化前触发，NuttX 启动即挂死。已在 build.sh 显式禁用并注明。
3. TLS 配置修正（apps/crypto/mbedtls 的目标文件不随 .config 变更重编，曾多次“改配置未生效”，build.sh 现已强制清理重编）：
   - 输入缓冲 8192 / 输出缓冲 1024（百度证书链约 5 KiB、MiMo 约 3 KiB；输出足够请求报文）；
   - 删除 mbedtls_ssl_conf_max_frag_len：MFL 会让服务器把 Certificate 握手消息分片成多条 record，mbedTLS 3.4 不支持握手消息重组（-0x7080 "TLS handshake fragmentation not supported"）——这是 MiMo 握手失败的直接原因；
   - MiMo 服务器偏好 ECDHE-RSA-CHACHA20-POLY1305 + x25519，均在已启用能力内。
4. 诊断日志保留：驱动 TX 路径 [HM-WLAN-TX]（wlan_transmit/tx_done/ENOMEM/超时、esp_wifi_sta_send_data 失败码）、[HM-TLS-DBG]（mbedTLS debug 回调，CONFIG_MBEDTLS_DEBUG[C]）。注意 esp_*_internal 分配路径禁止 printf（会冻死板子），已全部移除。

### 验收结果（2026-08-27 下午）

- net_test www.baidu.com 443：TLS 1.2 (ECDHE-RSA-AES128-GCM-SHA256) 540 ms，HTTP 200；
- net_test api.xiaomimimo.com 443：TLS 1.2 (ECDHE-RSA-CHACHA20-POLY1305-SHA256) 540 ms，HTTP 404（API 根路径无 GET 资源，属预期）；
- 当前固件产物 SHA-256：BIN 17046abf90e4177ed36ce623420131cf436779ed76d4bddbb4d25c3d1ab34502，ELF a928d894a00474eae4cbea6864558ef2c264db5ba7768bb531cc6c10cd3b2d30。
- 已知小坑：诊断脚本用 RTS 脉冲复位后 USB CDC 控制台可能不恢复，需用 esptool --after hard-reset chip_id 做干净硬复位后再跑无复位版诊断。

### 下一步

1. 配置 MiMo API Key 后做一次真实非流式 ask 问答验收（P0 最后一步）；
2. 关闭临时诊断开关（CONFIG_MBEDTLS_DEBUG、驱动 printf 日志）做净版固件；
3. 按原队列推进：agent 内 DHCP 自动化、关联状态判定、Flash 持久化、本地工具注册。

## 已完成或已有证据

- ESP32-S3-EYE 可启动 OpenVela/NuttX，历史记录显示可进入 `nsh>` 和 `vela>`；
- 固件已构建并保留：
  - 服务器项目 `artifacts/nuttx.bin`：SHA-256 `79d3c478f46e5922b65c8b803cdccfab8145f87a0084820aaad1646f90f068e4`；
  - 服务器项目 `artifacts/nuttx.elf`：SHA-256 `e55b440a887dbbd58d9858cc8a3039a1bbf4d65064bec430ecf96a605f5f9906`；
- ELF 中可确认包含 Wi-Fi 状态机以及 `[HM-WIFI]`、`[HM-NET]`、`[HM-TLS]`、`[HM-LLM]` 日志；
- Ubuntu 构建、部署、校验、烧录脚本已整理，Shell 静态语法通过；
- 已生成 Ubuntu/Win11 双机分工和迁移流程；
- `psk` 执行日志已改为 `<redacted>`，并已进入当前烧录固件；
- Ubuntu 22.04 已安装主要依赖，`/home/hfy/work/openvela` 工作区和比赛仓已恢复；
- 已记录 NuttX `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`、AI Agent `31faed70f683a6f5e690437c5507891360f0814a` 和 ESP HAL 等固定版本；
- `tools/verify-ubuntu.sh` 已通过，项目 `.venv`、esptool 5.3.1、pyserial 3.5.0 已就绪；
- Ubuntu 已识别开发板为 `/dev/ttyACM0`，`hfy` 已加入 `dialout`；截至 2026-08-12 尚未烧录或擦除。

## 当前主问题：DHCP 已打通，HTTPS/TLS 尚未完成

2026-08-19 至 2026-08-20 已完成：

1. 官方 `esp32s3-eye:ai_agent` 基线构建成功，`build_rc=0`、`fix_rc=0`；
2. 修复构建脚本误将 OpenVela 的 4 MB/DIO 镜像重新封装为 1 MB/QIO 镜像导致的看门狗复位循环；
3. 修复 `g_wifi_ctx.lock` 未初始化导致 `set_wifi` 永久阻塞；
4. 将关联等待调为 20 秒、DHCP 等待调为 30 秒，并加入同轮 DHCP 重试；
5. 扫描确认环境中存在两个同名 `MiWiFi` BSSID。自动流程停在弱 AP；手工锁定强 AP `50:88:11:7a:02:69` 后，`renew wlan0` 成功获得 `192.168.31.248`；
6. 已用 WAPI 原生 `wapi_scan_init/stat/coll` API 实现最强 BSSID 选择，实机连续选择强 AP `50:88:11:7a:02:69`（约 -53 至 -55 dBm）；
7. 已确认 ESP32-S3 WEXT 在同一个控制 socket 内执行 `ESSID -> AP -> ESSID` 会留下 `WAPI_ESSID_OFF`；对照官方 `wapi reconnect`，关闭旧 socket 后用新 socket 重放 WPA2 配置，实机达到 `WAPI_ESSID_ON`、72 Mbps、信号约 -55 dBm；
8. 2026-08-21 已恢复服务器 `192.168.31.251`，完成三轮新增构建、烧录和脱敏串口测试；每次 esptool 写入校验均通过；
9. 已确认当前构建未启用 `CONFIG_AI_AGENT_BLE_GATT`，固件 ELF 也不含 BLE GATT 启动字符串，因此 BLE/Wi-Fi 共存不是本轮根因；
10. 已把 WAPI 恢复为每个操作独立控制 socket，并补回 `mode -> PSK -> ESSID -> AP -> ESSID` 间的 3/3/8/3 秒等待；实机连续达到强 AP `50:88:11:7a:02:69`、`ESSID_ON`、72 Mbps、约 -55 dBm；
11. DHCP 已改为独立运行系统内建 `renew` 并返回真实子任务退出码；无论在关联后等待 30 秒还是 2 秒，均输出 `netlib_obtain_ipv4addr() failed`、`renew ret=1`，IP 保持 `0.0.0.0`；
12. 退出 `ai_agent` 的对照测试发生在状态机已开始下一轮重置之后，当时接口已回到 `ESSID_OFF`，所以该结果不能证明 DHCP 是进程组隔离问题；
13. 静态地址对照测试的 `ifconfig` 命令被串口输入截断（`gateway 192...` 处），因此参数无效，不能据此判断静态 IP 兜底是否可行；
14. 截至 2026-08-21，板上固件仍是关联成功但 DHCP 失败的版本；该历史结论已被下述 2026-08-23 实测更新。

2026-08-23 新增结论：

15. 静态地址对照已按短命令分步完成：板端到服务器 3/4 ping 成功，服务器到板端 4/4 成功，排除射频、关联和局域网二层故障；
16. 同一固件在 NSH 冷启动流程下运行 `renew wlan0` 可成功；抓包确认成功路径发出 DHCP Discover/Request，而 ai_agent 失败路径没有发出 Discover；
17. ai_agent 内直接调用 `netlib_obtain_ipv4addr()` 返回 `errno=11`（`EAGAIN`），普通堆仍有约 8.37 MB 空闲；
18. 早先把两轮成功归因于 `CONFIG_NET_UDP_NWRBCHAINS=32` 过于乐观；后续冷启动仍会失败。实测失败前 `iob_navail(false)` 为 64/64 或 128/128，排除 IOB 数据缓冲耗尽；
19. 同网段抓包确认 agent 运行时四轮 DHCP 都没有发出 Discover；退出 agent 后运行 NSH `renew wlan0` 立即发出 DHCP 报文并获得 `192.168.31.249`，路由器、DHCP 和 DNS 服务正常；
20. `CONFIG_NET_UDP_PREALLOC_CONNS` 已由 8 提到 16；UDP/TCP write-buffer chain 当前取 64/32。UDP 超过 64（96、128 均试过）会使 WAPI `essid_pre_ap` 超时，因此不能继续靠扩大静态池解决；
21. HTTPS 分层日志已完成：DNS 服务器为 `192.168.31.1`，`www.baidu.com` 可解析，TCP connect 成功（fd=4），但 TLS 1.2 握手约 90 秒后以 `MBEDTLS_ERR_NET_RECV_FAILED (-0x004c)`、`errno=11` 失败；TCP chain=32 未解决；
22. 当前可重复的安全恢复流程是：NSH 配置 Wi-Fi并执行 `renew wlan0`，获得地址后再启动 `ai_agent`。板子目前已按此流程在线；
23. 当前烧录产物 SHA-256：BIN `edb032d0f21ced7472a21515f919366e64649b03d1272fff3947d10c89bb191b`，ELF `f8c402d331f79ae39dceec03c83abb11222edb407e97d9bb377798eab5132efc`。

2026-08-24 新增结论：

24. **熵源修复确认生效**：上版固件（含 `CONFIG_MBEDTLS_ENTROPY_C` + `CONFIG_DEV_URANDOM` + `CONFIG_DEV_URANDOM_ARCH`）实测 `psa_crypto_init()` 不再报 `failed`，TLS 握手推进到 DNS/TCP 阶段；原 TLS 1.3 PSA 初始化失败根因已解决；
25. **找到 agent 内 DHCP 不通的真正根因**：`network_manager.c` 的 `wifi_run_dhcp_task()` 中 `char* argv[] = { (char*)dev, NULL };` 错误地把接口名放在 `argv[0]`，导致 `renew` 内建命令收不到接口参数、不发 DHCP Discover；正确写法应为 `{ (char*)"renew", (char*)dev, NULL }`（与 NSH 手敲 `renew wlan0` 等价，后者一直可用）；
26. **关键构建陷阱**：`scripts/build.sh` 的 deploy 步骤每次构建都会把 `tools/staging-network_manager.c` 覆盖回 `packages/ai_agent/src/infra/network_manager.c`，因此该文件的唯一真相源是 **staging 副本**，改 live 文件会被冲掉；本次修复已落在 staging 文件第 933 行；
27. 已重新 `build.sh all` 构建并烧录（esptool 校验通过，BIN 1034480 字节，`Hard resetting via RTS pin`）；新固件同时含熵源修复 + DHCP argv 修复，但**尚未做端到端验证**；
28. 验证入口明确：`nsh>` 下执行 `ai_agent` 进入 `vela>` 控制台；`set_wifi <ssid> <pw>` 关联、`net_test [host] [port]` 触发 TLS、`ask <text>` 调 MiMo；Wi-Fi 凭据在 TMPFS，每次重启/烧录后需重新 `set_wifi`；
29. Windows 侧自动化文件集中在本地私有工作目录：SSH 凭据必须通过环境变量或密钥提供，不得写入参赛仓；串口会话脚本通过 RTS 硬复位后执行交互验收。

## 下一主问题：修复版长稳补足与异常路径回归

当前剩余风险：

1. 修复版已完成干净构建、烧录、冷启动传输链 5/5、连续自然问答 7/7 + 4/4、受控重连 3/3；完整长稳门禁仍待补足；
2. 设备 Flash 中已保存用户提供的 MiMo key，提交物和日志不含完整 key；后续应按需要轮换设备侧凭据；
3. 当前本地官方仓已提交 TLS 修复为 `c79a240`；本轮新增回归记录待再提交，远程仍未推送。

## 尚未完成

| 阶段 | 状态 | 验收标准 |
| --- | --- | --- |
| 官方 ai_agent 基线构建 | 已通过 | 固定版本在恢复后的 Ubuntu 工作区成功构建并保存哈希 |
| HomeMind 构建/烧录 | 已通过基础门禁 | DIO 产物哈希通过，新固件稳定启动且 PSK 日志脱敏 |
| Wi-Fi 关联与 DHCP | 单轮、重启后凭据、冷启动 5/5 与受控重连 3/3 通过；外部 `ifdown` 不适用于当前 `vela>` 入口 | 更稳健串口采集器与外部断网测试入口 |
| HTTPS/TLS | 已通过：`www.baidu.com:443` 两次完整 TLS 1.2 + HTTP 200 | `net_test` 出现 `[HM-TLS] Handshake OK` + `HTTP Status` |
| MiMo 文本问答 | 已通过：首次配置和重启后各一次真实非流式 `ask`，均 HTTP 200 | 一次真实非流式 `ask` 成功 |
| Flash 持久化 | Wi-Fi 与 LLM 配置在硬复位后均恢复，重启后 `ask` 通过 | 断电/重启后 Wi-Fi/LLM 配置保留 |
| HomeMind 本地工具 | 新构建已通过 LED 开/关快速路径 | 工具进入 ELF、注册并控制确认过的 LED |
| 自动测试和真实日志 | 修复前基线 6/10；修复版自然问答已有 7/7 + 4/4 PASS，受控重连 3/3 PASS，冷启动传输链 5/5 无超时；另有直接续跑 3/3 未捕获 HTTP 状态行 | 完成最终长稳门禁，复核异常采集并提交 |

## 开发环境

- 固件主机：Ubuntu 22.04，i3-8100T，8 GB RAM，256 GB 存储；初始 `JOBS=2`；
- Win11：仅继续开发 UG 外壳和 `miniprogram/`；
- Windows 旧 `.venv-tools` 已失效并移入工作区 `_archive/local_envs/`；
- 本地旧迁移包、临时查询脚本和旧任务记录均已归档，没有删除；
- 当前 Win11 根工作区的 `.git` 为空，不是有效仓库；Ubuntu 的正式比赛仓 Git 元数据已经恢复，固件提交以 Ubuntu 为准。

## 下一执行队列

1. 复核或重跑最终标准自然语言长稳 10 轮，明确处理直接续跑的 HTTP 状态采集缺失；
2. 复核 TLS body framing、异常响应和重复请求路径的串口日志；
3. 提交本轮回归记录并保留远程 push；
4. 将固定关联等待替换为真实关联状态判断，并处理 `wapi show` 成功打印后仍返回 -1 的非关键查询错误；
5. 将当前 TMPFS 配置存储替换为真实掉电持久化；
6. C0 冻结后按工作区总体计划推进腾讯云/小程序 C1。

详细迁移步骤见 [MIGRATION_UBUNTU.md](MIGRATION_UBUNTU.md)。
