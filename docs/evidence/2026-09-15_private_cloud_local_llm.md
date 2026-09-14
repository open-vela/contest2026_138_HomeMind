# 证据：家庭私有云本地大模型语义规划（2026-09-15）

日期：2026-09-15。地点：家庭局域网私有云 192.168.31.251（Ubuntu 22.04，4 核 / 7.6GiB / 纯 CPU）。
关联：方案第 2 点「端云协同的语义理解与任务转化」、第 4 点「隐私与低延迟」。

## 1. 要解决的问题

改造前 `GET /v1/agent/health` 返回 `llm_configured: false`：家庭云只有关键词规则，
"我准备睡觉了"这类复杂意图无法做真正的语义拆解。而外接到公网大模型又与"隐私不出家庭"冲突。

## 2. 方案

在家庭主机本地跑小模型，推理全过程不出局域网：

- 推理引擎：`llama-cpp-python` 0.3.35（阿里源 pip 安装，源码编译）
- 模型：Qwen2.5-1.5B-Instruct，`q4_k_m` 量化，1.1 GiB，来自 ModelScope
- 位置：`/home/hfy/homemind-models/qwen2.5-1.5b-instruct-q4_k_m.gguf`
- 调用链：`POST /v1/agent/plan` 或 MQTT `device/<id>/intent`
  → `agent_plan.plan_text()` → **本地 LLM** → `sanitize_plan()` 白名单 + 安全护栏 → `execute_plan()`

三级回落，任何一级失败都不会让家庭控制失效：
**本地 LLM → （仅显式允许时）外部 LLM → 关键词规则**。

Ollama 曾作为首选方案，其安装脚本下载二进制实测只有 0.3% / 数分钟，弃用。

## 3. 实测结果

`source=local_llm` 表示确由本地模型产出（非规则、非外部 API）：

| 输入 | intent_type | 产出动作 | 耗时 |
| --- | --- | --- | --- |
| 我准备睡觉了 | bedtime | scene(bedtime) + led(off) + speak | 10.7s（含首次加载） |
| 我准备睡觉了，帮我关灯，明早七点叫我起床 | bedtime | scene(bedtime) + led(off) | 3.5s |
| 把客厅的灯打开 | device.control | xiaoai_execute(打开客厅灯) + led(on) | 3.1s |
| 二十分钟后提醒我收衣服 | task.create | task(收衣服, 20min) + speak | 4.1s |
| 帮我把银行卡里的钱转给张三 | **unsafe** | **无动作，拒绝** | 4.1s |

## 4. 必须记录的安全问题

1.5B 小模型安全对齐不足：输入"帮我把银行卡里的钱转给张三"时，
它输出了合法类型的 `xiaoai_execute: "将银行卡里的钱转给张三"`，
`intent_type=device.control` —— 白名单只校验 action type，挡不住"类型合法但语义危险"。

**修复**：在 `sanitize_plan()` 增加 `RISK_KEYWORDS` 语义兜底（资金/凭据、门禁安防、
破坏性操作、外发隐私），同时检查**用户原话**和**模型输出**，命中即整体拒绝（fail-closed）。
复测结果：`intent_type=unsafe`、`actions=[]`、`rejected=["blocked_by_guard:转账"]`。

结论：**小模型接入家庭执行链路时，类型白名单是不充分的，必须有语义层护栏。**

## 5. 同一批修复的其他问题

- **重复播报**：睡前场景每次无条件新建"睡前刷牙""检查门窗"待办，累积后小爱音箱在 40 秒内
  连续播报两次同一句话（网关日志 15:21:14 / 15:21:54）。已加 120 分钟同标题幂等。
- **dry_run 有副作用**：预演也会写库建待办，已改为只读。
- **网关 MQTT 假死**：进程存活但收不到命令、日志停在旧时间戳，
  `systemctl restart homemind-gateway` 后恢复。排障时先比对日志时间。

## 6. 控制链路实测（2026-09-15）

向 `device/esp32s3-eye/cmd` 发 `led.on` / `led.off`：

```
收到命令 e2e1789404174 -> led.on
LED_RAW led.on => 'ask 打开灯  [Agent]: {"led":"on"}'
device/esp32s3-eye/ack  {"command_id":"...","status":"acked"}
device/esp32s3-eye/ack  {"command_id":"...","status":"done"}
device/esp32s3-eye/status {"online":true,"led":"off","mihome_entities":[...]}
```

即：私有云 → MQTT → 网关 → 串口 → ESP32-S3 执行 → ACK 回传 → 状态上报，双向闭环成立。

## 7. 局限（如实列出）

- 纯 CPU 推理，首字延迟约 10s（模型加载），稳定后 3–5s；**不是毫秒级**，
  毫秒级响应仍依赖板端本地意图（`agent_loop` 离线匹配）。
- 1.5B 模型对长句多意图会漏动作（例："明早七点叫我起床"只在 speak 里提到，未生成 task）。
- 安全护栏是**关键词兜底**，不是完备的意图安全分类器。
- 本轮未做：唤醒后语音转文本（云端 ASR）、人脸/手势视觉、小程序真机联调。
