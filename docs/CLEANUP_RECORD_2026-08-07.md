# HomeMind 工程整理记录（2026-08-07）

## 整理目标

- 固件开发整体迁移到 Ubuntu 22.04；
- Win11 保留微信小程序和 UG 外壳；
- 不直接删除旧文件；
- 建立可校验的 Ubuntu 迁移包和 Win11 资产备份；
- 统一当前 README、状态、快速开始和双机工作流。

## 已移动到可恢复归档

| 原位置 | 新位置 | 原因 |
| --- | --- | --- |
| 根目录旧迁移说明与 2026-07-28 包 | `_archive/migration_20260728/` | 避免与新 Ubuntu Core 包混用 |
| `_query.py`～`_query4.py` | `_archive/temp_queries/` | 临时检查脚本，不属于产品工程 |
| 根目录 `任务记录.md` | `_archive/legacy_notes/` | 已转换为当前 README/STATUS/迁移文档 |
| 项目 `.venv-tools/` | `_archive/local_envs/` | 硬编码旧用户路径，当前不可用且不应迁移 |
| 原比赛模板 README | `docs/legacy/README_contest_template_2026.md` | 保留官方参考，项目根 README 改为作品说明 |
| 旧 PROJECT_GUIDE/QUICKSTART | `docs/legacy/` | 旧 Windows-first 入口已被 Ubuntu 流程替代 |

## 新增或更新

- 工作区 `README_WORKSPACE.md`；
- 项目 `README.md`、`STATUS.md`、`PROJECT_GUIDE.md`、`QUICKSTART.md`；
- `MIGRATION_UBUNTU.md`；
- `docs/DEVELOPMENT_WORKFLOW.md`；
- 外壳、小程序、旧服务端和 HomeMind 资料目录说明；
- Ubuntu Core/FullRepository 两种迁移包模式；
- Win11 外壳与小程序备份脚本；
- Ubuntu 构建默认并发由 4 调整为 2；
- Wi-Fi PSK 命令日志改为 `<redacted>`。

## 未修改

- UG `.prt` 和 STL 内容；
- 微信小程序业务源码；
- 现有 BIN/ELF 基线产物；
- FastAPI 业务源码；
- 开发板 Flash 和串口状态。

## 恢复方式

所有移动操作都在 `C:\Old\HomeMind` 内完成。需要恢复时从 `_archive/` 按上表移回原位置即可。旧 Windows 虚拟环境不建议恢复，应按平台重新创建。
