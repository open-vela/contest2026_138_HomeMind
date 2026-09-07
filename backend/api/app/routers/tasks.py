"""家庭待办/日程：到期时间、家庭时区、提醒信息与完成状态（工作包 B / C1.0）。

模型复用：Task 同时承载待办与日程；状态 pending/done/cancelled。
时区默认 Asia/Shanghai；due_at/remind_at 为本地时间（ISO 字符串），
跨端与重启一致由 SQLite 持久化保证。
"""
import json
import uuid
from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from ..db import SessionLocal
from ..models import Task
from ..security import get_current_user

router = APIRouter(prefix="/v1/tasks", tags=["tasks"])


class TaskReq(BaseModel):
    title: str
    note: str = ""
    due_at: str = ""
    timezone: str = "Asia/Shanghai"
    remind_at: str = ""
    status: str = "pending"


def _parse_dt(v: str):
    if not v:
        return None
    for fmt in ("%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d"):
        try:
            return datetime.strptime(v, fmt)
        except ValueError:
            continue
    return None


def _task_out(t: Task) -> dict:
    return {
        "task_id": t.task_id,
        "title": t.title,
        "note": t.note,
        "due_at": t.due_at.isoformat() if t.due_at else None,
        "timezone": t.timezone,
        "remind_at": t.remind_at.isoformat() if t.remind_at else None,
        "status": t.status,
        "created_at": t.created_at.isoformat() if t.created_at else None,
        "completed_at": t.completed_at.isoformat() if t.completed_at else None,
    }


@router.post("")
def create_task(req: TaskReq, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        if not req.title.strip():
            raise HTTPException(status_code=400, detail="title required")
        due = _parse_dt(req.due_at)
        remind = _parse_dt(req.remind_at)
        if remind and due and remind > due:
            raise HTTPException(status_code=400, detail="remind_at after due_at")
        t = Task(
            task_id=uuid.uuid4().hex, user_id=user_id,
            title=req.title.strip(), note=req.note,
            due_at=due, timezone=req.timezone, remind_at=remind,
            status=req.status if req.status in {"pending", "done"} else "pending")
        db.add(t)
        db.commit()
        db.refresh(t)
        return _task_out(t)
    finally:
        db.close()


@router.get("")
def list_tasks(user_id: str = Depends(get_current_user),
               status: str = "", limit: int = 50):
    db = SessionLocal()
    try:
        q = db.query(Task).filter_by(user_id=user_id)
        if status:
            q = q.filter_by(status=status)
        rows = q.order_by(Task.id.desc()).limit(min(limit, 200)).all()
        return {"tasks": [_task_out(t) for t in rows]}
    finally:
        db.close()


@router.patch("/{task_id}")
def update_task(task_id: str, req: dict, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        t = db.query(Task).filter_by(task_id=task_id, user_id=user_id).first()
        if not t:
            raise HTTPException(status_code=404, detail="task not found")
        status = req.get("status")
        if status:
            if status not in {"pending", "done", "cancelled"}:
                raise HTTPException(status_code=400, detail=f"bad status: {status}")
            t.status = status
            t.completed_at = datetime.utcnow() if status in {"done", "cancelled"} else None
        if "title" in req and req["title"]:
            t.title = req["title"].strip()
        if "note" in req:
            t.note = req["note"]
        due = _parse_dt(req.get("due_at", ""))
        if due:
            t.due_at = due
        db.commit()
        db.refresh(t)
        return _task_out(t)
    finally:
        db.close()


@router.get("/{task_id}")
def get_task(task_id: str, user_id: str = Depends(get_current_user)):
    db = SessionLocal()
    try:
        t = db.query(Task).filter_by(task_id=task_id, user_id=user_id).first()
        if not t:
            raise HTTPException(status_code=404, detail="task not found")
        return _task_out(t)
    finally:
        db.close()
