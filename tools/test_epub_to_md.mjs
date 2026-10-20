import { execFileSync } from "node:child_process";
import { crc32, deflateRawSync } from "node:zlib";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const { DOMParser } = require("linkedom");
const root = dirname(fileURLToPath(import.meta.url));

globalThis.DOMParser = DOMParser;
vm.runInThisContext(readFileSync(join(root, "epub_to_md.js"), "utf8"));

function entry(name, text, method) {
  const raw = Buffer.isBuffer(text) ? text : Buffer.from(text, "utf8");
  const data = method === 8 ? deflateRawSync(raw) : raw;
  return { name: Buffer.from(name, "utf8"), data, method, rawSize: raw.length, crc: crc32(raw) >>> 0 };
}

function zip(entries) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const item of entries) {
    const local = Buffer.alloc(30 + item.name.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(item.method, 8);
    local.writeUInt32LE(item.crc, 14);
    local.writeUInt32LE(item.data.length, 18);
    local.writeUInt32LE(item.rawSize, 22);
    local.writeUInt16LE(item.name.length, 26);
    item.name.copy(local, 30);
    locals.push(local, item.data);
    const central = Buffer.alloc(46 + item.name.length);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(item.method, 10);
    central.writeUInt32LE(item.crc, 16);
    central.writeUInt32LE(item.data.length, 20);
    central.writeUInt32LE(item.rawSize, 24);
    central.writeUInt16LE(item.name.length, 28);
    central.writeUInt32LE(offset, 42);
    item.name.copy(central, 46);
    centrals.push(central);
    offset += local.length + item.data.length;
  }
  const central = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(central.length, 12);
  eocd.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, central, eocd]);
}

const container = `<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>`;

const opf = `<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>测试之书: 第一/部</dc:title>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="cover" href="cover.png" media-type="image/png"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="cover" linear="no"/>
    <itemref idref="c2"/>
    <itemref idref="c1"/>
  </spine>
</package>`;

const ncx = `<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/">
  <navMap>
    <navPoint id="a"><navLabel><text>第一章</text></navLabel><content src="text/ch1.xhtml"/>
      <navPoint id="a1"><navLabel><text>小节</text></navLabel><content src="text/ch1.xhtml#s"/></navPoint>
    </navPoint>
    <navPoint id="b"><navLabel><text>第二章</text></navLabel><content src="text/ch2.xhtml"/></navPoint>
  </navMap>
</ncx>`;

const ch1 = `<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
<h1>第一章</h1>
<p>这是<strong>粗体</strong>和<em>斜体</em>，还有<a href="https://example.com/a)">链接</a>。</p>
<p>实体&amp;符号<br/>第二行</p>
<blockquote><p>引用一段话</p></blockquote>
<ul>
  <li>项目一</li>
  <li><input type="checkbox" checked="checked"/>已完成</li>
</ul>
<hr/>
<p>图前<img src="../a.png" alt="封面"/>图后</p>
<pre>line 1
line 2</pre>
<table><tr><th>甲</th><th>乙</th></tr><tr><td>1</td><td>2</td></tr></table>
<p># 这不是标题</p>
</body></html>`;

function gbk(text) {
  return execFileSync("python3", ["-c", "import sys; sys.stdout.buffer.write(sys.argv[1].encode('gbk'))", text]);
}

const ch2 = Buffer.concat([
  Buffer.from("<?xml version=\"1.0\" encoding=\"GBK\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><h2>", "ascii"),
  gbk("第二章"),
  Buffer.from("</h2><p>", "ascii"),
  gbk("第二章正文"),
  Buffer.from("</p></body></html>", "ascii"),
]);

function assert(cond, message) {
  if (!cond) {
    throw new Error(message);
  }
}

const book = zip([
  entry("mimetype", "application/epub+zip", 0),
  entry("META-INF/container.xml", container, 8),
  entry("OEBPS/content.opf", opf, 8),
  entry("OEBPS/toc.ncx", ncx, 8),
  entry("OEBPS/text/ch1.xhtml", ch1, 8),
  entry("OEBPS/text/ch2.xhtml", ch2, 0),
]);

const md = await epubToMarkdown(book, "下载/甲骨文.epub");
assert(md.name === "甲骨文.md", "name: " + md.name);
const text = md.text;
const order = text.indexOf("第二章正文");
const later = text.indexOf("这是**粗体**");
assert(order >= 0 && later > order, "spine order\n" + text);
assert(text.startsWith("# 测试之书: 第一/部\n"), "title");
assert(text.includes("## 目录\n\n- 第一章\n  - 小节\n- 第二章\n"), "toc\n" + text);
assert(text.includes("*斜体*"), "italic");
assert(text.includes("[链接](https://example.com/a%29)"), "link\n" + text);
assert(text.includes("实体&符号\n\n第二行"), "entity and break\n" + text);
assert(text.includes("> 引用一段话"), "quote");
assert(text.includes("- 项目一"), "list");
assert(text.includes("- [x] 已完成"), "task\n" + text);
assert(text.includes("---\n"), "rule");
assert(text.includes("图前") && text.includes("〔图〕") && text.includes("图后"), "image\n" + text);
assert(text.includes("```\nline 1\nline 2\n```"), "code");
assert(text.includes("| 甲 | 乙 |\n| --- | --- |\n| 1 | 2 |"), "table\n" + text);
assert(text.includes("\\# 这不是标题"), "escaped heading");
assert(!text.includes("<p>"), "no html");

