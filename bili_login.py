import asyncio
import json
import logging
import os
from pathlib import Path

import aiohttp
import qrcode

logger = logging.getLogger("login")

COOKIE_FILE = Path(__file__).parent / "cookie.json"
QR_IMG = Path(__file__).parent / "qrcode.png"

GENERATE_URL = "https://passport.bilibili.com/x/passport-login/web/qrcode/generate"
POLL_URL = "https://passport.bilibili.com/x/passport-login/web/qrcode/poll"
FINGER_URL = "https://api.bilibili.com/x/frontend/finger/spi"

TIMEOUT = aiohttp.ClientTimeout(total=15)

HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/127.0.0.0 Safari/537.36"
    ),
    "Referer": "https://www.bilibili.com/",
    "Origin": "https://www.bilibili.com",
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "zh-CN,zh;q=0.9",
}


async def scan_login() -> dict:
    print("[登录] 准备生成二维码...", flush=True)
    jar = aiohttp.CookieJar(unsafe=True)

    async with aiohttp.ClientSession(
        cookie_jar=jar,
        timeout=TIMEOUT,
        headers=HEADERS,
    ) as session:
        async with session.get(GENERATE_URL) as resp:
            print(f"[登录] HTTP 状态: {resp.status}", flush=True)
            gen = await resp.json()

        if gen.get("code") != 0:
            raise RuntimeError(f"生成二维码失败: {gen}")

        qrcode_key = gen["data"]["qrcode_key"]
        qr_url = gen["data"]["url"]

        # 生成 PNG 并自动打开
        img = qrcode.make(qr_url)
        img.save(QR_IMG)
        print(f"\n[登录] 二维码图片已保存: {QR_IMG}", flush=True)
        print("[登录] 正在打开图片，请用 B站 App 扫码...\n", flush=True)
        os.startfile(str(QR_IMG))

        print("[登录] 开始轮询扫码状态...", flush=True)
        while True:
            async with session.get(POLL_URL, params={"qrcode_key": qrcode_key}) as resp:
                poll = await resp.json()
            code = poll["data"]["code"]
            print(f"[登录] 轮询 code={code}", flush=True)

            if code == 0:
                logger.info("扫码登录成功")
                break
            elif code == 86038:
                raise RuntimeError("二维码已过期，请重新运行")
            elif code == 86090:
                logger.info("已扫码，请在手机上确认...")
            await asyncio.sleep(2)

        cookies = {c.key: c.value for c in jar}
        sessdata = cookies.get("SESSDATA", "")
        bili_jct = cookies.get("bili_jct", "")
        dedeuserid = cookies.get("DedeUserID", "")
        print(f"[登录] SESSDATA 前20: {sessdata[:20]}...", flush=True)

        async with session.get(FINGER_URL) as resp:
            finger = await resp.json()
        buvid3 = finger["data"]["b_3"]
        print(f"[登录] buvid3 = {buvid3}", flush=True)

        result = {
            "SESSDATA": sessdata,
            "BUVID3": buvid3,
            "BILI_JCT": bili_jct,
            "DedeUserID": dedeuserid,
        }
        COOKIE_FILE.write_text(
            json.dumps(result, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        logger.info(f"Cookie 已保存到 {COOKIE_FILE}")
        return result


def load_cookie():
    if not COOKIE_FILE.exists():
        return None
    try:
        return json.loads(COOKIE_FILE.read_text(encoding="utf-8"))
    except Exception as e:
        logger.warning(f"读取 cookie.json 失败: {e}")
        return None


async def get_login_info() -> dict:
    """
    返回完整登录信息字典:
    { "SESSDATA": ..., "BUVID3": ..., "BILI_JCT": ..., "DedeUserID": ... }
    """
    cookie = load_cookie()
    if cookie and cookie.get("SESSDATA") and cookie.get("BUVID3") and cookie.get("BILI_JCT"):
        logger.info("已从 cookie.json 加载登录凭证")
        return cookie

    if cookie:
        logger.warning("cookie.json 存在但内容为空，重新扫码")

    logger.info("未找到有效 Cookie，开始扫码登录")
    return await scan_login()

async def get_credential():
    cookie = load_cookie()
    if cookie and cookie.get("SESSDATA") and cookie.get("BUVID3") and cookie.get("BILI_JCT"):
        logger.info("已从 cookie.json 加载登录凭证")
        return cookie["SESSDATA"], cookie["BUVID3"], cookie["BILI_JCT"]

    if cookie:
        logger.warning("cookie.json 存在但内容为空，重新扫码")

    logger.info("未找到有效 Cookie，开始扫码登录")
    cookie = await scan_login()
    return cookie["SESSDATA"], cookie["BUVID3"], cookie["BILI_JCT"]


