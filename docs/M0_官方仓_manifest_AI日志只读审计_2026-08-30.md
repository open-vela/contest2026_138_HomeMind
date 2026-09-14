# HomeMind M0：官方仓、manifest 与 AI 日志只读审计

> 审计时间：2026-08-30  
> 范围：Windows 工作区、Ubuntu OpenVela 工作区、团队官方仓、manifest、AI Coding 日志  
> 本次操作：只读检查；未执行提交、推送、reset、clean、repo sync 或业务代码修改。

## 1. 结论

当前固件能够构建和运行的工作区，不等于评委最终能够看到和复现的官方仓状态。M0 目前是红色阻塞项：

1. Windows 的 `C:\Users\a2760\Desktop\HomeMind` 只有一个空的 `.git` 目录，不是 Git 工作树；其下 `contest2026_138_HomeMind` 也没有 `.git`。
2. Ubuntu 团队仓位于 `/home/hfy/work/openvela/contest2026_138_HomeMind`，有有效 Git 元数据，但处于 detached HEAD。
3. Ubuntu 团队仓只有 27 个受跟踪文件；当前工作区另有 90 个未跟踪文件和 2 个已修改文件。绝大多数 HomeMind 成果未进入提交历史。
4. 实际固件源码主要位于 `/home/hfy/work/openvela/packages/ai_agent`，但该目录的 `.git` 是断开的符号链接，整个目录当前不受 Git 正常管理。
5. NuttX 工作树的 Git 索引异常，表现为 23,986 个暂存删除和约 23,989 个未跟踪文件；三处近期 ESP32-S3 源文件的工作树内容均与 HEAD 对象不同。
6. 官方仓 `logs/` 只有模板示例，没有一条有效的 HomeMind AI Coding 会话。

因此，禁止在现有 Ubuntu 工作区内直接执行强制同步、硬重置或清理。最安全的方案是保留该工作区作为“生产现场”，另建一个干净的官方工作区，从两侧逐项迁移经过确认的成果。

## 2. Git 与工作区准确状态

### 2.1 Windows

| 路径 | 状态 |
|---|---|
| `C:\Users\a2760\Desktop\HomeMind` | `.git` 是空目录；`git rev-parse` 判定不是仓库 |
| `C:\Users\a2760\Desktop\HomeMind\contest2026_138_HomeMind` | 没有 `.git`；不是仓库 |
| `C:\Users\a2760\Desktop\HomeMind\hm_ui` | 位于比赛目录之外，共有 LCD 源码、字形、预览和部署脚本 |

Windows 比赛目录包含 `backend/`、`miniprogram/`、`server/` 等成果，但这些目录在 Ubuntu 团队仓工作树中不存在。

### 2.2 Ubuntu 团队仓

| 项目 | 值 |
|---|---|
| OpenVela 根目录 | `/home/hfy/work/openvela` |
| 团队仓 | `/home/hfy/work/openvela/contest2026_138_HomeMind` |
| 远端 | `https://github.com/open-vela/contest2026_138_HomeMind` |
| HEAD | `961cf680946773cd4c1f41b29c425de892bd9a69` |
| 分支 | detached HEAD |
| 远端基线 | `openvela/dev-ai-contest-2026`，与当前 HEAD ahead/behind 为 `0/0` |
| 受跟踪文件 | 27 |
| 已修改文件 | 2：`README.md`、`logs/README.md` |
| 未跟踪文件 | 90 |

27 个受跟踪文件仍基本是官方模板：hello_app、contest_board、hello_quickapp、示例 AI 日志与 manifest。HomeMind 的主要实现尚未成为任何提交。

### 2.3 Ubuntu OpenVela 元数据异常

- `repo list` 当前只列出 20 个粗粒度项目，完整同步没有成立。
- `/home/hfy/work/openvela/packages/.git` 不存在。
- `/home/hfy/work/openvela/packages/ai_agent/.git` 指向不存在的 `.repo/projects/packages_ai_agent.git`。
- `/home/hfy/work/openvela/nuttx/.git` 指向存在的 Git 目录，但索引呈“几乎全部文件已暂存删除、磁盘文件又全部未跟踪”的异常状态。
- `repo status packages` 会因 Git 元数据异常直接报错。

