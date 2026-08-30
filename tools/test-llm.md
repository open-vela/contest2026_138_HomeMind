# MiMo Token Plan 直连测试

## 前提条件

1. Wi-Fi 已连接（`net_status` 显示 IP）
2. 无代理配置（未执行 `set_proxy`）
3. TLS 直连正常（`net_test` 通过）

## 测试步骤

### 1. 配置 MiMo LLM

```
vela> set_llm mimo <your_token_plan_key>
```

其中 `<your_token_plan_key>` 是从 MiMo Token Plan 获取的 API Key。

### 2. 切换模型（可选）

```
vela> set_llm model mimo-v2-pro
```

可用模型：
- `MiMo-v2-Flash`（默认，快速）
- `MiMo-v2-Pro`（更强能力）
- `MiMo-v2-Omni`（多模态）

### 3. 验证配置

```
vela> config_show
```

预期输出（脱敏）：
```
LLM Backend: mimo
Model: MiMo-v2-Flash
API Key: sk-abcd****efgh
```

### 4. 测试对话

```
vela> ask 你好，请介绍一下你自己
```

预期输出：
```
[HM-LLM] HTTPS POST token-plan-cn.xiaomimimo.com:443/v1/chat/completions (body=xxx bytes)
[HM-NET] Direct TCP connect to x.x.x.x:443
[HM-NET] TCP connect OK, fd=5
[HM-TLS] TLS handshake starting: host=token-plan-cn.xiaomimimo.com
[HM-TLS] Handshake OK: version=TLSv1.2, cipher=... (xxxms)
[HM-LLM] HTTP response status: 200
你好！我是 MiMo，一个由小米开发的大语言模型...
```

### 5. 多轮对话测试

```
vela> ask 今天天气怎么样？
vela> ask 帮我写一首关于春天的诗
```

### 6. 重启后验证

```
# 重启开发板
nsh> ai_agent
vela> net_status    # 应自动连接 Wi-Fi
vela> ask 你好      # 应正常工作，不需重新配置
```

## 故障排查

### HTTP 401 Unauthorized
- API Key 无效或过期
- 重新执行 `set_llm mimo <new_key>`

### HTTP 403 Forbidden
- API Key 权限不足
- 检查 Token Plan 账户状态

### HTTP 429 Too Many Requests
- 请求过于频繁
- 等待一段时间后重试

### HTTP 5xx Server Error
- MiMo 服务器问题
- 稍后重试

### TLS 握手失败
- 检查 `[HM-TLS]` 日志
- 确认网络可访问 `api.mimo.ai:443`
- 确认无代理干扰

### 连接超时
- 检查 `[HM-NET]` 日志
- 确认 DNS 解析正常
- 确认防火墙未阻断 443 端口

## MiMo API 端点信息

- **Base URL**: `https://token-plan-cn.xiaomimimo.com/v1`（MiMo Token Plan 官方端点）
- **Chat Completions**: `POST /v1/chat/completions`
- **认证方式**: `Authorization: Bearer <api_key>`
- **Content-Type**: `application/json`
- **兼容接口**: OpenAI Chat Completions API

## 注意事项

1. API Key 只保存在设备持久存储中，不写入源代码
2. `config_show` 脱敏显示 API Key
3. 串口日志不输出完整 Authorization header
4. 首版只做非流式响应
5. 请求超时 60 秒
