"""运行时配置（全部来自环境变量，绝不硬编码密钥）。"""
import os


class Settings:
    def __init__(self):
        # 微信小程序（AppSecret 仅来自环境变量，不进仓库/不进聊天记录）
        self.WX_APPID = os.getenv("WX_APPID", "")
        self.WX_APPSECRET = os.getenv("WX_APPSECRET", "")

        # JWT 签名密钥必须显式提供；禁止以占位值或短密钥启动。
        self.JWT_SECRET = os.getenv("JWT_SECRET", "")
        if len(self.JWT_SECRET) < 32 or self.JWT_SECRET == "CHANGE_ME_GENERATE_RANDOM":
            raise RuntimeError("JWT_SECRET must be an explicit random value of at least 32 characters")
        self.JWT_ALG = "HS256"
        self.ACCESS_TOKEN_TTL_MIN = int(os.getenv("ACCESS_TOKEN_TTL_MIN", "120"))
        self.REFRESH_TOKEN_TTL_DAYS = int(os.getenv("REFRESH_TOKEN_TTL_DAYS", "30"))

        # 命令 TTL（秒），到期未执行/未 ACK 由后台扫描置为 expired
        self.COMMAND_TTL_SEC = int(os.getenv("COMMAND_TTL_SEC", "30"))

        # SQLite（验证版）；稳定版改 Postgres 仅需换 DATABASE_URL
        self.SQLITE_PATH = os.getenv("SQLITE_PATH", "./data/homemind.db")

        # 内部 MQTT（docker 网络内明文 1883）；设备/网关走外部 8883 TLS
        self.MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "mqtt")
        self.MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))
        # 家庭侧本地 broker 启用密码认证后使用；为空则匿名（兼容云端内网明文）
        self.MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
        self.MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

        # 第一版默认绑定设备（与设备侧已验证工具对应）
        self.DEMO_DEVICE_ID = os.getenv("DEMO_DEVICE_ID", "esp32s3-eye")

        # 命令动作白名单（违反即返工：只开放已验证能力）
        self.ALLOWED_ACTIONS = [
            a.strip()
            for a in os.getenv("ALLOWED_ACTIONS", "led.on,led.off,device.info").split(",")
            if a.strip()
        ]

        # 默认不启用跨域；确有浏览器客户端时再显式配置可信 HTTPS 源。
        self.CORS_ORIGINS = [
            o.strip() for o in os.getenv("CORS_ORIGINS", "").split(",") if o.strip()
        ]

        # 对外基地址（仅用于文档/回显）
        self.API_BASE_URL = os.getenv("API_BASE_URL", "https://api.hfy-ai.cloud")


settings = Settings()