const styledCss = `
.big { font-size: 1.83333em; }
.mid { font-size: 1.125em; }
.bold { font-weight: bold; }
.italic { font-style: italic; }
.underline { text-decoration: underline; }
`;
const styledOpf = `<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>样式之书</dc:title></metadata>
  <manifest>
    <item id="css" href="stylesheet.css" media-type="text/css"/>
    <item id="c1" href="text/ch.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c1"/></spine>
</package>`;
const styledChapter = `<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head>
<link rel="stylesheet" href="../stylesheet.css"/>
</head><body>
<p class="big">书名</p>
<p><span class="mid"><span class="bold">1　中间人</span></span></p>
<h2>原标题</h2>
<p>正文有<span class="bold">粗体</span>和<span class="italic">斜体</span>，还有<span class="underline">下划线</span>。</p>
<p><span class="bold">献词</span></p>
<p class="big">${"长".repeat(81)}</p>
</body></html>`;
const styled = await epubToMarkdown(zip([
  entry("mimetype", "application/epub+zip", 0),
  entry("META-INF/container.xml", container, 8),
  entry("OEBPS/content.opf", styledOpf, 8),
  entry("OEBPS/stylesheet.css", styledCss, 8),
  entry("OEBPS/text/ch.xhtml", styledChapter, 8),
]));
const styledText = styled.text;
assert(styledText.includes("# 书名\n"), "css h1\n" + styledText);
assert(styledText.includes("#### 1　中间人\n"), "css h4\n" + styledText);
assert(styledText.includes("## 原标题\n"), "tag heading\n" + styledText);
assert(styledText.includes("正文有**粗体**和*斜体*，还有<u>下划线</u>。"), "css marks\n" + styledText);
assert(styledText.includes("**献词**\n"), "bold paragraph\n" + styledText);
assert(!styledText.includes("# " + "长".repeat(10)), "long large text stays body\n" + styledText);

const pictureOpf = `<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>有图</dc:title></metadata>
  <manifest>
    <item id="c1" href="ch.xhtml" media-type="application/xhtml+xml"/>
    <item id="pic" href="pic.png" media-type="image/png"/>
  </manifest>
  <spine><itemref idref="c1"/><itemref idref="pic"/></spine>
</package>`;
const pictureChapter = `<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
<p>前文<img src="pic.png"/>后文</p>
<p><img src="pic.png"/></p>
</body></html>`;
globalThis.decodeEpubImage = async () => {
  const width = 8;
  const height = 8;
  const gray = new Uint8Array(width * height);
  gray.fill(255);
  for (let y = 0; y < height; y++) gray[y * width + 3] = 0;
  return { width, height, gray };
};
const pictured = await epubToMarkdown(zip([
  entry("mimetype", "application/epub+zip", 0),
  entry("META-INF/container.xml", container, 8),
  entry("OEBPS/content.opf", pictureOpf, 8),
  entry("OEBPS/ch.xhtml", pictureChapter, 8),
  entry("OEBPS/pic.png", "png", 0),
]), "有图.epub");
assert(pictured.images.length === 1, "one image " + pictured.images.length);
assert(pictured.images[0].name === "0001.i1", pictured.images[0].name);
const page = pictured.images[0].bytes;
assert(page.length === 8 + 50 * 282, "tti1 size " + page.length);
assert(page[0] === 0x54 && page[1] === 0x54 && page[2] === 0x49 && page[3] === 0x31, "magic");
assert(page[4] === 0x90 && page[5] === 0x01 && page[6] === 0x1a && page[7] === 0x01, "size");
assert(page[8] === 0xff, "letterbox");
assert(page.some((b, i) => i >= 8 && b !== 0xff), "has ink");
const marks = pictured.text.split("![](0001.i1)").length - 1;
assert(marks === 3, "image marks " + marks + "\n" + pictured.text);
globalThis.decodeEpubImage = async () => { throw new Error("decode"); };
const broken = await epubToMarkdown(zip([
  entry("mimetype", "application/epub+zip", 0),
  entry("META-INF/container.xml", container, 8),
  entry("OEBPS/content.opf", pictureOpf, 8),
  entry("OEBPS/ch.xhtml", pictureChapter, 8),
  entry("OEBPS/pic.png", "png", 0),
]), "有图.epub");
assert(broken.images.length === 0, "failed image dropped");
assert(broken.text.includes("〔图〕") && !broken.text.includes("![]("), "fallback\n" + broken.text);
delete globalThis.decodeEpubImage;

let failed = false;
try {
  await epubToMarkdown(Buffer.from("not an epub"));
  failed = true;
} catch (error) {
  assert(error.message === "不是 EPUB 文件", error.message);
}
assert(!failed, "invalid epub should fail");

const embedded = readFileSync(join(root, "../src/Service/TTBookTransferServer.cpp"), "utf8");
const source = readFileSync(join(root, "epub_to_md.js"), "utf8").trim();
assert(embedded.includes(source), "transfer page is missing the tested converter");

console.log("epub_to_md: ok");
