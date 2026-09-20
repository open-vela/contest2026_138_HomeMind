# HomeMind Web 端（云端官网与运维控制台）

本目录是 HomeMind 公共云侧的 **Web 前端源码**，对外域名 **https://hfy-ai.cloud**（已完成 ICP 备案）。

## 目录

| 路径 | 说明 |
| --- | --- |
| `landing/index.html` | 品牌官网单页（样式全部内联，50 670 B） |
| `landing/hm-app.js` | 交互脚本：hash 路由、滚动显现、Canvas 粒子心、运维控制台（15 971 B） |
| `screenshots/` | 线上实拍截图（官网首页 / 运维控制台 / 接口文档） |

## 线上形态（2026-09-20 核实）

- **托管**：腾讯云主机，单机 Docker Compose —— `nginx:stable`(80/443) + FastAPI(:8000) + `eclipse-mosquitto:2`(1883/8883)
- **静态根**：容器内 `/var/www/demo` ← 宿主机 `/root/hfy-ai-demo/html`
- **路由**：nginx `try_files $uri $uri/ @api` —— 静态文件优先，其余路径回落 FastAPI
- **传输安全**：TLS 1.2 / 1.3，已启用 HSTS（`max-age=31536000; includeSubDomains`）

| 入口 | 内容 |
| --- | --- |
| `https://hfy-ai.cloud/` | 品牌官网（首页 / 产品 / 科技 / 栖心的故事 / 运维控制台 五个板块） |
| `https://hfy-ai.cloud/demo/` | Demo 入口（指向同一静态页） |
| `https://hfy-ai.cloud/docs` | FastAPI 自动生成的 Swagger 接口文档 |
| `https://hfy-ai.cloud/v1/*` | HomeMind C1 API（微信登录 / 设备绑定与指令 / 命令回执 / 媒体上报 / WSS 实时通道） |

## 前端实现

- 原生 HTML / CSS / JavaScript，**无框架、无构建步骤**，静态文件由 nginx 直出，便于在最小规格云主机上长期免维护运行
- hash 路由：`#home`、`#product`、`#tech`、`#story`、`#console`
- 运维控制台为两态视图：登录视图 ↔ 状态视图（服务状态 / 数据存储 / 接入设备三张卡片），token 存放于 `localStorage['hm_console_tk']`

## 已知缺口（如实记录）

控制台前端会请求 `POST /v1/console/login` 与 `GET /v1/console/status`，但云端 API **尚未实现这两个接口**（实测返回 404），因此控制台目前以静态形态呈现，登录与状态拉取未闭环。

云端其余接口均正常：`/v1/health` → 200、`/v1/devices` → 401（鉴权拦截生效，未越权返回数据）、`/docs` → Swagger UI 正常加载。

## 更新前端（在云主机上执行）

```bash
sudo cp landing/index.html landing/hm-app.js /root/hfy-ai-demo/html/
# 静态文件由 nginx 直接读取，无需 reload
```

## 来源与校验

本目录文件取自云主机 `/root/hfy-ai-demo/html/`，与线上直出内容 **MD5 完全一致**：

```
index.html  dcb3406a2b3e718ddceeaea09a237381
hm-app.js   ba7c1d50570a717cdc03529ea5aa2e75
```

（核对时间：2026-09-20）
