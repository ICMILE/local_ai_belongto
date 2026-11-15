# local_ai_belongto

B 站直播弹幕 + 历史记录管理工具集，包含两个独立子项目：

- **bili_danmaku**：B 站直播弹幕实时显示，扫码登录，支持 OBS 透明叠加
- **BiliHistoryDelete**：C++ 桌面工具，批量删除 B 站视频历史

> ⚠️ 本仓库不包含任何账号凭证。`cookie.json`、`*.pem`、`bili_cookies.txt` 均被 `.gitignore` 排除。

---

## 目录结构

```text
local_ai_belongto/
├── .gitignore
├── README.md
├── requirements.txt            # Python 依赖
│
├── danmaku_server.py           # 弹幕主程序：WSS/WS/HTTPS/HTTP + B站客户端
├── bili_login.py               # 扫码登录，生成 cookie.json
├── index.html                  # 弹幕前端页面（透明背景）
├── yt_download.py              # 可选：视频下载工具（yt-dlp）
│
└── BiliHistoryDelete/          # C++ 历史删除工具
    ├── CMakeLists.txt
    ├── src/
    │   ├── main.cpp
    │   ├── utils.h
    │   ├── http_client.h/.cpp
    │   ├── bili_api.h/.cpp
    │   └── selection_server.h/.cpp
    └── main_singlefile_backup.cpp
```

---

## 🎬 bili_danmaku（Python）

### 功能
- **扫码登录**：终端生成二维码图片，手机 B站 App 扫码，Cookie 自动保存到 `cookie.json`
- **弹幕实时推送**：WebSocket 广播，支持多个前端同时连接
- **双协议**：
  - `wss://localhost:7485` → HTTPS 页面用
  - `ws://localhost:7486` → HTTP 页面（OBS）用
- **双静态服务**：
  - `https://localhost:19447/index.html` → 浏览器预览
  - `http://localhost:19448/index.html` → OBS 浏览器源
- **登录信息 API**：`GET http://localhost:19448/api/login`（仅本机可访问）
- **OBS 透明叠加**：白字黑描边，背景透明

### 环境要求
- Windows 10/11
- Python 3.10+
- Conda（推荐）

### 快速开始

```powershell
conda create -n bili-danmaku python=3.11 -y
conda activate bili-danmaku
pip install -r requirements.txt
```

生成 HTTPS 自签名证书（首次）：

```powershell
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/CN=localhost" -config NUL
```

> Windows 上如果 OpenSSL 报找不到配置，先执行 `New-Item -ItemType File openssl.cnf -Force`，再用 `-config openssl.cnf` 指定。

启动：

```powershell
python danmaku_server.py
```

首次启动会生成 `qrcode.png` 并自动打开，手机扫码确认后写入 `cookie.json`。之后启动直接读文件，不再扫码。

### 访问地址

| 用途 | 地址 |
| :--- | :--- |
| 浏览器预览（HTTPS） | `https://localhost:19447/index.html` |
| OBS 浏览器源 | `http://localhost:19448/index.html` |

> OBS 里勾选 **"透明背景"**，宽高推荐 `420 × 800`。

### 配置

在 `danmaku_server.py` 顶部修改：

```python
ROOM_ID = 1944729449  # 你的直播间房间号
WS_PORT = 7485        # WSS 端口
PLAIN_WS_PORT = 7486  # WS 端口
HTTPS_PORT = 19447    # HTTPS 端口
HTTP_PORT = 19448     # HTTP 端口
```

### API

**`GET /api/login`**

返回当前登录信息（仅 `127.0.0.1` / `::1` 可访问）。

- **成功**：
```json
{
  "SESSDATA": "<your_sessdata>",
  "BUVID3": "<your_buvid3>",
  "BILI_JCT": "<your_bili_jct>",
  "DedeUserID": "<your_uid>"
}
```
- **未登录**（HTTP 401）：
```json
{"error": "not logged in, please scan QR in terminal"}
```
- **非本机访问**（HTTP 403）：
```json
{"error": "forbidden"}
```

---

## 🗑️ BiliHistoryDelete（C++）

### 功能
- 从 `/api/login` 自动获取登录信息，无需手动填 cookie
- 分页拉取最近 N 条视频历史（默认最多 300 条，可调）
- 浏览器打开网格页面，点封面选区间：点第 1 张 = 起点 A，点第 2 张 = 终点 B，自动选中 `[A, B]`
- 取消区间模式：点 C、点 D，从已选中移除 `[C, D]`（C=D 取消单张）
- `Ctrl + 点击` 单独切换
- 全选 / 清空
- 点"删除选中"提交给 C++ 程序，逐条调用删除接口
- 多段删除：删完一批可继续在剩余卡片里选下一批，已删卡片标灰不可选
- ESC 退出：终端按 ESC，等当前批次删完后退出

