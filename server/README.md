# HomeMind 旧服务端骨架（冻结）

本目录是早期 FastAPI/MQTT/小程序端云方案的骨架，不属于当前“ESP32-S3-EYE 独立联网并直接调用 MiMo”的 MVP。

当前服务不能直接启动：缺少 `services` 包、Dockerfile、MQTT/Nginx 配置和测试，路由仍使用内存模拟数据。迁移到 Ubuntu 时默认保留在 Win11 完整仓库中，但不进入 Ubuntu Core 包，也不继续投入固件主线工时。

如未来重新启用，应作为独立增强项目重新设计和测试，不能把它变成 ESP32 联网或 LLM 调用的必要网关。
