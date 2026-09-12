"""
AI 服务路由
"""

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from typing import List, Optional
import openai
import os

router = APIRouter()


class ChatRequest(BaseModel):
    message: str
    context: Optional[str] = None


class ChatResponse(BaseModel):
    response: str
    model: str


class TaskRequest(BaseModel):
    task: str
    parameters: Optional[dict] = None


class TaskResponse(BaseModel):
    result: str
    status: str


# MiMo API 配置
MIMO_API_KEY = os.getenv("MIMO_API_KEY", "")
MIMO_BASE_URL = "https://api.mimo.xiaomi.com/v1"


@router.post("/chat", response_model=ChatResponse)
async def chat(request: ChatRequest):
    """AI 对话"""
    try:
        # 使用 MiMo API
        client = openai.OpenAI(
            api_key=MIMO_API_KEY,
            base_url=MIMO_BASE_URL
        )
        
        messages = [
            {"role": "system", "content": "你是 HomeMind 家庭智能助手，帮助用户管理智能家居设备和日程。"},
            {"role": "user", "content": request.message}
        ]
        
        if request.context:
            messages.insert(1, {"role": "assistant", "content": request.context})
        
        response = client.chat.completions.create(
            model="mimo-v2.5",
            messages=messages,
            max_tokens=1000
        )
        
        return ChatResponse(
            response=response.choices[0].message.content,
            model="mimo-v2.5"
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"AI 服务错误: {str(e)}")


@router.post("/task", response_model=TaskResponse)
async def process_task(request: TaskRequest):
    """处理任务"""
    try:
        # 使用 MiMo API 处理任务
        client = openai.OpenAI(
            api_key=MIMO_API_KEY,
            base_url=MIMO_BASE_URL
        )
        
        prompt = f"""
        请分析以下任务并返回执行计划：
        任务：{request.task}
        参数：{request.parameters}
        
        请返回 JSON 格式的执行计划。
        """
        
        response = client.chat.completions.create(
            model="mimo-v2.5",
            messages=[
                {"role": "system", "content": "你是任务规划助手，分析用户任务并生成执行计划。"},
                {"role": "user", "content": prompt}
            ],
            max_tokens=500
        )
        
        return TaskResponse(
            result=response.choices[0].message.content,
            status="completed"
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"任务处理错误: {str(e)}")


@router.post("/analyze")
async def analyze_data(data: dict):
    """分析数据"""
    try:
        # 使用 MiMo API 分析数据
        client = openai.OpenAI(
            api_key=MIMO_API_KEY,
            base_url=MIMO_BASE_URL
        )
        
        prompt = f"""
        请分析以下数据并给出建议：
        数据：{data}
        
        请提供分析结果和改进建议。
        """
        
        response = client.chat.completions.create(
            model="mimo-v2.5",
            messages=[
                {"role": "system", "content": "你是数据分析助手，帮助用户分析智能家居数据。"},
                {"role": "user", "content": prompt}
            ],
            max_tokens=500
        )
        
        return {
            "analysis": response.choices[0].message.content,
            "status": "success"
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"数据分析错误: {str(e)}")
