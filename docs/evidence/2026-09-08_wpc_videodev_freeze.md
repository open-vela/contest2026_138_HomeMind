# 2026-09-08 WP C 端侧 TFLM 人员检测：卡点重定位证据

> 状态：**未实现**（目标能力尚未接入）。本文只记录实测证据与结论，不升级任何状态。
> 固件：`contest2026_138_HomeMind/artifacts/nuttx.bin` 1418316 B（2026-09-08 15:49 构建，`EXIT=0`）。
> 环境：Ubuntu 192.168.31.251，板串口 `/dev/ttyACM1`，ESP32-S3-EYE。
> 代码位置（三处同步，md5 一致）：`packages/ai_agent/src/vision/person_detect.cc`、
> `packages/ai_agent/src/channels/nsh_commands.c`、官方仓 `firmware/ai_agent_overlay/src/**`。

## 1. 结论：真卡点是 `/dev/video0` 访问冻结系统，不是 TFLM

| 观察 | 证据 | 说明 |
| --- | --- | --- |
| 堆空间充足 | `vela>` 下 `heap_info`：`Heap: arena=8525536 fordblks(free)=6873848 uordblks(used)=1651688` | 8.5 MB 堆仅用 1.65 MB；模型 300 KB + arena 136 KB + 帧 150 KB 合计 < 0.6 MB。**堆紧张假设不成立** |
| 纯摄像头路径同样冻结 | `media_probe` 只输出 `MEDIA_PROBE_BEGIN video=/dev/video0 audio=/dev/audio/pcm_in0` 后无响应，其后 `heap_info` 亦无输出 | `media_probe` **完全不触碰 TFLM**。其 BEGIN 之后第一步即 `open("/dev/video0", O_RDWR\|O_NONBLOCK)`（nsh_commands.c:877） |
| TFLM 路径冻结 | `vision local 0.5` 全程零控制台输出，之后 CLI 无响应 | 与 `media_probe` 表现一致，指向同一设备 |
| 新固件确实已烧入 | `strings nuttx.elf` 命中 `heap probe`(3)、`S0 schedlock`(1)、`S0 critical in`(0)；`nm` 可见 `T hm_pd_heap_probe` | 排除「改了没编进去」的可能 |

**由此推翻此前的两个判断**：
1. 「camera DMA 写越界破坏 kmm heap / 内存不足」——堆空闲 6.87 MB，不成立；基于此的 `+2048 padding`、调换 init/capture 顺序均属无效尝试。
2. 「capture 与 TFLM init 不能串联共存」——是表象。两者都只是访问了同一个会冻结的摄像头设备，**顺序无关**。

## 2. 本轮已落地的代码修复（正确性与卡点是否解除无关）

1. **删除 `enter_critical_section()` 关中断**（person_detect.cc）。
   原实现在关中断区间内调用 `pd_flog()` → littlefs 写 SPI flash。关中断下执行 flash 擦写/GC 会卡死 flash 驱动或残留未释放的堆锁，与 `/data/pd.log` 中「第一次 init 完整通过、之后每次卡在 `malloc64k`」的现象吻合。现仅保留 `sched_lock()`（中断仍开，flash/DMA/WiFi 可正常推进），`run`/`Invoke` 同理。
2. **resolver 改堆分配，修复悬空引用**（person_detect.cc）。
   此前为绕开 `__cxa_guard_acquire` 死锁，将 `MicroMutableOpResolver<8>` 改为栈上对象；但 TFLM `micro_interpreter.h:162` 声明为 `const MicroOpResolver& op_resolver_`（引用语义），init 返回后该引用即悬空。改为 `new` 出的 `g_resolver`：既无 guard 变量，生命周期也与 interpreter 对齐。
3. **摄像头缓冲常驻**（nsh_commands.c）。
   `hm_media_capture_rgb565` 的三块 ~152 KB 缓冲改为一次分配、不再 `free`，消除 close 后 GDMA 仍写已释放块破坏堆的可能。`vision local` 顺序还原为正确的 **init → capture → run**，四个阶段各插入 `hm_pd_heap_probe()` 埋点。
4. 新增 `hm_pd_heap_probe(tag)` / `hm_person_detect_ready()`；`pd_flog` 增加 32 KB 自截断，避免 littlefs 反复 GC。

## 3. 未闭合的歧义（下一轮必须先解决）

- 烧入新固件后 `vision local 0.5` 零输出，重启后 `/data/pd.log` **无任何新记录**。两种解释尚未区分：
  ① 系统在 `cmd_vision` 早段即冻结，littlefs 未 flush 导致日志丢失；② `pd_flog` 根本未执行。
- **板子当前无网络**：`net_status` = `Network connected: no / IP: 0.0.0.0`（既有 P0：`set_wifi` 的 `essid_pre_ap` 关联失败）。
  原计划的「ping 存活对照」因此失效（ping 0.0.0.0 实际打到 127.0.0.1），**未能区分「系统冻结」与「仅 USB-CDC 控制台失灵」**。
- 建议下一步：先按既有可靠路径手动配网（`ifup wlan0` → `wapi mode/psk/ap/essid` → `renew`），拿到 IP 后重做 ping 对照；或用 LCD/LED 心跳作为不依赖 UART 的存活指示。

## 4. 后续排查方向

- 摄像头驱动 `open()`/`VIDIOC_S_FMT` 路径本身（建议在驱动内加 `syslog`/RAM 标记定位到具体 ioctl）。
- **内部 DRAM 占用**：GDMA 描述符与缓冲必须位于片内 DRAM；CLI 线程栈 64 KB 此前已从 PSRAM 移入 DRAM，需核查是否挤压了摄像头驱动所需空间。
- 不要重复排查：堆大小、MTU、非阻塞 socket、SPIRAM_RODATA、`/dev/esp32s3flash`、UDP 日志（均已有结论，见项目长期记忆）。

## 5. 复用工具（transfer\）

`sshcmd.py`（SSH 执行 stdin 脚本）、`pushrun.py`（SFTP 上传+执行）、`hm_ser.py`（串口脚本驱动）、
`wpc_fvt.py`（烧录+双次 vision+取日志）、`wpc_probe.py`（干净单步探测）、`wpc_ping.py`（ping 存活对照）。

> 注意：paramiko 仅存在于系统 Python 3.12；板子冻结时串口 `write()` 会永久阻塞，探测脚本必须设 `write_timeout`。
