"""数据库会话（SQLite 验证版，SQLAlchemy 同步）。"""
import os
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker, declarative_base

SQLITE_PATH = os.getenv("SQLITE_PATH", "./data/homemind.db")
DATABASE_URL = f"sqlite:///{SQLITE_PATH}"

# 确保数据库文件所在目录存在
_dir = os.path.dirname(SQLITE_PATH)
if _dir:
    os.makedirs(_dir, exist_ok=True)

engine = create_engine(DATABASE_URL, connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)
Base = declarative_base()


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
