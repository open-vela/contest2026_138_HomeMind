"""
日程管理路由
"""

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from typing import List, Optional
from datetime import datetime

router = APIRouter()


class CalendarCreate(BaseModel):
    title: str
    date: str
    time: Optional[str] = None
    description: Optional[str] = None
    reminder: bool = True


class CalendarUpdate(BaseModel):
    title: Optional[str] = None
    date: Optional[str] = None
    time: Optional[str] = None
    description: Optional[str] = None
    reminder: Optional[bool] = None


class CalendarResponse(BaseModel):
    id: str
    title: str
    date: str
    time: Optional[str]
    description: Optional[str]
    reminder: bool
    created_at: str


# 模拟数据库
calendar_db = {}


@router.get("/", response_model=List[CalendarResponse])
async def get_calendars():
    """获取所有日程"""
    return list(calendar_db.values())


@router.get("/{calendar_id}", response_model=CalendarResponse)
async def get_calendar(calendar_id: str):
    """获取单个日程"""
    if calendar_id not in calendar_db:
        raise HTTPException(status_code=404, detail="日程不存在")
    return calendar_db[calendar_id]


@router.post("/", response_model=CalendarResponse)
async def create_calendar(calendar: CalendarCreate):
    """创建日程"""
    calendar_id = f"calendar_{len(calendar_db) + 1}"
    now = datetime.now().isoformat()
    
    new_calendar = {
        "id": calendar_id,
        "title": calendar.title,
        "date": calendar.date,
        "time": calendar.time,
        "description": calendar.description,
        "reminder": calendar.reminder,
        "created_at": now
    }
    
    calendar_db[calendar_id] = new_calendar
    return new_calendar


@router.put("/{calendar_id}", response_model=CalendarResponse)
async def update_calendar(calendar_id: str, calendar: CalendarUpdate):
    """更新日程"""
    if calendar_id not in calendar_db:
        raise HTTPException(status_code=404, detail="日程不存在")
    
    existing_calendar = calendar_db[calendar_id]
    
    if calendar.title is not None:
        existing_calendar["title"] = calendar.title
    if calendar.date is not None:
        existing_calendar["date"] = calendar.date
    if calendar.time is not None:
        existing_calendar["time"] = calendar.time
    if calendar.description is not None:
        existing_calendar["description"] = calendar.description
    if calendar.reminder is not None:
        existing_calendar["reminder"] = calendar.reminder
    
    calendar_db[calendar_id] = existing_calendar
    return existing_calendar


@router.delete("/{calendar_id}")
async def delete_calendar(calendar_id: str):
    """删除日程"""
    if calendar_id not in calendar_db:
        raise HTTPException(status_code=404, detail="日程不存在")
    
    del calendar_db[calendar_id]
    return {"message": "日程已删除"}


@router.post("/reminder")
async def set_reminder(calendar: CalendarCreate):
    """设置提醒"""
    # 这里可以添加实际的提醒逻辑
    # 例如发送通知或设置定时任务
    
    return {"message": "提醒已设置", "calendar": calendar.dict()}


@router.get("/count")
async def get_calendar_count():
    """获取日程数量"""
    return {"count": len(calendar_db)}
