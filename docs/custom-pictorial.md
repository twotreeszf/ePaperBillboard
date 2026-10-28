# 自定义画报

这篇文档给要接入画报页的第三方服务。设备固件已经实现客户端。第三方提供一个 HTTPS 地址，返回一张 TTI1 图片。设备主人在局域网里的 Web 设置中打开开关、填写地址和 Token。

内置漫画系列和这条扩展互斥。开关打开后，画报页只请求填写的地址。

## 设备上怎么打开

1. 手机或电脑和设备连同一个 Wi-Fi。
2. 设备上进入 **设置 → Web 设置**。
3. 打开页面上的 **画报** 标签。
4. 勾选「打开自定义画报」，填写请求 URL 和 Token，保存。

热点里的「联网设置」只有 Wi-Fi 和时区，没有画报标签。地址和 Token 保存在设备上，重启后仍然有效。Token 会回填到这个表单里，日志里只出现长度，不出现原文。

保存规则：

| 开关 | URL | Token | 结果 |
|------|-----|-------|------|
| 开 | 合法 `https://` 地址 | 1–63 字符，不含回车换行 | 保存成功 |
| 开 | 空、非 https、含空格或控制字符、超长 | 任意 | 整次保存失败，提示「请填写 https 开头的画报地址」 |
| 开 | 合法 | 空、含回车换行或达到 64 字符 | 整次保存失败，提示「请填写画报 Token」 |
| 关 | 空 | 空 | 清除自定义画报，回到内置系列 |
| 关 | 非空但不合法 | 任意 | 整次保存失败，提示「画报地址无效」 |
| 关 | 任意 | 非空但不合法 | 整次保存失败，提示「画报 Token 无效」 |

URL 和 Token 的检查失败时，同一次提交里的天气、日历不会被改写。

字段限制：

| 项目 | 限制 |
|------|------|
| URL | 最多 239 个字符 |
| 主机名 | 最多 63 个字符，不含 `https://`、端口和路径 |
| 路径 | 最多 767 个字符，从第一个 `/` 到末尾，含查询串 |
| Token | 1–63 个字符 |

主机名用域名或 IPv4。不要写 IPv6，解析会把冒号当成端口。路径里不要出现空格；空格以及 `<= 0x20` 和 `0x7F` 的字符都会被拒绝。

整条 HTTP 请求必须短于 896 字节。请求行、`Host`、`Connection`、`Token`、`Timestamp` 都算在里面。主机名、路径和 Token 的长度之和不超过 819 时一定放得下。表单能保存的最长 URL 再配上最长 Token，有可能在真正请求时被判为过长，串口日志是 `HTTPS: request too long`。给集成用的地址保持在 200 字符以内。

## 请求

每次真正拉取都是一次 GET，没有请求体，也不跟随重定向。

```http
GET /pictorial?user=zhangjun HTTP/1.0
Host: example.com
Connection: close
Token: <设备里填写的 Token>
Timestamp: 1793169545

```

约定：

- 方法固定是 GET。Token 和时间戳只放在头里，不会附加到查询串。查询串可以写在配置的 URL 里，设备会原样放进请求行。
- `Token` 的值就是设置里的原文，没有 `Bearer` 前缀。
- `Timestamp` 是 Unix 秒，十进制，UTC。不是毫秒，也不是本地时区的字符串。设备时间未同步时不会发请求，画面提示「时间未同步」。
- `Host` 只有主机名，自定义端口不会写进 Host 头。连接端口来自 URL，省略时是 443。
- 没有 `User-Agent`、Cookie 或 `Authorization`。
- 上下拨动发出的是同一种请求。设备不传页码，也不说明这一次是向上还是向下。下一张图由服务端决定。
- 失败不会自动重试。

服务端用 Token 识别调用方。设备不会对请求签名，只靠这个共享密钥。建议同时检查 `Timestamp` 与服务器时间的差，例如 5 分钟，用来丢掉过期重放。密钥比对不要用会提前返回的普通字符串比较。

校验失败时返回 200 以外的状态码即可。设备不解析错误正文，画面上统一是「画报下载失败」。若屏幕上已经有上一张成功的图，失败后仍留着那一张。

