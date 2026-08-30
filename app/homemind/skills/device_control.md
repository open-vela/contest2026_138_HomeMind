# 设备控制技能

## 描述
设备控制技能提供智能家居设备的统一控制接口，支持灯光、开关、传感器等设备的控制和管理。

## 支持设备类型
- 灯光（light）：开关、亮度、色温、颜色
- 开关（switch）：开关状态
- 传感器（sensor）：温度、湿度、光照、噪音
- 摄像头（camera）：拍照、录像、预览
- 其他（other）：自定义设备

## 控制命令
### 灯光控制
`json
{
  "device_id": "light_001",
  "command": "turn_on",
  "parameters": {
    "brightness": 80,
    "color": "warm_white"
  }
}
`

### 开关控制
`json
{
  "device_id": "switch_001",
  "command": "turn_off"
}
`

### 传感器查询
`json
{
  "device_id": "sensor_001",
  "command": "get_data"
}
`

## 自动化场景
### 场景示例
1. **回家模式**：检测到有人回家，自动开灯、开空调
2. **离家模式**：检测到无人，自动关灯、关空调、启动安防
3. **睡眠模式**：定时关灯、调低空调温度
4. **起床模式**：定时开灯、播放音乐

### 场景配置
`json
{
  "scene_name": "回家模式",
  "triggers": [
    {
      "type": "face_detected",
      "person": "family_member"
    }
  ],
  "actions": [
    {
      "device_id": "light_001",
      "command": "turn_on",
      "parameters": {"brightness": 80}
    },
    {
      "device_id": "ac_001",
      "command": "turn_on",
      "parameters": {"temperature": 25}
    }
  ]
}
`

## 使用示例
`
用户：打开客厅灯
系统：已打开客厅灯，亮度 80%
用户：关闭所有设备
系统：已关闭所有设备
用户：设置回家模式
系统：已配置回家模式，将在检测到有人回家时自动执行
`

## 注意事项
- 设备控制需要设备在线
- 部分设备可能需要额外配置
- 自动化场景需要传感器支持