这不是普通 dirty 工作树，不能用常规 `git reset --hard` 或 `repo sync --force-sync` 处理。

## 3. manifest 审计

团队 manifest `contest2026_138_HomeMind.xml` 目前只建立三条映射：

- `app/hello_app` → `packages/demos/contest2026_138_hello_app`
- `quickapp/hello_quickapp` → `packages/apps/contest2026_138_hello_quickapp`
- `board/contest_board` → `vendor/openvela/boards/contest2026_138_board`

它没有把 `app/homemind` 或 LCD、工具、网络、TLS 源码映射到实际构建树。仓库 README 也明确说明 `app/homemind` 只是配置与 Skill 草案，尚未正式构建接入。

当前构建依赖 `tools/staging-*.c` 复制覆盖 `/home/hfy/work/openvela/packages/ai_agent` 中的文件。这种机制能产出固件，但评委只检出官方仓时无法获得完整的当前源码，也不能证明产物对应哪一版源文件。

## 4. 尚未进入官方仓历史的源码

### 4.1 Windows 独有目录

Ubuntu 团队仓当前不存在以下目录：

- `backend/`：Windows 侧约 52 个文件；
- `miniprogram/`：Windows 侧约 39 个文件；
- `server/`：Windows 侧约 8 个文件；
- `hm_ui/`：位于比赛目录外，含 `hm_lcd_display.c`、`hm_glyphs.h`、预览图与部署脚本。

### 4.2 Ubuntu `packages/ai_agent` 生产树

以下近期关键源码位于生产树，而不是官方团队仓中可构建的 HomeMind 应用目录：

- `src/channels/cmd_llm.c`、`src/channels/nsh_commands.c`
- `src/core/agent_loop.c`
- `src/infra/config_store.c`、`http_proxy.c`、`network_manager.c`、`vela_tls.c`
- `src/tools/tool_led.c`、`tool_device.c`、`tool_registry.c`
- `src/ui/hm_lcd_display.c`
- `include/tools/tool_led.h`、`tool_device.h`
- `include/ui/hm_lcd_display.h`
- `Makefile`、`src/agent_main.c`

生产树还包含大量 `.bak.*` 和编译中间文件，不能整目录直接提交。必须与干净基线比较，只提取有意修改。

### 4.3 Ubuntu NuttX 树

近期修改涉及：

- `nuttx/arch/xtensa/src/esp32s3/esp32s3_wifi_adapter.c`
- `nuttx/arch/xtensa/src/esp32s3/esp32s3_wlan.c`
- `nuttx/arch/xtensa/src/esp32s3/esp32s3_usbserial.c`

三者当前工作树对象哈希都与 HEAD 版本不同。团队仓中有部分 Wi-Fi patch 和修复脚本，但不能据此假设它们完整等价于生产树现状；需要在干净基线上重新生成并审查最小补丁。

## 5. AI Coding 日志缺口

Ubuntu 与 Windows 比赛目录中的有效提交日志数量均为 0。唯一 JSONL 是：

- 用户目录：`your-github-login`
- 团队 ID：`contest2026_000_openvela`
- 日期：`2026-01-01`
- generator/session：示例值

该文件只能作为目录模板，不能计入 HomeMind 开发日志。

可恢复线索：Windows 用户目录中发现 78 个 Codex rollout JSONL，其中 28 个 JSONL 能匹配到 HomeMind 相关文本；但它们尚未经过大赛采集器导出和校验。另有 WorkBuddy 日志，但当前仓内说明只明确支持 Claude Code/AIoT-IDE、OpenCode 和 Codex，不能自行宣称 WorkBuddy 日志有效。

Ubuntu 用户目录没有发现可直接归集的 AI JSONL 会话。

