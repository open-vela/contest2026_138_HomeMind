---
name: homemind-build-flash-accept
description: HomeMind 开发流程 Skill：构建—烧录—串口验收—归档。触发于需要复现固件、切换演示 BIN、或整理验收证据时。
---

# HomeMind 构建—烧录—串口验收—归档

## 触发条件

- 需要在 Ubuntu 权威现场重建/烧录 ESP32-S3-EYE 固件
- 需要把 overlay 源码同步进 openvela 树并生成 `artifacts/nuttx.bin|elf`
- 需要把一次通过的验收结果与 SHA-256 绑定归档

## 权威路径

- 工作区：`/home/hfy/work/openvela-clean-20260830/contest2026_138_HomeMind`
- Overlay：`firmware/ai_agent_overlay/`
- 部署脚本：`tools/deploy-to-vm.sh`
- 串口稳定别名：`/dev/homemind-esp32`（udev → `ttyACM*`）
- 产物：`artifacts/nuttx.bin`、`artifacts/nuttx.elf`、`artifacts/SHA256SUMS`

## 步骤

1. **同步 overlay**  
   运行 `tools/deploy-to-vm.sh`，确认 `src/vision/*`、`nsh_commands.c` 等进入 openvela 构建树。

2. **配置与构建**  
   按 `scripts/build.sh` 既有媒体/视觉/I2S 配置执行干净构建；记录构建日志与耗时。

3. **写哈希**  
   `sha256sum artifacts/nuttx.bin artifacts/nuttx.elf > artifacts/SHA256SUMS`  
   同步更新 `firmware/ai_agent_overlay/SOURCE_SNAPSHOT.json`。

4. **烧录**  
   esptool 写入后校验 hash；保存 `flash_*.log`。失败不得用旧 BIN 冒充。

5. **串口验收（最小集）**  
   - 进入 `vela>`（必要时 nsh 下 `ai_agent`）  
   - `net_status` connected  
   - `ask 打开灯` / `ask 关灯` 应出现 `[Agent]: {"led":"on"|"off"}`  
   - 经家庭 API 下发 `led.on`/`led.off`/`device.info`/`mihome.set_power` 并核对 status=done  
   - 灯类动作必须有真实回读（HA/物理），失败不得标 done

6. **归档**  
   - 证据写入 `docs/evidence/YYYY-MM-DD_*.md`（日期、样本数、成功/失败、BIN 哈希）  
   - 源码提交到 `contest-final` 并 push fork  
   - 未通过项写入缺口，不改写历史日志

## 输出证据格式

```markdown
# YYYY-MM-DD 项目名
- 固件 BIN/ELF SHA-256:
- 环境: Ubuntu host / 串口 / 网关版本
- 样本: N 次，成功 x，失败 y
- 结果: 已验收 / 部分完成 / 未实现
- 边界: 不覆盖的能力
```

## 安全失败规则

- 无网络、无 `[Agent]` 回包、无真实回读 → 不得写 done
- 不把 VAD 写成指定词唤醒，不把遮挡样本写成通用准确率
- 不把 Windows 旧副本覆盖 Ubuntu 现场权威源
