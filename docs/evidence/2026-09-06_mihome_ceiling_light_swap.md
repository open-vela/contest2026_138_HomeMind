# 米家白名单更换为多功能房吸顶灯（含物理确认）— 2026-09-06

## 变更

- 用户指定验收设备从阳台开关换为**多功能房吸顶灯**（雷士 `leishi.light.eps127` 主灯）。
- 网关 `/home/hfy/homemind-gateway/.env`：
  - `HOMEMIND_MIHOME_ALLOWED_ENTITIES=light.leishi_cn_940744854_eps127_s_2_light`（替换原阳台开关实体；旧实体即刻失去小程序/网关控制权）；
  - 新增 `HOMEMIND_SPEAK_ENTITY=notify.xiaomi_cn_2085562629_lx06_play_text_a_5_1`（多功能房小爱音箱 Pro"播放文本"实体，供语音回复链路使用）。
- 网关服务重启时机：摄像头 JPEG 固件子代理正在构建主机做串口测试（需要独占 /dev/ttyACM0），配置在其完成后服务重启时生效。

## 验证（直连适配器，不占用串口）

- `describe`: enabled=True, allowed_entities=1；
- `list_entities()` 实测：`[{"entity_id": "light.leishi_cn_940744854_eps127_s_2_light", "name": "多功能房吸顶灯", "state": "on"}]` —— `_clean_name()` 将小米集成 friendly_name（"多功能房吸顶灯 灯 灯"）正确清洗为"多功能房吸顶灯"；
- `set_power on` → 回读 `on` 确认；`set_power off` → 回读 `off` 确认；
- **物理确认：用户目视多功能房吸顶灯确已熄灭**（2026-09-06 晚）。

## 边界

- 阳台开关已移出白名单，不再受小程序/网关控制（其灯当时为开，需用户自行用米家/小爱关闭）；
- 氛围灯实体未加入白名单；
- 小程序侧无需改动：设备页实体行由网关快照自动同步，网关重启后显示"多功能房吸顶灯"。