## 6. 最安全的收口步骤

### A. 先保全，不修复原现场

1. 暂停继续向 `packages/ai_agent` 和 NuttX 生产目录直接覆盖源码。
2. 对 Ubuntu 团队仓、`packages/ai_agent`、NuttX 三处制作只读时间戳快照，并保存文件清单与 SHA-256。
3. 同时保全 Windows 的比赛目录和 `hm_ui/`。不移动、不覆盖现有文件。
4. 明确禁止在旧工作区执行：`git reset --hard`、`git clean`、`repo sync --force-sync`、批量 checkout/reset。

### B. 建立新的干净官方工作区

1. 在 Ubuntu 新建并列目录，例如 `/home/hfy/work/openvela-clean-20260830`，按官方仓、`dev-ai-contest-2026` 和团队 manifest 重新 `repo init`/`repo sync`。
2. 验证团队仓处于实际本地分支，不是 detached HEAD；验证 `repo status`、`packages/ai_agent`、NuttX Git 元数据正常。
3. 从 `openvela/dev-ai-contest-2026` 新建恢复分支，如 `homemind/m0-recovery`；绝不向基线分支强推。
4. 先迁移 Windows 的 backend/miniprogram/server 和真正需要的文档，再迁移固件。
5. 固件迁移必须以“干净基线 vs 生产树”逐文件比较。只带入经过确认的功能修改；排除 `.o`、`.built`、`Make.dep`、`.bak.*`、临时诊断代码、虚拟环境和本机部署脚本。
6. LCD 源码应进入正式受跟踪源码路径；不得继续只存在于 Windows 根目录 `hm_ui/` 或远端生产树。
7. 对 ai_agent/NuttX 公共仓修改，生成最小可审查补丁并按组委会要求走对应公共仓 PR；团队 manifest/README 必须引用可访问、可复现的版本。若官方技术组给出不同提交方式，以书面回复为准。
8. 在全新工作区完成一次从空构建到烧录的复现，保存 commit SHA、构建命令、固件 SHA-256 和脱敏串口日志。

### C. 恢复 AI 日志合规链

1. 只在新的完整 `.repo` 工作区按 `logs/README.md` 安装官方采集器，使用真实 GitHub 登录名和正确团队 ID。
2. 运行官方 `verify-setup.sh`，然后用 `contest-snapshot --list`/预览模式确认会话来源。
3. 对 Windows 已有 Codex 会话，只使用官方采集器或官方手册认可的导入方式；不要直接复制、手改或拼接 JSONL。
4. WorkBuddy 会话是否有效应向组委会确认；未确认前不计入声明。
5. 用官方 `validate-log.py logs/` 校验。发现含凭据的会话时，按手册撤回整个会话，不编辑日志正文。
6. 真实日志校验通过后，再删除模板示例并提交 `logs/`。

### D. 提交与 PR

1. 按“仓库恢复 → 后端/小程序 → 固件最小补丁 → 日志”拆分可审查提交。
2. 每个提交保留 Signed-off-by，推送恢复分支并创建 PR。
3. PR 合入后，在 GitHub 网页核对目标分支确实含源码、日志、manifest、README 和复现说明。
4. 只有干净克隆能构建、真机可复现且 PR 已 merge，M0 才能标记完成。

## 7. M0 完成判据

- [ ] 新的完整 OpenVela 工作区 `repo status` 正常；
- [ ] 团队仓在本地分支，非 detached HEAD；
- [ ] Windows、Ubuntu 生产树的有意成果都已逐项收口；
- [ ] backend、miniprogram、固件、LCD、patch/PR 均在官方仓或可访问公共 PR 中；
- [ ] manifest 能从干净克隆重建作品；
- [ ] 至少一批真实 Codex 会话通过官方日志校验；
- [ ] 仓中无示例日志冒充、无编译中间文件、无备份文件、无本机部署凭据；
- [ ] PR 已合并到组委会评审分支。
