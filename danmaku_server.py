import asyncio
import json
import logging
import ssl
import sys
from pathlib import Path

import websockets
from aiohttp import web
from bilibili_api import live, Credential

from bili_login import get_credential, load_cookie

# ===== 配置 =====
ROOM_ID = 1944729449
WS_PORT = 7485
PLAIN_WS_PORT = 7486
HTTPS_PORT = 19447
HTTP_PORT = 19448

BASE_DIR = Path(__file__).parent
CERT = "cert.pem"
KEY = "key.pem"
# ================

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("danmaku")

connected_clients = set()


async def ws_handler(websocket):
    connected_clients.add(websocket)
    logger.info(f"HTML 客户端已连接，当前 {len(connected_clients)} 个")
    try:
        await websocket.wait_closed()
    finally:
        connected_clients.discard(websocket)
        logger.info(f"HTML 客户端已断开，当前 {len(connected_clients)} 个")


async def broadcast(message: dict):
    if not connected_clients:
        return
    text = json.dumps(message, ensure_ascii=False)
    await asyncio.gather(
        *[client.send(text) for client in connected_clients],
        return_exceptions=True,
    )


async def start_wss_server():
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(str(BASE_DIR / CERT), str(BASE_DIR / KEY))
    async with websockets.serve(
        ws_handler, "0.0.0.0", WS_PORT, ssl=ssl_context
    ):
        logger.info(f"WSS 服务已启动: wss://localhost:{WS_PORT}")
        await asyncio.Future()


async def start_plain_ws_server():
    async with websockets.serve(ws_handler, "0.0.0.0", PLAIN_WS_PORT):
        logger.info(f"WS 服务已启动: ws://localhost:{PLAIN_WS_PORT}")
        await asyncio.Future()


class HttpsServer:
    def __init__(self, port, cert, key, root):
        self.port = port
        self.cert = cert
        self.key = key
        self.root = root
        self.runner = None

    def _ssl_context(self):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(str(self.root / self.cert), str(self.root / self.key))
        return ctx

    async def start(self):
        async def index(request):
            return web.FileResponse(self.root / "index.html")

        app = web.Application()
        app.router.add_get("/", index)
        app.router.add_get("/index.html", index)

        self.runner = web.AppRunner(app)
        await self.runner.setup()
        site = web.TCPSite(
            self.runner, "0.0.0.0", self.port, ssl_context=self._ssl_context()
        )
        await site.start()


async def start_https_server():
    server = HttpsServer(HTTPS_PORT, CERT, KEY, BASE_DIR)
    await server.start()
    logger.info(f"HTTPS 服务已启动: https://localhost:{HTTPS_PORT}/index.html")
    await asyncio.Future()


async def start_http_server():
    async def index(request):
        return web.FileResponse(BASE_DIR / "index.html")

    async def api_login(request):
        # 只允许本机访问
        if request.remote not in ("127.0.0.1", "::1"):
            return web.json_response({"error": "forbidden"}, status=403)

        cookie = load_cookie()
        if (
            cookie
            and cookie.get("SESSDATA")
            and cookie.get("BUVID3")
            and cookie.get("BILI_JCT")
        ):
            return web.json_response(cookie)

        return web.json_response(
            {"error": "not logged in, please scan QR in terminal"},
            status=401,
        )

    app = web.Application()
    app.router.add_get("/", index)
    app.router.add_get("/index.html", index)
    app.router.add_get("/api/login", api_login)

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", HTTP_PORT)
    await site.start()
    logger.info(f"HTTP 服务已启动: http://localhost:{HTTP_PORT}/index.html")
    await asyncio.Future()


async def start_bili_client():
    sessdata, buvid3, bili_jct = await get_credential()
    credential = Credential(
        sessdata=sessdata,
        buvid3=buvid3,
        bili_jct=bili_jct,
    )

    room = live.LiveDanmaku(ROOM_ID, credential=credential)

    @room.on("DANMU_MSG")
    async def on_danmaku(event):
        try:
            info = event["data"]["info"]
            user = info[2][1]
            content = info[1]
            data = {"type": "danmaku", "user": user, "content": content}
            logger.info(f"[弹幕] {user}: {content}")
            await broadcast(data)
        except Exception as e:
            logger.error(f"处理弹幕出错: {e}")

    logger.info(f"正在连接直播间 {ROOM_ID} ...")
    await room.connect()


async def main():
    results = await asyncio.gather(
        start_wss_server(),
        start_plain_ws_server(),
        start_https_server(),
        start_http_server(),
        start_bili_client(),
        return_exceptions=True,
    )
    for i, r in enumerate(results):
        if isinstance(r, Exception):
            logger.error(f"任务 {i} 异常: {r!r}")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("程序已停止")
        sys.exit(0)