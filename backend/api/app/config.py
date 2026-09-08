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

        # 入口转发模式（C1.0）：true 时业务路由经家庭出站通道转发，云端只做入口。
        # 需云端与家庭共用 JWT_SECRET。
        self.RELAY_MODE = os.getenv("RELAY_MODE", "false").lower() in ("1", "true", "yes")
        self.RELAY_TIMEOUT = float(os.getenv("RELAY_TIMEOUT", "20"))

        # 第一版默认绑定设备（与设备侧已验证工具对应）
        # 注意：auth.py 会在用户首次登录时自动绑定该设备，因此"任何人登录即可下发命令"。
        self.DEMO_DEVICE_ID = os.getenv("DEMO_DEVICE_ID", "esp32s3-eye")

        # 演示/审核模式：非受信任用户（如提审审核员）即使已自动绑定演示设备，
        # 也只能执行 DEMO_ALLOWED_ACTIONS，默认不含 mihome.set_power，
        # 避免陌生人通过审核环境控制家庭真实电器。
        self.DEMO_MODE = os.getenv("DEMO_MODE", "false").lower() in ("1", "true", "yes")

        # 受信任用户（拥有完整权限，可控制真实电器），填 user_id，逗号分隔。
        # user_id 获取方式：小程序设置页/Storage 中的 userId，或云端数据库 users 表。
        self.TRUSTED_USER_IDS = [
            u.strip() for u in os.getenv("TRUSTED_USER_IDS", "").split(",") if u.strip()
        ]

        # 演示模式下允许的动作（只读与安全动作为主，禁止真实电器控制）
        self.DEMO_ALLOWED_ACTIONS = [
            a.strip()
            for a in os.getenv(
                "DEMO_ALLOWED_ACTIONS", "led.on,led.off,device.info,mihome.get_state",
            ).split(",")
            if a.strip()
        ]

        # 命令动作白名单（违反即返工：只开放已验证能力）
        self.ALLOWED_ACTIONS = [
            a.strip()
            for a in os.getenv(
                "ALLOWED_ACTIONS",
                "led.on,led.off,device.info,mihome.set_power,mihome.get_state",
            ).split(",")
            if a.strip()
        ]

        # 默认不启用跨域；确有浏览器客户端时再显式配置可信 HTTPS 源。
        self.CORS_ORIGINS = [
            o.strip() for o in os.getenv("CORS_ORIGINS", "").split(",") if o.strip()
        ]

        # 对外基地址（仅用于文档/回显）
        self.API_BASE_URL = os.getenv("API_BASE_URL", "https://api.hfy-ai.cloud")


settings = Settings()
