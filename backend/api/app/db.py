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


def ensure_sqlite_columns():
    """验证版轻量迁移：create_all 不会给已存在的表加列，这里显式补齐。

    必须在 Base.metadata.create_all 之后调用（首次建库时新表已含全部列）。
    """
    from sqlalchemy import text
    with engine.connect() as conn:
        cols = [row[1] for row in conn.execute(text("PRAGMA table_info(device_status)"))]
        if cols and "mihome_entities" not in cols:
            conn.execute(text(
                "ALTER TABLE device_status ADD COLUMN mihome_entities TEXT DEFAULT ''"))
            conn.commit()


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
