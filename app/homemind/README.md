# HomeMind 应用资料

本目录目前包含产品配置、历史 Skill 设计文档和一个按当前 ai_agent 平铺 `.md` 格式编写的运行时 Skill 源文件，不是已经接入 OpenVela 构建的完整 C 应用。

当前固件修改实际位于 `tools/staging-*.c`，由 `tools/deploy-to-vm.sh` 部署到完整 OpenVela 工作区的 `packages/ai_agent`。后续应在比赛技术组确认推荐提交方式后，把 staging 修改转换为正式补丁、上游关联提交或可构建的 HomeMind 应用接入。

`skills/home_security.md` 是比赛仓库中的运行时 Skill 源文件，已通过静态约束并以原文 3185 字节安装到真机；设备实际加载路径由固件的 `AGENT_SKILLS_DIR` 决定，本机已验收固件使用 `/data/ai_agent/skills/`。离线场景使用固件受限的 `skill_write_begin` / `skill_write_hex` / `skill_write_commit` 串口导入路径，避免依赖被 Wi-Fi 客户端隔离阻断的局域网 HTTPS。`configs/default_config.json` 仍保留早期 FastAPI/MQTT 字段，仅作历史设计参考，不代表当前独立版运行配置。
