# 第三方组件与模型声明

本项目在官方开源组件之上做适配与集成，不代表对第三方代码/模型拥有全部知识产权。

| 组件/模型 | 来源 | 版本/标识 | 许可证 | 用途 |
| --- | --- | --- | --- | --- |
| OpenVela / NuttX | open-vela | contest-2026 工作树 | Apache-2.0 / 上游许可 | 设备 OS 与 ai_agent 运行时 |
| TFLite Micro | Google | 工作树内置 | Apache-2.0 | 端侧混合存在检测 |
| person detect 模型 | 工作树/示例模型 | 见 `SOURCE_SNAPSHOT.json` 与 `artifacts/SHA256SUMS` | 以模型文件与上游声明为准 | 实验性存在检测 |
| KWS 线性模型 | 本项目自训（小样本，未达可用） | `kws_model_data.cc` | 项目代码 Apache-2.0 | 指定词实验（已知缺口） |
| paho-mqtt / FastAPI 等 Python 依赖 | PyPI | 见 `requirements.txt` | 各自上游许可证 | 家庭 API/网关/relay |

未交付或未归集的模型不得虚构来源。完整清单在最终提交前按实际展示功能复核。