## 响应

成功时：

- 状态码 `200`。
- `Connection: close`。
- 正文是完整的 TTI1 文件，长度 1–32768 字节。
- 不要用分块传输（`Transfer-Encoding: chunked`）。设备把空行之后的全部字节当成图片。
- 不要 301 / 302。设备不跟随重定向。
- `Content-Length` 可以带。设备按连接结束来切分正文，不依赖这个头。

正文短于图片头里声明的行数据时，画面会画不完整。正文长度应当正好是 `8 + ceil(width / 8) * height`。

## 什么时候会请求

| 时机 | 行为 |
|------|------|
| 打开画报页，且上次成功的图也是自定义画报 | 直接显示缓存，不发请求 |
| 打开画报页，缓存缺失，或缓存来自内置系列 | 立刻 GET |
| 在画报页向上或向下拨动 | 立刻再 GET，时间戳更新 |
| 画报页停在前台，设备在整点后第 1 秒醒来 | 再 GET |

整点更新只在画报页仍是当前页面时发生。人离开画报页之后，下一次打开会先显示缓存。本地时间 22:00 至次日 06:00 不安排这次更新，下一次落在 06:00:01。

请求过程中画面出现「正在更新」。成功后左侧仍是设备自己画的天气、日期和时钟，右侧是返回的图片。图片在右侧区域内居中。

屏幕是 400×300。左侧栏宽 120，底栏占用 18，右侧可画区域是 **280×282**。请返回 **280×280** 的图，放进去几乎贴齐。更小的图会居中，四周留白。允许的最大尺寸是 400×300，超过这个范围会被拒。宽于 280 的图会伸进左侧栏或超出屏幕，不要用。

长按中间键不会打开内置系列列表。上下拨动仍然会重新请求。

画面上的失败文案：

| 文案 | 原因 |
|------|------|
| 未配置自定义画报 | 开关开着，但地址或 Token 读出来不合法 |
| 时间未同步 | 时钟还不可用，尚未发请求 |
| 画报下载失败 | 连接失败、状态码不是 200、正文为空，或正文不是合法 TTI1 |
| 自定义画报不能切换系列 | 自定义模式下收到了切换系列的请求 |

串口上可以对照这些日志：

```text
Pictorial: custom GET https://example.com/pictorial token_len=14 ts=1793169545
HTTPS: status=200 body_len=9808
Pictorial page: show ... (自定义)
Pictorial: custom HTTP 401 body=0
HTTPS: handshake failed
HTTPS: DNS fail host=example.com
HTTPS: request too long
Pictorial page: custom mode ignores series picker
```

`custom HTTP` 只在失败时打印。成功时先有 `HTTPS: status=200`，随后是 `show ... (自定义)`。

## TTI1

文件是未压缩的 1 位图。

```text
偏移  长度  含义
0     4     ASCII "TTI1"
4     2     宽度，uint16，小端
6     2     高度，uint16，小端
8     …     按行排列的位图
```

每行 `ceil(width / 8)` 字节，行尾不足 8 像素的位留在该字节的低位，读的时候忽略。一行之内从左到右，字节内高位是左边的像素。

- 位值 `1`：白
- 位值 `0`：墨

280×280 的文件是 8 + 35 × 280 = **9808** 字节。生成时先把位图填成 `0xFF`（全白），再把墨点的对应位清掉。

下面的脚本可以单独保存为 `make_pictorial.py`。它依赖 Pillow（`pip install pillow`）。每个参数是一行字；只给一个参数时，会按宽度自动折行。同时会写出一张同名 PNG，用来在浏览器里预览。

```bash
python make_pictorial.py "给张军的" "扩展功能" -o pictorial.i1
python make_pictorial.py "给张军的扩展功能" -o pictorial.i1
python make_pictorial.py --image photo.png -o pictorial.i1
python make_pictorial.py "给张军的" "扩展功能" --font /path/to/font.ttf -o pictorial.i1
```

找不到中文字体时，用 `--font` 指定一个包含这些字的 TTF、OTF 或 TTC。

