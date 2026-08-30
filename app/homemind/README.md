# HomeMind 应用资料

本目录目前包含产品配置草案和五份 Skill 设计文档，不是已经接入 OpenVela 构建的完整 C 应用。

当前固件修改实际位于 `tools/staging-*.c`，由 `tools/deploy-to-vm.sh` 部署到完整 OpenVela 工作区的 `packages/ai_agent`。后续应在比赛技术组确认推荐提交方式后，把 staging 修改转换为正式补丁、上游关联提交或可构建的 HomeMind 应用接入。

`configs/default_config.json` 仍保留早期 FastAPI/MQTT 字段，仅作历史设计参考，不代表当前独立版运行配置。
