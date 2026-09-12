# HomeMind Ubuntu / Win11 双机开发工作流

## 1. 分工

| 平台 | 负责内容 | 不负责内容 |
| --- | --- | --- |
| Ubuntu 22.04 | OpenVela、NuttX、ai_agent、固件、串口、网络调试 | UG 外壳、小程序页面开发 |
| Win11 | UG 外壳、STL、`miniprogram/` | OpenVela 编译、固件源码修改 |

Ubuntu 是固件的唯一事实来源，Win11 是小程序和机械设计的唯一事实来源。

## 2. 建议分支

Ubuntu：

```bash
git switch -c feature/firmware-wifi
```

Win11：

```powershell
git switch -c feature/miniprogram-ui
```

原则：

- 一次提交只解决一个明确问题；
- 不在两端同时修改 README、STATUS、manifest 等共享文件；
- 共享文件先在 Ubuntu 修改并提交，Win11 拉取后再继续；
- 每次构建记录项目 commit 和上游 `packages/ai_agent`、`nuttx` commit；
- 二进制产物只保留明确的验收版本。

## 3. 日常 Ubuntu 流程

```bash
cd ~/work/openvela/contest2026_138_HomeMind
git pull --ff-only
git status --short

source /实际路径/esp-idf/export.sh
OPENVELA_ROOT=$HOME/work/openvela JOBS=2 ./scripts/build.sh build
sha256sum -c artifacts/SHA256SUMS
```

验证后：

```bash
git add -- <明确文件>
git commit -m "fix: improve wifi association diagnostics"
git push -u origin feature/firmware-wifi
```

不要提交真实凭据、原始敏感串口日志、`.venv/` 或 OpenVela 全量构建目录。

## 4. 日常 Win11 小程序流程

在有效的比赛仓 clone 中：

```powershell
git pull --ff-only
git status --short
```

只修改 `miniprogram/`。当前小程序仍有语法损坏和缺失图标，应在固件 Wi-Fi 稳定后再集中修复。修改完成后使用独立分支提交，避免覆盖 Ubuntu 固件分支。

## 5. UG 外壳流程

UG 源文件保留在工作区上级 `外壳/`：

- 先修改 `.prt`；
- 再导出对应 `.STL`；
- 记录日期和结构变化；
- 运行 `tools/create-win11-backup.ps1` 生成带哈希备份。

UG 二进制文件不适合普通 Git 文本合并。如果需要入仓，先确认比赛提交方式，再使用 Git LFS 或最终附件，避免频繁提交每个中间版本。

## 6. 串口和测试记录

原始串口日志可以保存在 Win11 工作区 `esp_log/` 或 Ubuntu 本地非仓库目录。提交前：

1. 移除 Wi-Fi 密码、Token 和 Authorization Header；
2. 保留固件哈希、commit、测试时间和实际结果；
3. 区分“预期结果”和“实测结果”；
4. 只把脱敏摘要加入项目文档。

## 7. 当前开发顺序

```text
Ubuntu 环境/有效 Git
  → 官方固件基线
  → HomeMind 脱敏诊断固件
  → Wi-Fi 关联
  → DHCP/IPv4
  → DNS/TCP/TLS
  → MiMo
  → 持久化
  → 本地工具
  → Win11 小程序联动展示
  → 外壳与最终提交
```

任何后续阶段都不能掩盖前一阶段没有通过的事实。
