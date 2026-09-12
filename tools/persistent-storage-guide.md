# ESP32-S3-EYE 持久存储配置

## 当前状态

- `CONFIG_FS_TMPFS=y` — `/data` 是 TMPFS（掉电丢失）
- `CONFIG_FS_PROCFS=y` — `/proc` 只读
- 无 LittleFS/SPIFFS/SPI Flash 持久分区

## 推荐方案：LittleFS on SPI Flash

### 为什么选 LittleFS

| 方案 | 优点 | 缺点 | 推荐 |
|------|------|------|------|
| LittleFS | 掉电安全、磨损均衡、空间效率高 | 需要 Flash 分区 | **推荐** |
| SPIFFS | 简单、低 RAM | 无目录支持、掉电不安全 | 不推荐 |
| SMARTFS | NuttX 原生 | Flash 擦写粒度大 | 备选 |
| NXFFS | NuttX 原生 | 较老、社区少 | 不推荐 |

### 配置步骤

#### 1. 在 defconfig 中添加

```kconfig
# 持久存储：LittleFS on SPI Flash
CONFIG_FS_LITTLEFS=y
CONFIG_ESP32S3_SPIFLASH=y
CONFIG_ESP32S3_PARTITION=y
CONFIG_ESP32S3_PARTITION_CUSTOM=y
```

#### 2. 创建分区表

在板级目录创建 `partitions.csv`：

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x6000
phy_init,   data, phy,     0xf000,   0x1000
factory,    app,  factory, 0x10000,  0x1E0000
storage,    data, littlefs,0x1F0000, 0x10000
```

- `factory` 分区：固件（约 1.9MB）
- `storage` 分区：LittleFS 持久数据（64KB）

#### 3. 启动时挂载

在 `board_boot.c` 或 `nsh_bringup.c` 中添加：

```c
#include <sys/mount.h>

/* Mount LittleFS at /data for persistent storage */
int ret = mount("/dev/esp32s3flash", "/data", "littlefs", 0,
    "autoformat=true");
if (ret < 0) {
    syslog(LOG_ERR, "Failed to mount /data as LittleFS: %d\n", errno);
    /* Fallback to TMPFS */
    mount(NULL, "/data", "tmpfs", 0, NULL);
}
```

#### 4. 验证

```
nsh> ls /data
nsh> echo "test" > /data/test.txt
nsh> cat /data/test.txt
# 重启后
nsh> cat /data/test.txt  # 应该还在
```

## 最小实现（无 Flash 分区修改）

如果当前固件无法修改 Flash 分区表，可以使用以下方案：

### 方案：config_store 内存缓冲 + 定期写入

1. config_store 在内存中维护配置
2. 通过串口命令 `config_save` 手动触发写入
3. 启动时通过 `config_load` 从文件读取

这需要修改 ai_agent 的 config_store 实现，但不涉及 Flash 分区修改。

## 日志持久化

### 滚动日志实现

```c
#define LOG_MAX_SIZE  (128 * 1024)  /* 128KB */
#define LOG_FILE      "/data/agent/error.log"

/* 内存缓冲区，定期刷新到 Flash */
static char log_buffer[4096];
static int log_pos = 0;
static time_t last_flush = 0;

void log_append(const char* msg) {
    int len = strlen(msg);
    if (log_pos + len >= sizeof(log_buffer)) {
        log_flush_to_flash();
    }
    memcpy(log_buffer + log_pos, msg, len);
    log_pos += len;
}

void log_flush_to_flash() {
    if (log_pos == 0) return;

    /* Check file size, rotate if needed */
    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size >= LOG_MAX_SIZE) {
        rename(LOG_FILE, LOG_FILE ".old");
    }

    FILE* fp = fopen(LOG_FILE, "a");
    if (fp) {
        fwrite(log_buffer, 1, log_pos, fp);
        fclose(fp);
    }
    log_pos = 0;
    last_flush = time(NULL);
}
```

### 节流策略

- 内存缓冲 4KB 或每 30 秒刷新一次
- 日志文件上限 128KB，自动轮转
- 只持久化 WARNING 及以上级别
- 串口日志不受限制（开发主日志）

## 新增命令

| 命令 | 说明 |
|------|------|
| `config_show` | 脱敏显示所有配置 |
| `config_reset` | 确认后清除所有配置 |
| `config_save` | 手动保存配置到 Flash |
| `log_show` | 查看持久错误日志 |
| `log_clear` | 确认后清除日志 |

## 内存约束

ESP32-S3-EYE 资源：
- SPI Flash: 4MB（WROOM1N4）或 8MB
- PSRAM: 8MB（Octal SPI）
- SRAM: 512KB

LittleFS 分区大小建议：
- 最小 16KB（仅配置）
- 推荐 64KB（配置 + 少量日志）
- 最大取决于 Flash 剩余空间