```python
#!/usr/bin/env python3
"""Build a 280x280 TTI1 pictorial.

Each argument is one line. A single argument is wrapped to the pane width.

    python make_pictorial.py "给张军的" "扩展功能" -o pictorial.i1
    python make_pictorial.py "给张军的扩展功能" -o pictorial.i1
    python make_pictorial.py --image photo.png -o pictorial.i1
"""

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH = 280
HEIGHT = 280
MARGIN = 16
THRESHOLD = 160


def pack_tti1(image):
    """Pack a grayscale image. Pixels darker than THRESHOLD become ink."""
    image = image.convert("L")
    width, height = image.size
    if not (1 <= width <= 400 and 1 <= height <= 300):
        raise ValueError(f"size {width}x{height} is outside 1..400 by 1..300")
    stride = (width + 7) // 8
    body = bytearray(b"\xFF" * (stride * height))
    pixels = image.tobytes()
    for y in range(height):
        row = y * width
        for x in range(width):
            if pixels[row + x] >= THRESHOLD:
                continue
            body[y * stride + (x >> 3)] &= ~(1 << (7 - (x & 7)))
    return b"TTI1" + struct.pack("<HH", width, height) + body


def wrap_line(draw, text, font, max_width):
    lines = []
    rest = text.strip()
    while rest:
        fit = 1
        for count in range(1, len(rest) + 1):
            if draw.textlength(rest[:count], font=font) <= max_width:
                fit = count
            else:
                break
        if draw.textlength(rest[:1], font=font) > max_width:
            raise SystemExit("font is too large for the pane")
        lines.append(rest[:fit])
        rest = rest[fit:].strip()
    return lines


def find_font(explicit):
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise SystemExit(f"font not found: {path}")
        return path
    candidates = [
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
    ]
    for item in candidates:
        path = Path(item)
        if path.is_file():
            return path
    raise SystemExit("pass --font with a TTF, OTF, or TTC that contains the characters")


def render_text(lines, font_path):
    lines = [line.strip() for line in lines if line.strip()]
    if not lines:
        raise SystemExit("no text")
    probe = Image.new("L", (WIDTH, HEIGHT), 255)
    draw = ImageDraw.Draw(probe)
    max_width = WIDTH - MARGIN * 2
    max_height = HEIGHT - MARGIN * 2
    chosen = None
    for size in range(120, 12, -1):
        font = ImageFont.truetype(str(font_path), size)
        wrapped = []
        for line in lines:
            wrapped.extend(wrap_line(draw, line, font, max_width))
        boxes = [draw.textbbox((0, 0), line, font=font) for line in wrapped]
        widths = [box[2] - box[0] for box in boxes]
        heights = [box[3] - box[1] for box in boxes]
        gap = max(4, size // 6)
        ink_h = sum(heights) + gap * (len(wrapped) - 1)
        if widths and max(widths) <= max_width and ink_h <= max_height:
            chosen = (font, wrapped, boxes, widths, heights, gap, ink_h)
            break
    if chosen is None:
        raise SystemExit("text does not fit in 280x280")
    font, wrapped, boxes, widths, heights, gap, ink_h = chosen
    page = Image.new("L", (WIDTH, HEIGHT), 255)
    draw = ImageDraw.Draw(page)
    y = (HEIGHT - ink_h) // 2
    for line, box, width, height in zip(wrapped, boxes, widths, heights):
        x = (WIDTH - width) // 2 - box[0]
        draw.text((x, y - box[1]), line, font=font, fill=0)
        y += height + gap
    return page


def fit_image(path):
    source = Image.open(path).convert("L")
    source.thumbnail((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    page = Image.new("L", (WIDTH, HEIGHT), 255)
    page.paste(source, ((WIDTH - source.width) // 2, (HEIGHT - source.height) // 2))
    return page.point(lambda pixel: 0 if pixel < THRESHOLD else 255)


def main():
    parser = argparse.ArgumentParser(description="Build a 280x280 TTI1 pictorial.")
    parser.add_argument("lines", nargs="*", help="One argument per line. One argument is wrapped.")
    parser.add_argument("--image", help="Fit this picture into the pane instead of drawing text.")
    parser.add_argument("-o", "--output", default="pictorial.i1", help="Output .i1 path.")
    parser.add_argument("--font", help="TTF, OTF, or TTC. Required when no system CJK font is found.")
    args = parser.parse_args()
    if args.image and args.lines:
        raise SystemExit("pass text or --image, not both")
    if args.image:
        page = fit_image(args.image)
    else:
        page = render_text(args.lines, find_font(args.font))
    payload = pack_tti1(page)
    output = Path(args.output)
    output.write_bytes(payload)
    page.convert("1").save(output.with_suffix(".png"))
    width, height = struct.unpack_from("<HH", payload, 4)
    print(f"wrote {output} {width}x{height} {len(payload)} bytes")
    print(f"wrote {output.with_suffix('.png')}")


if __name__ == "__main__":
    main()
```

