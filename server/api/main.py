"""
HomeMind 私有云服务 - FastAPI 主应用
"""

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from contextlib import asynccontextmanager
import os
import uvicorn

from routers import device, calendar, ai
from services.ai_service import AIService
from services.device_service import DeviceService
from services.calendar_service import CalendarService


# 全局服务实例
ai_service = AIService()
device_service = DeviceService()
calendar_service = CalendarService()


@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用生命周期管理"""
    # 启动时初始化
    print("HomeMind 服务启动中...")
    await ai_service.initialize()
    await device_service.initialize()
    await calendar_service.initialize()
    print("HomeMind 服务启动完成")
    
    yield
    
    # 关闭时清理
    print("HomeMind 服务关闭中...")
    await ai_service.cleanup()
    await device_service.cleanup()
    await calendar_service.cleanup()
    print("HomeMind 服务已关闭")


# 创建 FastAPI 应用
app = FastAPI(
    title="HomeMind API",
    description="HomeMind 家庭智能助手私有云服务",
    version="1.0.0",
    lifespan=lifespan
)

# 旧版骨架默认禁用跨域；确有浏览器客户端时显式配置可信源。
cors_origins = [
    origin.strip()
    for origin in os.getenv("CORS_ORIGINS", "").split(",")
    if origin.strip()
]
if cors_origins:
    app.add_middleware(
        CORSMiddleware,
        allow_origins=cors_origins,
        allow_credentials=True,
        allow_methods=["GET", "POST"],
        allow_headers=["Authorization", "Content-Type"],
    )

# 注册路由
app.include_router(device.router, prefix="/api/device", tags=["设备管理"])
app.include_router(calendar.router, prefix="/api/calendar", tags=["日程管理"])
app.include_router(ai.router, prefix="/api/ai", tags=["AI 服务"])


@app.get("/")
async def root():
    """根路径"""
    return {
        "message": "HomeMind API 服务",
        "version": "1.0.0",
        "status": "running"
    }


@app.get("/api/status")
async def get_status():
    """获取系统状态"""
    return {
        "status": "online",
        "temperature": 25,
        "humidity": 60,
        "devices_count": await device_service.get_device_count(),
        "calendar_count": await calendar_service.get_calendar_count()
    }


@app.get("/api/health")
async def health_check():
    """健康检查"""
    return {"status": "healthy"}


if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8000,
        reload=False,
        log_level="info"
    )
