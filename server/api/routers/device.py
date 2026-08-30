"""
设备管理路由
"""

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from typing import List, Optional
from datetime import datetime

router = APIRouter()


class DeviceCreate(BaseModel):
    name: str
    type: str
    location: Optional[str] = None


class DeviceUpdate(BaseModel):
    name: Optional[str] = None
    type: Optional[str] = None
    location: Optional[str] = None
    status: Optional[str] = None


class DeviceResponse(BaseModel):
    id: str
    name: str
    type: str
    location: Optional[str]
    status: str
    created_at: str


# 模拟数据库
devices_db = {}


@router.get("/", response_model=List[DeviceResponse])
async def get_devices():
    """获取所有设备"""
    return list(devices_db.values())


@router.get("/{device_id}", response_model=DeviceResponse)
async def get_device(device_id: str):
    """获取单个设备"""
    if device_id not in devices_db:
        raise HTTPException(status_code=404, detail="设备不存在")
    return devices_db[device_id]


@router.post("/", response_model=DeviceResponse)
async def create_device(device: DeviceCreate):
    """创建设备"""
    device_id = f"device_{len(devices_db) + 1}"
    now = datetime.now().isoformat()
    
    new_device = {
        "id": device_id,
        "name": device.name,
        "type": device.type,
        "location": device.location,
        "status": "offline",
        "created_at": now
    }
    
    devices_db[device_id] = new_device
    return new_device


@router.put("/{device_id}", response_model=DeviceResponse)
async def update_device(device_id: str, device: DeviceUpdate):
    """更新设备"""
    if device_id not in devices_db:
        raise HTTPException(status_code=404, detail="设备不存在")
    
    existing_device = devices_db[device_id]
    
    if device.name is not None:
        existing_device["name"] = device.name
    if device.type is not None:
        existing_device["type"] = device.type
    if device.location is not None:
        existing_device["location"] = device.location
    if device.status is not None:
        existing_device["status"] = device.status
    
    devices_db[device_id] = existing_device
    return existing_device


@router.delete("/{device_id}")
async def delete_device(device_id: str):
    """删除设备"""
    if device_id not in devices_db:
        raise HTTPException(status_code=404, detail="设备不存在")
    
    del devices_db[device_id]
    return {"message": "设备已删除"}


@router.post("/{device_id}/control")
async def control_device(device_id: str, control: dict):
    """控制设备"""
    if device_id not in devices_db:
        raise HTTPException(status_code=404, detail="设备不存在")
    
    device = devices_db[device_id]
    
    # 更新设备状态
    if "status" in control:
        device["status"] = control["status"]
    
    # 这里可以添加实际的设备控制逻辑
    # 例如发送 MQTT 消息到设备
    
    return {"message": "控制指令已发送", "device": device}
