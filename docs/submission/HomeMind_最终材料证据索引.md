# HomeMind 最终材料证据索引

> 核对日期：2026-09-02。此索引只把仓库中已经存在的事实作为“可引用证据”；外部真机、视频和提交回执仍需补入。

| 申报内容 | 当前事实 | 可引用位置 | 状态 |
| --- | --- | --- | --- |
| OpenVela/ai_agent 固件 | 已完成干净构建、烧录和 SHA-256 记录 | `STATUS.md`、`artifacts/SHA256SUMS`、`firmware/README.md` | 已有证据 |
| 自动联网 / MiMo 问答 | 有真实串口日志和 10 轮稳定性记录 | `logs/hardware-2026-09-01-stability-ok-full.log`、`STATUS.md` | 已有证据 |
| 白名单本地工具 | LED、设备信息、BOOT 键有源码与真机记录 | `firmware/ai_agent_overlay/src/tools/`、`STATUS.md` | 已有证据 |
| 运行时 Skill | `home_security.md` 原文 3185 字节已写入设备并执行一次主动 LED 场景 | `app/homemind/skills/`、`logs/hardware-2026-09-02-skill-runtime.log`、`STATUS.md` | 已有证据 |
| MQTT 设备侧闭环 | `led.on/off` 各真实通过 `acked → done → status` | `docs/evidence/2026-09-02_mqtt_gateway_loop.txt` | 已有证据 |
| 摄像头 / 离线唤醒 | 当前板端无 video/audio/I2S 节点，源码也无离线唤醒模型 | `docs/evidence/2026-09-02_media_probe.txt` | 未完成，不能宣称 |
| 真实米家设备 | 未接入真实灯/插座，仓中无桥接实现 | `docs/HomeMind_比赛冲刺计划_2026-08-30_至_2026-09-20.md`、`STATUS.md` | 未完成，不能用板载 LED 替代 |
| 云端 API / 数据库 / WSS | 后端源码已有接口；本轮只完成本地解绑、TTL 和小程序轮询改动，公共 API 未部署/未验收 | `backend/api/`、`miniprogram/`、`STATUS.md` | 待部署与真机验收 |
| 微信小程序 | JS/JSON 静态检查通过；未完成开发者工具清洁编译和微信真机测试 | `miniprogram/README.md`、本轮工作记录 | 待外部验收 |
| 技术报告 | 官方原版和事实工作副本存在；尚未形成最终 DOCX/PDF | `docs/submission/` | 待定稿/导出 |
| 演示视频 / 实物照片 | 当前仓库未发现最终视频和硬件多角度照片 | `docs/submission/HomeMind_视频脚本_事实版.md` | 待用户录制/提供 |
| 官方仓提交 | 本地 `contest-final` 有代码，尚未 push、PR 或 merge | `git log`、`STATUS.md` | 用户确认后再做 |

## 最终交付前的外部门禁

1. 部署并验证公共 FastAPI 的最新后端改动，完成真实微信登录、绑定、命令、WSS 和失败回显。
2. 取得真实米家设备、合法桥接方式和开关重复测试记录。
3. 在具备对应硬件/固件支持后，才补录摄像头或离线唤醒；否则按已知限制提交。
4. 录制不超过 5 分钟的事实版视频，拍摄无秘密的硬件多角度照片。
5. 更新官方 DOCX，导出 PDF，生成只含最终材料的压缩包并保存 SHA-256/提交回执。
6. 最后才由用户决定 push、PR 和 merge；本轮不执行远程 push。