## TLS

设备使用自己的 TLS 1.2 客户端，证书链和主机名都不校验。服务端仍然必须谈成下面的套件，否则日志是 `HTTPS: handshake failed`，画面是「画报下载失败」。

| 项目 | 要求 |
|------|------|
| 版本 | TLS 1.2。只开 TLS 1.3 的入口会失败 |
| 套件 | `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`（`0xC02F`）或 `TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`（`0xC02B`） |
| 密钥交换 | X25519（`0x001D`） |
| 签名 | `rsa_pkcs1_sha256`、`ecdsa_secp256r1_sha256` 或 `rsa_pss_rsae_sha256` |
| 证书 | RSA 2048 或 ECDSA P-256。ServerKeyExchange 正文不能超过 320 字节，RSA 4096 的签名放不下 |
| 客户端证书 | 不发送，也不接受 CertificateRequest 之后的额外往返要求 |
| 超时 | 握手和读写各 15 秒 |
| 名字 | 会发 SNI，内容就是 URL 里的主机 |

公网证书可以用。自签证书也可以，只要套件匹配。DNS 由设备解析；主机名必须能从设备所在的网络查到地址。

## 服务端最小实现

下面的服务用 TLS 1.2 和 `ECDHE-RSA-AES128-GCM-SHA256` 提供一张固定的 280×280 图。`pictorial.i1` 用上一节的脚本生成。证书用 RSA 2048。Token 比对要换成服务端自己的密钥。

```python
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

IMAGE = Path("pictorial.i1").read_bytes()
TOKEN = "replace-with-shared-secret"

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        if self.headers.get("Token", "") != TOKEN:
            self.send_error(401)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(IMAGE)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(IMAGE)

    def log_message(self, fmt, *args):
        return

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_2
ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.set_ciphers("ECDHE-RSA-AES128-GCM-SHA256")
ctx.load_cert_chain("cert.pem", "key.pem")

httpd = ThreadingHTTPServer(("0.0.0.0", 443), Handler)
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
httpd.serve_forever()
```

证书：

```bash
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes \
  -subj "/CN=example.com"
```

本地试通可以先用：

```bash
curl --tlsv1.2 --ciphers ECDHE-RSA-AES128-GCM-SHA256 -k \
  -D - -o out.i1 \
  -H "Token: replace-with-shared-secret" \
  -H "Timestamp: 1793169545" \
  "https://127.0.0.1/pictorial"
```

`out.i1` 的前 4 字节应是 `TTI1`，长度应等于 `8 + ceil(宽 / 8) * 高`。

## 接入检查

1. 地址以 `https://` 开头，主机名短于 64 字节，整条 URL 短于 240 字节。
2. 服务器接受 TLS 1.2 和上面的套件，证书密钥是 RSA 2048 或 P-256。
3. `curl --tlsv1.2` 能拿到 `200` 和一份 TTI1 正文。
4. 错误 Token 返回非 200。
5. 设备时间已经同步。
6. Web 设置里打开自定义画报并保存。
7. 打开画报页。第一次没有自定义缓存时会马上请求；已有缓存时，向上拨动一次才会请求。
8. 右侧出现图片。串口有 `custom GET`，以及随后的 `show ... (自定义)`。
9. 再拨动一次，服务端看到新的 `Timestamp`。
10. 长按中间键，画面仍停在这张图上。