### 依赖
- CMake ≥ 3.16
- C++17 编译器（MSVC 2019+ / MinGW）
- vcpkg 安装：`curl`、`nlohmann-json`、`cpp-httplib`

```powershell
vcpkg install curl:x64-windows nlohmann-json:x64-windows cpp-httplib:x64-windows
```

### 编译

```powershell
cd BiliHistoryDelete
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -A x64
cmake --build . --config Release
```

产物：`build\Release\bili_history_delete.exe`

### 运行

确保 `bili_danmaku` 的 HTTP 服务已在 `http://localhost:19448` 启动：

```powershell
python danmaku_server.py
```

然后另开终端：

```powershell
.\BiliHistoryDelete\build\Release\bili_history_delete.exe
```

终端会打印：

```text
fetching history (up to 300 items)...
fetched 300 items
opening browser: http://127.0.0.1:51234/
press ESC in this console to quit
```

### 代码结构

| 文件 | 职责 |
| :--- | :--- |
| `main.cpp` | 入口：登录 → 拉历史 → 启动选择服务 |
| `utils.h` | `FormatTime`、`GetIntField` |
| `http_client.h/.cpp` | 通用 HTTP 封装（libcurl），不感知 B 站 |
| `bili_api.h/.cpp` | B 站协议：`FetchLoginInfo`、`BuildBiliHeaders`、`FetchAllHistory`、`DeleteOne` |
| `selection_server.h/.cpp` | 本地 HTTP 服务、HTML 生成、批量删除编排、ESC 监听 |

> 分层原则：`http_client` 不知道 B 站，`bili_api` 不知道浏览器，`selection_server` 不知道 curl 细节，`main` 只做串联。

### 使用的 B 站接口

| 功能 | 方法 | 路径 | 参数 |
| :--- | :--- | :--- | :--- |
| 历史列表 | `GET` | `/x/web-interface/history/cursor` | `ps`、`business=archive`、`max`、`view_at` |
| 单条删除 | `POST` | `/x/v2/history/delete` | `kid`（`archive_{aid}`）、`csrf` |
| 清空历史 | `POST` | `/x/v2/history/clear` | `business`、`csrf`（本工具未使用） |

请求头需带 `Cookie`、`Referer: https://www.bilibili.com/history`、`Origin: https://www.bilibili.com`、`User-Agent`。

---

## 常见问题

- **删除返回 `-400`**
  - 确认 `kid` 格式是 `archive_{aid}`，不是纯数字
  - 确认请求头带 `Origin: https://www.bilibili.com`
  - 确认 `csrf` 与 cookie 里的 `bili_jct` 一致
- **删除返回 `-101` / `-111`**
  - `-101`：`SESSDATA` 失效，重新扫码
  - `-111`：CSRF 校验失败，`BILI_JCT` 与 cookie 不一致
- **删除返回 `-412`**
  - 触发风控。程序已对每条删除间隔 500ms、失败重试 1 次；量大时建议分批，每批之间等几分钟。
- **二维码不完整 / 终端显示错乱**
  - `qrcode_terminal` 在窄终端会换行错乱。程序已改为生成 `qrcode.png` 并自动打开图片，扫码扫屏幕上的图片即可。
- **弹幕用户名叫 `t****`**
  - 匿名连接时 B 站会对用户名打码。确保 `cookie.json` 有真实值，日志不应出现 `未提供登录凭据，使用匿名身份连接`。
- **按 ESC 没反应**
  - 先点一下 PowerShell 窗口让它获得焦点，再按 ESC。

---

## 安全须知

- `SESSDATA`、`BILI_JCT` 等同于账号密码，不要截图、发到公开场合、提交到 Git
- `.gitignore` 已排除 `cookie.json`、`bili_cookies.txt`、`*.pem`、`*.key`、`.env` 等
- 上传前建议执行：
```powershell
git status --short
git grep -n "SESSDATA"
git grep -n "bili_jct"
```
确认无敏感值再 push。
- 若曾误提交凭证，除了 `git rm --cached`，还应在 B 站退出登录 → 重新登录刷新 token。

---

## License

仅用于个人学习与本地管理。请遵守 B 站用户协议，不要用于高频请求、爬取或任何违反服务条款的用途。