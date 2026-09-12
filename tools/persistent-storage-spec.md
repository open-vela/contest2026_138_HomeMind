# HomeMind 持久存储与日志管理规格

## 1. 持久存储需求

### 1.1 当前问题

当前 `/data` 使用 TMPFS（`CONFIG_FS_TMPFS=y`），掉电即丢失。
`config_store` 的 `/data/agent/config/config.json` 在重启后会丢失所有配置。

### 1.2 解决方案

在 ESP32-S3 的 SPI Flash 上启用 LittleFS 分区，挂载到 `/data`。

#### 需要的 Kconfig 配置

```
# MTD 驱动
CONFIG_MTD=y

# LittleFS
CONFIG_FS_LITTLEFS=y

# ESP32-S3 SPI Flash（用于数据分区）
# 具体配置取决于板级 Flash 分区表
```

#### Flash 分区布局（ESP32-S3-EYE 8MB）

```
| 分区 | 大小 | 用途 |
|------|------|------|
| bootloader | 64KB | 引导程序 |
| firmware | ~1.5MB | nuttx.bin |
| data | ~512KB | LittleFS 配置存储 |
| (剩余) | ~6MB | 未使用/可用 |
```

### 1.3 持久化内容

| 数据 | 存储位置 | 说明 |
|------|----------|------|
| Wi-Fi SSID | config.json | `AGENT_CFG_KEY_WIFI_SSID` |
| Wi-Fi 密码 | config.json | `AGENT_CFG_KEY_WIFI_PASS`（加密存储） |
| LLM 后端 | config.json | LLM preset 名称 |
| LLM API Key | config.json | 加密存储 |
| LLM 模型名 | config.json | 模型标识 |
| 滚动错误日志 | `/data/agent/logs/error.log` | 上限 128KB |

## 2. 日志管理

### 2.1 日志节流策略

- 串口日志：实时输出（开发用）
- Flash 日志：仅记录 WARNING 及以上级别
- 写入节流：内存缓冲 50 条或 5 秒，取先者刷新到 Flash
- 轮转策略：达到 128KB 时删除最旧的 32KB

### 2.2 新增 CLI 命令

#### `config_reset`

```
vela> config_reset
Are you sure? This will erase all saved configuration. (yes/no): yes
[HM-NET] Configuration reset complete. Reboot required.
```

实现：
1. 确认提示（防止误操作）
2. 删除 `/data/agent/config/config.json`
3. 清空内存中的配置
4. 提示需要重启

#### `log_show`

```
vela> log_show
=== HomeMind Error Log (last 128KB) ===
[2026-07-25 10:30:15] [HM-WIFI] DHCP failed after 15s
[2026-07-25 10:30:20] [HM-TLS] Handshake FAILED: ret=-0x004c
...
=== End of Log ===
```

实现：
1. 读取 `/data/agent/logs/error.log`
2. 输出到串口（限制输出量，避免刷屏）
3. 支持 `log_show last N` 查看最后 N 行

#### `log_clear`

```
vela> log_clear
Are you sure? This will erase all saved logs. (yes/no): yes
[HM-NET] Logs cleared.
```

实现：
1. 确认提示
2. 删除 `/data/agent/logs/error.log`
3. 重新创建空文件

## 3. 实现注意事项

### 3.1 Flash 写入节流

```c
#define LOG_FLUSH_INTERVAL_SEC  5
#define LOG_FLUSH_THRESHOLD     50
#define LOG_MAX_FILE_SIZE       (128 * 1024)  /* 128KB */
#define LOG_ROTATE_SIZE         (32 * 1024)   /* 32KB */

static char log_buffer[LOG_FLUSH_THRESHOLD][256];
static int log_count = 0;
static time_t last_flush = 0;

void hm_log_write(const char* msg) {
    /* Add to buffer */
    if (log_count < LOG_FLUSH_THRESHOLD) {
        strncpy(log_buffer[log_count], msg, 255);
        log_count++;
    }
    
    /* Flush if threshold or timer reached */
    time_t now = time(NULL);
    if (log_count >= LOG_FLUSH_THRESHOLD || 
        now - last_flush >= LOG_FLUSH_INTERVAL_SEC) {
        log_flush_to_flash();
    }
}
```

### 3.2 日志轮转

```c
void log_rotate_if_needed(void) {
    struct stat st;
    if (stat(LOG_FILE_PATH, &st) == 0 && st.st_size > LOG_MAX_FILE_SIZE) {
        /* Read last 96KB, write to temp, rename */
        /* Or: truncate from beginning */
    }
}
```

### 3.3 配置脱敏显示

```c
void config_show_masked(void) {
    char key[128];
    agent_config_get(AGENT_CFG_KEY_LLM_API_KEY, key, sizeof(key));
    
    if (strlen(key) > 8) {
        printf("API Key: %.4s****%.4s\n", key, key + strlen(key) - 4);
    } else if (key[0]) {
        printf("API Key: ****\n");
    } else {
        printf("API Key: (not set)\n    }
}
```

## 4. 验收标准

1. ✅ 重启/断电后 Wi-Fi 和 LLM 配置仍在
2. ✅ 日志不会无限增长（上限 128KB）
3. ✅ 密钥不出现在日志和终端回显中
4. ✅ `config_show` 脱敏显示
5. ✅ `config_reset` 确认后清除配置
6. ✅ `log_show` 查看有限的持久错误日志
7. ✅ `log_clear` 确认后清除日志
