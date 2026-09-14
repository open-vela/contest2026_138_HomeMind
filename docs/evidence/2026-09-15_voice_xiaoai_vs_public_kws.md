# 2026-09-15 语音入口方案：小爱音箱 + HomeMind 中枢（对照公开 KWS）

## 结论（给报告/演示用）

| 路线 | 结论 | 原因 |
| --- | --- | --- |
| 公开中文 KWS 上 ESP32-S3/OpenVela | **本轮不做** | 需 INT8+TFLM/NuttX 适配、RAM/时延标定、板载麦域差；截止 09-20 风险过高 |
| 小爱音箱作中文语音入口 | **采用** | 多功能房已有小爱音箱Pro；原生中文唤醒/指令可靠；原始音频留在小米设备与家庭内 |
| 板载 `kws_listen` | **保留为实验能力** | 持续监听闭环已通（frames=155/8s）；判别率未达标，不写成已实现唤醒 |

## 实测拓扑（2026-09-14）

```
人声「小爱同学…」
    → 小爱音箱Pro-多功能房 (media_player.xiaomi_cn_2085562629_lx06)
    → 米家/HA 控制 多功能房吸顶灯
         light.leishi_cn_940744854_eps127_s_2_light
    → HomeMind ha_bridge 轮询状态变化（5s）
    → SQLite home_events: light_state_changed
    → （可选）场景/提醒/播报 device/<id>/speak → 小爱 TTS
```

板端并行：本地 LED / vision / kws_listen / 场景命令；家庭 API 编排待办与睡前场景。

## 本机验收

- HA bridge 启动：`url=http://127.0.0.1:8123`，监视灯 + 小爱 media_player
- HA `light.turn_off` / `turn_on` → HomeMind 各 1 条 `light_state_changed`
  - 例：`{"old":"on","new":"off","via":"xiaomi_home_ha",...}`
- 睡前场景灯实体已改为真实 entity：`light.leishi_cn_940744854_eps127_s_2_light`

## 演示脚本建议（≤30s）

1. 「小爱同学，关灯」→ 真实吸顶灯灭  
2. 串口/日志或 API：HomeMind 出现 `light_state_changed`  
3. 板端 `kws_listen status` 仍 run=1（实验监听）  
4. `POST /v1/scenes` bedtime：关灯 + 待办 + 小爱播报  

## 报告口径（禁止夸大）

- 可写：「家庭中文语音入口采用小爱音箱；HomeMind 通过 HA 观察设备状态并编排场景/待办/播报；板端具备离线能量唤醒与持续 KWS 实验链路。」  
- 不可写：「板端已实现高准确率你好 openvela 唤醒」或「公开 KWS 已在 OpenVela 落地」。

## 源码位置

- `backend/api/app/ha_bridge.py` — HA 状态桥  
- `backend/api/app/main.py` — `start_ha_bridge(interval_sec=5)`  
- `backend/api/app/routers/scenes.py` — 真实灯 entity  
- 部署：`/home/hfy/homemind-backend`，`homemind-api` active  
