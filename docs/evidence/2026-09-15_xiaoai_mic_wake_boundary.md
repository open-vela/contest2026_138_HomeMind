# 小爱麦克风 ×「我要睡觉了」× 与「你好，openvela」的边界

## 硬件事实（必须写进报告，不可含糊）

| 能力 | 小爱音箱Pro (LX06) | ESP32-S3-EYE 板载麦 |
| --- | --- | --- |
| 自定义唤醒词「你好，openvela」 | **否**（固件唤醒词固定「小爱同学」） | **可以自研**（kws/ASR） |
| 识别结果能否被 HomeMind 读取 | **否**（官方 xiaomi_home 无 last_query/conversation） | 可以（PCM/特征留在家庭内） |
| 中文识别质量 | 厂商云端，好 | 受限于板载麦与小模型 |

因此：**不能**用小爱的麦克风实现「你好，openvela」唤醒。小爱麦只能支撑「小爱同学 + 米家指令」。

## 推荐演示链路（小爱麦 + HomeMind 语义）

```
米家 App 创建场景「我要睡觉了」→ 关闭 多功能房吸顶灯
        （或直接「小爱同学，关灯」）
    → 小爱识别并执行（厂商 ASR，不在 HomeMind）
    → HA 灯 on→off
    → HomeMind ha_bridge（5s）
        · 记录 light_state_changed
        · HA_AUTO_BEDTIME=1 时自动：
            - 待办「睡前刷牙」「检查门窗与闹钟」
            - 板端 led.off
            - 小爱 TTS：「好的，进入睡前模式…晚安」
```

HomeMind 价值不在“再做一遍小爱唤醒”，而在：**状态感知、任务编排、提醒、隐私边界、板端离线工具**。

## 若必须字面唤醒「你好，openvela」

只能走板载链路（与小爱麦互斥）：
`kws_listen` / 家庭侧本地 ASR → 命中后指令「我要睡觉了」→ `/v1/scenes`。
该路径准确率未达标，不得写成已验收。

## 验收开关

- `HA_AUTO_BEDTIME=1`（默认开，冷却 180s）
- `HOMEMIND_SPEAK_ENTITY=notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1`
