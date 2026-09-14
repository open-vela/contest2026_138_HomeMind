# 2026-09-09 人员检测初始化生命周期修复

状态：**源码与构建已验证，端侧推理待验收**。源码修复已纳入 Ubuntu 诊断构建并烧录，esptool 报告校验通过；不能据此宣称端侧人员检测或摄像头冻结已修复。

## 问题与改动

文件：`firmware/ai_agent_overlay/src/vision/person_detect.cc`；基线为 Ubuntu 官方工作区 HEAD `aaefdcb`。

- 原初始化函数先分配并加载 300568 字节模型，再判断 interpreter 是否已经存在。重复调用会覆盖模型指针，泄漏此前缓冲。
- 模型、resolver、arena、interpreter 的失败路径没有统一清理；部分失败后仍有 interpreter 指针，会把未完成的初始化误判为已成功。
- 改为成功状态提前返回、失败统一按依赖顺序释放、close 复用同一清理函数；失败后允许重新初始化。
- 模型日志原先读取 `g_model_ram[299 * 1024]`，偏移 306176 超出 300568 字节分配。改为读取最后一个合法字节并调整日志标签。

## 验证边界

- Ubuntu Xtensa 编译器执行 `-fsyntax-only -std=c++11`，退出码 0；使用当前 NuttX 配置与 TFLM/FlatBuffers 等实际头文件。存在 TFLM 头文件 C++ 标准警告及编译器不支持 `-Wno-atomic-alignment` 的警告，未报编译错误。
- 完整 `scripts/build.sh build` 退出码 0；随后使用 esptool v5.3.1 烧录 `artifacts/nuttx.bin`，报告 `Hash of data verified`。本次诊断 BIN SHA-256 为 `e02c44127634d203363283efddd15d98c0eba443f6a1fac1f186959d09f3cf31`。
- 源码检查确认成功分支在模型加载之前，原越界下标已删除、最后合法下标为 300567，各初始化失败分支在返回前清理资源；`git diff --check` 通过。
- 这是语法、静态控制流与完整链接/烧录检查，**没有运行失败注入或内存泄漏测试，也没有完成真实 TFLM 推理验收**。
- 仓外原文件备份：Ubuntu `/home/hfy/person_detect.cc.init-fix-backup-20260909`。修复仍未提交；诊断构建同时包含摄像头阶段标记，不能把该 BIN 当作人员检测验收版本。

复验语法检查（Ubuntu，当前配置 `CONFIG_TFLITEMICRO=y`）：

```sh
cd /home/hfy/work/openvela-clean-20260830
./prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-g++ \
  -fsyntax-only -std=c++11 -fno-common -Wall -Wshadow -Wundef \
  -Wno-attributes -Wno-unknown-pragmas -Wno-atomic-alignment -Wno-psabi \
  -fno-exceptions -fcheck-new -fno-rtti -Os -fno-strict-aliasing \
  -fomit-frame-pointer -ffunction-sections -fdata-sections \
  -fno-strength-reduce -mlongcalls -D__NuttX__ \
  -isystem nuttx/include -isystem apps/crypto/mbedtls/include \
  -isystem apps/crypto/mbedtls/mbedtls/include \
  -I apps/system/flatbuffers/flatbuffers/include -I apps/include \
  -I contest2026_138_HomeMind/firmware/ai_agent_overlay/include \
  -I contest2026_138_HomeMind/firmware/ai_agent_overlay/src \
  -I apps/mlearning/tflite-micro/tflite-micro \
  -I apps/math/gemmlowp/gemmlowp -I apps/math/kissfft/kissfft \
  -I apps/math/ruy/ruy -Wno-shadow -Wno-sign-compare -Wno-undef \
  -DTFLITE_EMULATE_FLOAT -DTF_LITE_DISABLE_X86_NEON \
  -DTF_LITE_STRIP_ERROR_STRINGS \
  contest2026_138_HomeMind/firmware/ai_agent_overlay/src/vision/person_detect.cc
```

## 实机相关性

本轮[摄像头/网络对照](2026-09-09_camera_network_probe.md)在包含该修复的诊断 BIN 上执行，但 `media_probe` 不经过上述模型初始化；阶段标记把卡点定位到 `capture_open()` 的管理锁等待。因此这些修复是独立的源码正确性修复，尚不能解释或解除摄像头命令导致的串口/网络失响应。
