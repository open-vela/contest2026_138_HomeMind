from fastapi import APIRouter

router = APIRouter(tags=["health"])


@router.get("/v1/health")
def health():
    return {"status": "ok", "service": "homemind-c1"}
