# 2026-09-15 四大核心能力对照与本轮实现

## 1 离线语音唤醒与端侧感知

| 目标 | 状态 | 说明 |
| --- | --- | --- |
| 本地离线 KWS 持续监听 | **代码+烧录闭环** | `kws_listen`，8s frames=155；**判别率未达标** |
| 无需网络捕捉指令 | 部分 | 本地 CLI/LED/能量 VAD 可离线；复杂语义需家庭局域网 |
| 周期摄像头 + 人脸/手势 | 未完成 | `vision local` 单次混合存在检测可用；`vision_loop` 仅为 tick 心跳，与 `kws_listen` 需互斥，手势/人脸身份未做 |

## 2 端云协同语义理解（本轮重点）

```
板端 intent_send 我准备睡觉了
  → MQTT device/esp32s3-eye/intent 或 homemind {type:intent,text}
  → 家庭私有云 plan_text()
       · 有 MIMO_API_KEY：文本送 MiMo（不出原始音频）
       · 无 Key/失败：本地规则回落
  → 白名单结构化 JSON actions
  → execute_plan：scene/led/xiaoai_execute/speak/task
```

验收：
- `POST /v1/agent/plan` text=我准备睡觉了 → intent_type=bedtime，scene 执行 ok
- MQTT intent 关灯 → device.control，actions=2
- `GET /v1/agent/health` → llm_configured=false，fallback=local_rules
- 板端命令：`intent_send`（需烧录含该命令的固件）

## 3 米家联动与小程序

| 目标 | 状态 |
| --- | --- |
| 米家设备控制 | **已验收**：HA + 小爱 `execute_text_directive` 关/开多功能房吸顶灯 |
| 感知状态上报私有云 | 已有：person_detected / light_state_changed / plan_created |
| 小程序跨端日程资产 | **未完成** 同步闭环 |

## 4 隐私与延迟

| 目标 | 状态 |
| --- | --- |
| 断网基础控制 | 本地 LED/CLI/agent_loop 冷启动已改；米家/小爱仍需局域网 |
| 原始音视频不出家庭 | API `PRIVACY_MODE` 默认 403；语义链路仅文本 |
| 毫秒级整套交互 | **不主张**；局域网 MQTT+规划为百毫秒～秒级 |

## 源码

- `backend/api/app/agent_plan.py` / `routers/agent.py`
- `backend/api/app/mqtt_client.py` intent 分支
- `firmware/.../nsh_commands.c` `intent_send` / `vision_loop`
- ACL：home-api 读 event/intent，home-gateway 写 event/intent
