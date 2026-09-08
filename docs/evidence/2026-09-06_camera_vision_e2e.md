# 摄像头拍照 → 云端转码 → MiMo 视觉识别 端到端验收 — 2026-09-06

> 2026-09-07 文档核查补注：本记录为公网视觉历史实验的现场验收记录，状态为**部分完成**（源码与产物待归集）。本地 BIN 为 `8cf605d9…`，与下方现场 `a0029225…` 不同；本地命令源码未找到所述 `cmd_vision` / `cmd_set_media`。不能据此证明当前工作副本已包含此实现。
>
> 本链路会外发原始图像；正式隐私模式须关闭该路径，不能作为端侧有人/无人或隐私验收。末尾 voice/公网 ASR/播报为历史路线，当前复杂语音目标改为家庭主机转写，小爱音箱播报不作为必交。当前口径见 [STATUS](../../STATUS.md)。

## 链路

`vision` 命令（vela> 控制台）→ OV2640 RGB565 QVGA 帧捕获（V4L2 DMA，153600 字节）
→ HTTPS POST（TLS，150KB body，`Authorization: Bearer MEDIA_TOKEN`）
→ 云端 `https://hfy-ai.cloud/v1/media/frame?w=320&h=240&swap=1&q=<提示词>`
→ 云端 RGB565→JPEG 转码（Pillow）→ MiMo `mimo-v2.5` 图像理解
→ JSON `{"text": 描述}` 回设备 → 串口打印 `[Vision]: <描述>`

## 固件

- BIN `a0029225a41a0cccdf5e3b71577d98c3957e6369b07e343bd322c87deecbd2a2`
- ELF `8ffea5234b5513ff2779b507903f06d95c7e1c62b9207ed42efaffca1a6e66fb`
- esptool 写 0x0 @460800，`Hash of data verified`
- 新增：`media_capture_rgb565()`、`cmd_vision`（含大包上传与结果解析）、`cmd_set_media`（media_host/port/token 持久化到 /data）

## 实测记录（2026-09-06）

1. **第一轮（镜头被遮挡/无光）**：MiMo 返回"近乎全黑的画面……镜头被遮挡、曝光严重不足"——与实际一致。
2. **第二轮（用户在镜头前放置紫色 PCB 电路板）**：MiMo 返回"这是一块紫色的印刷电路板……右上角带有丝印的编号标识，比如'23''C526'，板体的角落设有安装孔……被放置在浅色的平面上"——**用户口头指认镜头前为"紫色PCB电路板"，颜色、物体类型完全命中**，丝印细节待用户复核但格式合理。
3. 两轮均完整走完采集→上传→转码→识别→回显，网关服务测试后恢复 active。

## 边界

- OV2640 传感器 JPEG 模式经 4 轮迭代证实无法启用（DVP 恒输出 raw YUV422），识别链路采用"云端转码"方案；
- `vision` 结果行在串口上可能被 Wi-Fi 日志穿插（不影响解析）；
- `voice`（录音→ASR→问答→播报）为下一个待实现项；MiMo 音频理解能力已验证（`input_audio` base64 WAV）。
