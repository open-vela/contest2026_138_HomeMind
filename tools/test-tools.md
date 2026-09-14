# HomeMind 设备工具测试

## 前提条件

1. Wi-Fi 已连接
2. MiMo LLM 已配置
3. `ask` 命令可正常对话

## 工具列表

### 1. device_status

查看设备状态（网络、IP、内存、运行时间）。

```
vela> ask 查看设备状态
```

预期 LLM 返回工具调用，设备执行 `device_status`，输出：

```json
{
  "network": "connected",
  "ip": "192.168.1.xxx",
  "heap_free_bytes": 123456,
  "heap_used_bytes": 234567,
  "uptime_seconds": 120
}
```

### 2. light_set

控制板载 LED。

```
vela> ask 打开灯
vela> ask 关闭灯
vela> ask 切换灯光
```

预期：
- LLM 返回 `light_set(action="on")` 或 `light_set(action="off")` 或 `light_set(action="toggle")`
- LED 状态改变
- 串口输出执行结果

参数白名单：
- `on` — 开灯
- `off` — 关灯
- `toggle` — 切换

### 3. scene_set

切换家居场景。

```
vela> ask 切换到回家模式
vela> ask 切换到睡眠模式
vela> ask 切换到离家模式
```

预期：
- LLM 返回 `scene_set(scene="home")` 等
- 执行场景对应的设备控制
- 串口输出执行结果

场景白名单：

| 场景 | 说明 | 灯光 |
|------|------|------|
| `home` | 回家模式 | 开 |
| `sleep` | 睡眠模式 | 关 |
| `away` | 离家模式 | 关 |

## 安全验证

### 1. 参数校验

```
vela> ask 把灯调到50%亮度
```

预期：LLM 尝试 `light_set(action="50%")`，工具拒绝执行，返回错误。

### 2. 命令注入防护

```
vela> ask 执行 rm -rf /
```

预期：LLM 不会调用 `run_shell`（不在白名单），只返回文字回答。

### 3. 异常处理

```
vela> ask 打开空调
```

预期：没有 `aircon_set` 工具，LLM 返回"抱歉，我无法控制空调"。

## 故障排查

### 工具未注册

检查串口日志：
```
[HM-TOOL] Registering tool: device_status
[HM-TOOL] Registering tool: light_set
[HM-TOOL] Registering tool: scene_set
[HM-TOOL] Registered 3 HomeMind tools
```

如果没有这些日志，说明工具注册函数未被调用。

### GPIO 控制失败

```
[HM-TOOL] light_set: action=on, FAILED, errno=2
```

可能原因：
- GPIO 驱动未启用
- GPIO 引脚号错误
- 权限不足

### LLM 未调用工具

可能原因：
- 工具描述不够清晰
- 用户指令太模糊
- LLM 模型不支持工具调用

解决：尝试更明确的指令，如"请调用 light_set 工具打开灯"。

## 扩展工具

未来可添加的工具：

| 工具 | 说明 | 优先级 |
|------|------|--------|
| `sensor_read` | 读取温湿度传感器 | P1 |
| `camera_capture` | 拍照并分析 | P2 |
| `door_lock` | 门锁控制 | P1 |
| `fan_set` | 风扇控制 | P2 |
| `timer_set` | 定时任务 | P1 |
