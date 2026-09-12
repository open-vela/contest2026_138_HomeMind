"""最小冒烟测试：健康检查、鉴权拦截、登录参数校验。"""
import os
import sys

# 测试时强制占位密钥，避免依赖真实环境
os.environ.setdefault("JWT_SECRET", "test-only-jwt-secret-32-characters-minimum")
os.environ.setdefault("SQLITE_PATH", "./data/test_homemind.db")

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fastapi.testclient import TestClient
from app.main import app

client = TestClient(app)


def test_health():
    r = client.get("/v1/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_devices_requires_auth():
    r = client.get("/v1/devices")
    assert r.status_code == 401


def test_login_requires_code():
    # 缺 code 字段 -> 422（pydantic）；空 code -> 400（业务校验）
    r1 = client.post("/v1/auth/wechat/login", json={})
    assert r1.status_code == 422
    r2 = client.post("/v1/auth/wechat/login", json={"code": ""})
    assert r2.status_code == 400


def test_commands_requires_auth():
    r = client.post("/v1/devices/x/commands", json={"action": "led.on"})
    assert r.status_code == 401
