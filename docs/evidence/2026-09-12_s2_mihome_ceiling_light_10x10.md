# 2026-09-12 S2 最小回归：多功能房吸顶灯开/关各 10 次通过

日期：2026-09-12。目标实体：`light.leishi_cn_940744854_eps127_s_2_light`（多功能房吸顶灯）。链路：小程序/本地 API → 家庭 FastAPI → 本地 MQTT → 家庭网关 → Home Assistant → 米家真实灯具，再以 HA 状态回读验收。

## 前置

- 09-12 上午该实体曾为 `unavailable`；用户修复米家/HA 连接后恢复为 `on`。
- 部署侧 `ALLOWED_ACTIONS` 已包含 `mihome.set_power,mihome.get_state`。
- 结束状态保持灯为 **off**。

## 结果

| 动作 | 次数 | API status | HA 回读 | 结论 |
| --- | --- | --- | --- | --- |
| `mihome.set_power on=true` | 10 | 10× `done` | 10× `on` | 通过 |
| `mihome.set_power on=false` | 10 | 10× `done` | 10× `off` | 通过 |

合计 **20/20** 一致；单次约 1.4s（queued→acked→done + HA 状态迁移确认）。

## 边界

- 本记录证明本轮最终家庭 API/网关/HA/真实吸顶灯闭环，不代表小程序真机预览或公网入口已复测。
- 失败不得显示 done；本轮无失败样本。
