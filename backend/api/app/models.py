"""ORM 模型：用户 / 设备绑定 / 命令状态机 / 设备状态。"""
from datetime import datetime
from sqlalchemy import (
    Column, String, Integer, DateTime, Text, Boolean, ForeignKey, UniqueConstraint,
)
from .db import Base


def utcnow():
    return datetime.utcnow()


class User(Base):
    __tablename__ = "users"
    id = Column(Integer, primary_key=True)
    user_id = Column(String(36), unique=True, index=True)  # 内部 UUID，下发给小程序
    openid = Column(String(64), unique=True, index=True)    # 微信 openid（不对外暴露）
    created_at = Column(DateTime, default=utcnow)


class DeviceBinding(Base):
    __tablename__ = "device_bindings"
    id = Column(Integer, primary_key=True)
    user_id = Column(String(36), index=True)
    device_id = Column(String(64), index=True)
    name = Column(String(128), default="")
    created_at = Column(DateTime, default=utcnow)
    __table_args__ = (
        UniqueConstraint("user_id", "device_id", name="uq_user_device"),
    )


class Command(Base):
    """命令状态机：queued -> acked -> done / expired。"""
    __tablename__ = "commands"
    command_id = Column(String(36), primary_key=True)
    user_id = Column(String(36), index=True)
    device_id = Column(String(64), index=True)
    action = Column(String(32))
    params = Column(Text, default="{}")          # JSON 字符串
    ttl = Column(Integer, default=30)            # 秒
    status = Column(String(16), default="queued")  # queued/acked/done/expired
    created_at = Column(DateTime, default=utcnow)
    acked_at = Column(DateTime, nullable=True)
    done_at = Column(DateTime, nullable=True)


class DeviceStatus(Base):
    __tablename__ = "device_status"
    device_id = Column(String(64), primary_key=True)
    online = Column(Boolean, default=False)
    led_state = Column(String(16), default="unknown")  # on/off/unknown
    last_seen = Column(DateTime, default=utcnow)
    updated_at = Column(DateTime, default=utcnow)
