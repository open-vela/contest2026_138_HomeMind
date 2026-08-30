# HomeMind 作品提交合规清单

> 核对日期：2026-08-30  
> 提交截止：2026-09-20  
> 作品仓库：`contest2026_138_HomeMind`  
> 本清单只记录已核实事实；未取得源码、日志或真机证据的功能不得标记为完成。

## 1. 官方依据

- [大赛总览与作品提交要求](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [AI 硬件产品创新赛道详细指引](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_track_guide.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI Coding 日志归集与提交手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)
- [大赛官网](https://openvela.com/#/contest)

## 2. 当前状态摘要

| 项目 | 当前状态 | 结论 |
| --- | --- | --- |
| 官方作品模板 | 已下载官方原版 DOCX，并验证为有效 Office Open XML 文件 | 已完成准备，不代表技术报告已填写 |
| Windows 当前目录 Git 状态 | `git -C contest2026_138_HomeMind status` 返回“not a git repository” | 未完成，不能以此目录证明官方仓已提交 |
| 官方仓远端 | 公共 API 已确认仓库存在；默认分支为 `dev-ai-contest-2026`，截至核对时仅有初始模板，最后 push 为 2026-07-17，PR 数为 0 | **红色风险**：当前 HomeMind 实际代码尚未进入评审读取的官方仓 |
| AI Coding 日志 | 仅发现 `logs/your-github-login/.../claude-code__example.jsonl` 示例会话 | 未完成；示例日志不能作为 HomeMind 开发证据 |
| 演示视频 | 当前仓内未发现 `.mp4` 或 `.mov` | 未完成 |
| 硬件实物照片 | 当前仓内只有小程序图标等资源，未发现可确认的实物多角度照片 | 未完成；HomeMind 涉及硬件实物，官方模板要求提交 |
| 技术报告 | 尚未按官方模板填写 | 未完成 |
| 提交压缩包 | 尚未生成 | 未完成 |

## 3. 官方仓与源码

- [ ] 使用报名时填写的 GitHub 账号接受专属仓协作邀请，并确认具备 push 权限。
- [ ] 确认专属仓 URL 为 `https://github.com/open-vela/contest2026_138_HomeMind`。
- [ ] 在由 `repo init` / `repo sync` 建立、且包含 `.repo/` 的正式 openvela 工作区操作。
- [ ] 确认工程基线来自 `dev-ai-contest-2026`；如果修改 `nuttx`、`vendor_*` 等公共仓，按官方指南向相应公共仓的 `dev-ai-contest-2026` 分支提交 PR。
- [ ] 将 HomeMind 的全部参赛源码、配置、构建脚本、运行说明和必要资源归集到专属仓；不得依赖评委无法访问的本机散落目录。
- [ ] 检查 manifest 的 `<linkfile>` 映射，确保 `repo sync` 后源码进入正确的 openvela 构建位置。
- [ ] 从自己的 fork 发起 PR 到专属仓，完成 review 并 **merge**；不能停留在 open 状态。
- [ ] 首次 PR 已签署 CLA，`cla/signature` 检查通过；提交使用 `git commit -s`。
- [ ] 专属仓默认分支包含最终 commit，远端不存在只在本地的未提交/未推送改动。
- [ ] 在干净工作区按 README 从零复现：同步、配置、编译、烧录、启动和核心功能演示。
- [ ] 截止前从另一个空目录 clone 专属仓，完成最终只读复核。

最终记录：

- 默认分支：`dev-ai-contest-2026`（2026-08-30 已由 GitHub 公共 API 核实）
- 当前远端 commit SHA：`961cf680946773cd4c1f41b29c425de892bd9a69`（仅初始模板；不得作为最终提交）
- 最终 commit SHA：`________________________________________`
- 已合并 PR：`__________`
- CLA/CI 状态：`__________`
- 干净复现日期与结果：`__________`

> 注意：`dev-ai-contest-2026` 是大赛工程基线，以及获奖后向 openvela 上游提交的目标分支；初赛评审首先读取团队专属仓已合入的最终代码，不要误把“本地分支存在”当成“作品已提交”。

## 4. AI Coding 日志

- [ ] 在正式 openvela 工作区、专属仓目录中安装官方日志采集器：

  ```bash
  bash ../.claude/skills/contest-log-collector/onboarding/install.sh \
    --team-id contest2026_138_HomeMind \
    --github-login <本人真实 GitHub 用户名>
  ```

- [ ] 运行 `verify-setup.sh`，所有检查项通过。
- [ ] 确认 `~/.claude/contest-collector.env` 中的 `TEAM_ID` 和 `GITHUB_LOGIN` 正确。
- [ ] 在带 `.repo/` 标识的 openvela 工作区内使用官方支持的 AI 工具开发并正常结束会话。
- [ ] 检查真实日志已进入 `logs/<github-login>/<date>/*.jsonl`。
- [ ] 使用 `contest-snapshot --list` 检查遗漏；如需补录已有 Claude Code 历史会话，使用官方 `contest-snapshot --backfill`。
- [ ] 运行官方校验：

  ```bash
  python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
  ```

- [ ] 删除整个示例会话文件，而不是修改其内容；不要编辑任何真实 `.jsonl` 日志。
- [ ] 提交前预览日志，若某个会话包含秘密或私人信息，只能整份删除该会话，禁止手工删改字段或伪造 Token。
- [ ] `git add logs/`、签名提交并 push，确认真实日志已出现在专属仓远端。
- [ ] 技术报告中的 AI 工具、Skills、Token 和 AI Coding 占比，与实际日志及统计口径一致。

最终记录：

- GitHub 用户名：`__________`
- 有效会话数量：`__________`
- 日志日期范围：`__________`
- `validate-log.py` 结果：`__________`
- 日志最终 commit：`________________________________________`

## 5. 技术报告（官方模板）

- [x] 官方原版模板已下载到本目录。
- [x] 文件验证：ZIP/OOXML 头为 `PK`，包含 `[Content_Types].xml` 和 `word/document.xml`。
- [x] 官方原版 SHA-256：`94247BFE0ED960629B138108316DCCDD2827761BBE9CC8D5FE48E1C78AD396A8`。
- [ ] 从官方原版复制出 HomeMind 填写版；保留原版不覆盖。
- [ ] 信息表填写作品名称、队伍名称、真实成员与分工，方向选择“AI 硬件产品创新”。
- [ ] 摘要不超过 300 字，量化成果必须来自测试记录。
- [ ] 3.1 明确家庭场景、痛点、技术难点和创新点。
- [ ] 3.2 附系统架构图，准确划分 ESP32-S3-EYE、家庭服务、小程序和外部服务职责，并说明断网/弱网降级。
- [ ] 3.3 明确端侧与云端 AI 的真实实现；说明实际使用的 openvela 图形、AI、多媒体能力，不把计划写成已完成。
- [ ] 3.4 说明固件、数据流、硬件接口、应用端，以及至少 1 个真正部署在 `/data/agent/skills/` 且有演示证据的运行时 Skill。
- [ ] 3.5 提供功能、性能、可靠性实测数据；准确率、误报率、漏报率、时延、内存等若未测，不得填写估算值冒充实测。
- [ ] 3.6 AI-Native 数据与 `logs/`、MiMo 控制台等证据一致，并说明统计口径。
- [ ] 3.7 如实写成果、不足与未来工作；所有未完成能力放入“已知限制/未来工作”。
- [ ] 逐项核对技术报告中的声明都能定位到：官方仓源码、真机日志、测试记录或视频时间点。
- [ ] 导出最终 PDF，检查中文字体、图片、页码、表格和链接无错位。

拟交文件名：

- DOCX：`HomeMind_技术报告_最终版.docx`
- PDF：`HomeMind_技术报告_最终版.pdf`

## 6. AI 硬件赛道硬性能力证据

- [ ] 真机运行 openvela + ai_agent，基础对话正常。
- [ ] 至少一个交互渠道真实可用（CLI / 语音 / WebSocket）。
- [ ] 至少 1 个自定义 Skill 真实存在于 `/data/agent/skills/`，并在视频中演示定义、触发与结果。
- [ ] 至少 1 个“主动 + 执行”场景：定时、阈值、事件或上下文触发；不能只做被动问答。
- [ ] 有工具调用/执行结果，不是纯聊天机器人。
- [ ] 若实现语音唤醒，唤醒词统一为“你好，openvela”或“Hello，openvela”。
- [ ] 至少实际落地图形、AI、多媒体三项 openvela 核心能力之一，并在报告中点名组件与证据。

## 7. 演示视频与实物照片

- [ ] 视频使用 `.mp4` 或 `.mov` 等常见格式，总时长不超过 5 分钟。
- [ ] 视频清楚展示功能、交互操作和 AI 能力。
- [ ] 视频只展示真实输入、真实设备和真实结果；不以固定返回值、测试图案、随机数据或音量阈值冒充功能。
- [ ] 视频中同时出现 ESP32-S3-EYE 实物和关键结果，避免只录屏不见硬件。
- [ ] 关键环节保留串口/LCD/小程序/服务端状态之一作为可核验证据。
- [ ] 视频中的全部口播与字幕和最终 README、报告“已实现功能/已知限制”一致。
- [ ] 准备硬件实物前视图、后视图、侧视图和俯视图；照片清晰、背景整洁、无账号、Token、Wi-Fi 密码、服务器密码等秘密。
- [ ] 播放最终视频检查声音、画面、字幕、方向和完整性。

建议成片结构（不得先写死尚未实现的功能）：

1. 20–30 秒：问题、定位与架构。
2. 30–45 秒：OpenVela / ai_agent 真机启动与交互入口。
3. 150–180 秒：只演示已通过验收的核心闭环，包括 Skill、主动触发、工具执行和端云协同。
4. 30–45 秒：断网/恢复或稳定性实测。
5. 20–30 秒：官方仓、AI 日志、技术亮点与已知限制。

## 8. 提交压缩包

官方模板明确：项目源码与 AI Coding 日志保留在专属仓，**无需放入提交压缩包**，评审会直接 clone 专属仓验证。

- [ ] 压缩包中包含按官方模板完成的技术报告 `.docx` 和/或 `.pdf`。
- [ ] 压缩包中包含不超过 5 分钟的演示视频。
- [ ] HomeMind 是硬件实物作品，压缩包中包含实物多角度照片。
- [ ] 海报仅在线下展示/入围决赛时准备；答辩 PPT 仅入围决赛后准备，除非组委会另行通知。
- [ ] 在大赛官网/飞书提交表中填写专属仓地址。
- [ ] 压缩包命名严格使用：`<队伍名称>-<作品名称>-<仓库名称>.zip`。
- [ ] 确认最终压缩包可以正常解压，所有文档、视频、照片可打开。
- [ ] 提交前对压缩包和每个关键文件计算 SHA-256，保存校验记录。
- [ ] 最迟 2026-09-20 中午完成上传，保存成功页面、提交时间、文件名和回执截图。
- [ ] 上传后重新下载或预览，确认线上版本与本地最终版本一致。

待填写：

- 队伍名称：`__________`
- 作品名称：`HomeMind：端云协同的家庭感知中枢`
- 仓库名称：`contest2026_138_HomeMind`
- 最终压缩包：`__________-HomeMind端云协同的家庭感知中枢-contest2026_138_HomeMind.zip`
- 专属仓 URL：`https://github.com/open-vela/contest2026_138_HomeMind`
- 提交时间：`__________`
- 提交回执：`__________`

## 9. 最终红线检查

- [ ] 没有把未完成、未测试或仅规划的功能写成已实现。
- [ ] 没有固定推理返回、假传感器数据、测试图片、随机数据或伪唤醒。
- [ ] 没有手工修改或伪造 AI Coding 日志。
- [ ] 仓库、日志、技术报告、视频四者的功能声明完全一致。
- [ ] 仓库历史、文档、日志、视频和压缩包均不包含 API Key、Token、密码、私钥、Wi-Fi 凭据或个人隐私。
- [ ] 所有第三方依赖、模型、素材和协议均已列明来源与许可证。
- [ ] 参赛作品为原创并遵循 Apache 2.0；特殊许可证或闭源服务已单独说明。
- [ ] 截止前所有 PR 已合入、全部最终文件已上传，并保存可验证回执。

## 10. 官方模板文件记录

- 文件：`2026首届openvela_AI硬件开发者大赛_作品提交模板_官方原版.docx`
- 官方下载地址：`https://openvela.com/contest/2026%20%E9%A6%96%E5%B1%8A%20openvela%20AI%20%E7%A1%AC%E4%BB%B6%E5%BC%80%E5%8F%91%E8%80%85%E5%A4%A7%E8%B5%9B%20-%20%E4%BD%9C%E5%93%81%E6%8F%90%E4%BA%A4%E6%A8%A1%E6%9D%BF.docx`
- 下载日期：2026-08-30
- 文件大小：26,526 字节
- SHA-256：`94247BFE0ED960629B138108316DCCDD2827761BBE9CC8D5FE48E1C78AD396A8`
