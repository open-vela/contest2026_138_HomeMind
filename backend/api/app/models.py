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
    # 网关上报的米家实体快照：[{entity_id, name, state}] 的 JSON 字符串
    mihome_entities = Column(Text, default="")
    last_seen = Column(DateTime, default=utcnow)
    updated_at = Column(DateTime, default=utcnow)


class Intent(Base):
    """家庭意图：语义理解结果记录（工作包 B / C1.0）。"""
    __tablename__ = "intents"
    id = Column(Integer, primary_key=True)
    intent_id = Column(String(36), unique=True, index=True)
    user_id = Column(String(36), index=True)
    text = Column(Text, default="")
    intent_type = Column(String(32), default="")       # 如 task.create / device.control
    action = Column(String(32), default="")
    entities = Column(Text, default="{}")              # JSON
    params = Column(Text, default="{}")                # JSON
    status = Column(String(16), default="pending")     # pending/handled/rejected
    device_id = Column(String(64), default="")
    reason = Column(Text, default="")                  # 拒绝原因等
    created_at = Column(DateTime, default=utcnow)
    handled_at = Column(DateTime, nullable=True)


class HomeEvent(Base):
    """家庭感知事件：时间/来源设备/事件类型与载荷（工作包 B）。"""
    __tablename__ = "home_events"
    id = Column(Integer, primary_key=True)
    event_id = Column(String(36), unique=True, index=True)
    device_id = Column(String(64), index=True)
    source = Column(String(32), default="")            # vision/voice/sensor/mqtt/manual
    event_type = Column(String(48), index=True)        # person_detected / motion / wake_word ...
    payload = Column(Text, default="{}")               # JSON
    created_at = Column(DateTime, default=utcnow, index=True)


class Task(Base):
    """家庭待办/日程：到期时间、家庭时区、提醒与完成状态（工作包 B）。"""
    __tablename__ = "tasks"
    id = Column(Integer, primary_key=True)
    task_id = Column(String(36), unique=True, index=True)
    user_id = Column(String(36), index=True)
    title = Column(String(256), default="")
    note = Column(Text, default="")
    due_at = Column(DateTime, nullable=True)
    timezone = Column(String(32), default="Asia/Shanghai")
    remind_at = Column(DateTime, nullable=True)
    status = Column(String(16), default="pending")     # pending/done/cancelled
    created_at = Column(DateTime, default=utcnow)
    completed_at = Column(DateTime, nullable=True)


class Asset(Base):
    """家庭资产：记录与授权查询/更新（工作包 B；不整体发送 MiMo）。"""
    __tablename__ = "assets"
    id = Column(Integer, primary_key=True)
    asset_id = Column(String(36), unique=True, index=True)
    name = Column(String(128), default="")
    category = Column(String(64), default="")
    location = Column(String(128), default="")
    attributes = Column(Text, default="{}")            # JSON
    owner = Column(String(36), default="")
    created_at = Column(DateTime, default=utcnow)
    updated_at = Column(DateTime, default=utcnow)
