import asyncio
import logging
from contextlib import asynccontextmanager
from datetime import datetime
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import settings
from .db import engine, SessionLocal, ensure_sqlite_columns
from . import models
from .mqtt_client import start_mqtt
from .ws_hub import init as ws_init, drain
from .models import Command
from .routers import health, auth, devices, commands, media, ws as ws_router

logging.basicConfig(level=logging.INFO)
# httpx/httpcore 在 INFO 级会打印完整请求 URL，而微信 jscode2session 把 AppSecret
# 放在 query string 里（?...&secret=xxx）——必须压到 WARNING，否则 AppSecret 明文落日志。
logging.getLogger("httpx").setLevel(logging.WARNING)
logging.getLogger("httpcore").setLevel(logging.WARNING)
logging.getLogger("urllib3").setLevel(logging.WARNING)
logger = logging.getLogger("api")

models.Base.metadata.create_all(bind=engine)
ensure_sqlite_columns()


async def expire_sweeper():
    """命令到期仍未 done 则置 expired；云端绝不把 queued 当作已执行。"""
    while True:
        await asyncio.sleep(5)
        db = SessionLocal()
        try:
            now = datetime.utcnow()
            rows = db.query(Command).filter(Command.status.in_(["queued", "acked"])).all()
            changed = False
            for c in rows:
                if c.created_at and (now - c.created_at).total_seconds() > c.ttl:
                    c.status = "expired"
                    changed = True
            if changed:
                db.commit()
        except Exception as e:
            logger.error("expire_sweeper error: %s", e)
        finally:
            db.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    ws_init(asyncio.get_event_loop())
    asyncio.create_task(drain())
    asyncio.create_task(expire_sweeper())
    start_mqtt()  # 在独立线程跑 MQTT 循环
    yield


app = FastAPI(title="HomeMind C1 API", version="1.0.0", lifespan=lifespan)
if settings.CORS_ORIGINS:
    app.add_middleware(
        CORSMiddleware,
        allow_origins=settings.CORS_ORIGINS,
        allow_credentials=True,
        allow_methods=["GET", "POST", "DELETE"],
        allow_headers=["Authorization", "Content-Type"],
    )
app.include_router(health.router)
app.include_router(auth.router)
app.include_router(devices.router)
app.include_router(commands.router)
app.include_router(media.router)
app.include_router(ws_router.router)
