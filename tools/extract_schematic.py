#!/usr/bin/env python3
"""Extract an EasyEDA Pro schematic (.epro2) into JSON and Markdown.

The .epro2 file is a zip of project2.json plus one .epru document. Records in
the .epru are JSON pairs separated by "||", and each record ends with "|".
Nets are traced from wire endpoints, pin coordinates, and the NET attribute
on wires. The pin chapter in the Markdown is the part with designator U2.

    python3 tools/extract_schematic.py
    python3 tools/extract_schematic.py --input docs/Hardware/LiClock.epro2
"""

import argparse
import json
import re
import zipfile
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "docs" / "Hardware" / "LiClock.epro2"
DEFAULT_JSON = ROOT / "docs" / "Hardware" / "liclock.json"
DEFAULT_MARKDOWN = ROOT / "docs" / "Hardware" / "schematic.md"
TZ = timezone(timedelta(hours=8))

NET_LABEL_TITLES = {
    "Ground-GND",
    "Power-VCC",
    "5-Voltage",
    "Netport-IN",
    "Netport-OUT",
    "Netport-BI",
}
POWER_TITLES = {"Power-VCC", "5-Voltage"}
GROUND_TITLES = {"Ground-GND"}
RAIL_ORDER = ["+5V", "BAT", "VCC", "3V3", "SD_VDD", "GND"]

GPIO_ALIAS = {
    "SENSOR_VP": "GPIO36",
    "SENSOR_VN": "GPIO39",
}


def load_records(path):
    with zipfile.ZipFile(path) as zf:
        project = json.loads(zf.read("project2.json"))
        name = next(n for n in zf.namelist() if n.endswith(".epru"))
        text = zf.read(name).decode("utf-8")
    records = []
    for chunk in text.split("|\n"):
        chunk = chunk.strip()
        if not chunk or "||" not in chunk:
            continue
        head_s, body_s = chunk.split("||", 1)
        records.append((json.loads(head_s), json.loads(body_s)))
    docs = []
    current = None
    for head, body in records:
        if head.get("type") == "DOCHEAD":
            current = {"head": body, "items": []}
            docs.append(current)
        elif current is not None:
            current["items"].append((head, body))
    return project, docs


def meta_of(doc):
    for head, body in doc["items"]:
        if head.get("type") == "META":
            return body
    return {}


def stamp(ms):
    if not ms:
        return None
    return datetime.fromtimestamp(ms / 1000, TZ).strftime("%Y-%m-%d %H:%M")


def snap(x, y):
    return (round(float(x), 3), round(float(y), 3))


def rotate_cw(x, y, rotation, mirror):
    if mirror:
        x = -x
    turns = int(round(((rotation or 0) % 360) / 90)) % 4
    for _ in range(turns):
        x, y = y, -x
    return x, y


class UnionFind:
    def __init__(self):
        self.parent = {}

    def find(self, item):
        self.parent.setdefault(item, item)
        root = item
        while self.parent[root] != root:
            root = self.parent[root]
        while self.parent[item] != root:
            self.parent[item], item = root, self.parent[item]
        return root

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[rb] = ra


def resolve(value, attrs, depth=0):
    if depth > 4 or not isinstance(value, str):
        return value if isinstance(value, str) else ""
    if value.startswith("={") and value.endswith("}"):
        return resolve(attrs.get(value[2:-1], "") or "", attrs, depth + 1)
    return value


def designator_key(text):
    match = re.match(r"([A-Za-z]+)(\d+)$", text or "")
    if match:
        return (match.group(1), int(match.group(2)), "")
    return (text or "", 0, text or "")


def pin_key(number):
    text = str(number)
    if text.isdigit():
        return (0, int(text), "")
    return (1, 0, text)


def gpio_label(pin_name):
    if not pin_name:
        return ""
    if pin_name.startswith("IO") and pin_name[2:].isdigit():
        return f"GPIO{pin_name[2:]}"
    return GPIO_ALIAS.get(pin_name, "")


def esc(text):
    return str(text or "").replace("|", "\\|").replace("\n", " ")


def relative_to_root(path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(ROOT).as_posix()
    except ValueError:
        return str(resolved)


def schematic_title(docs):
    for doc in docs:
        if doc["head"].get("docType") == "SCH":
            return meta_of(doc).get("title") or ""
    return ""


def build(source_path):
    project, docs = load_records(source_path)
    symbols = {}
    devices = {}
    footprints = {}
    for doc in docs:
        kind = doc["head"].get("docType")
        uuid = doc["head"].get("uuid")
        if kind == "SYMBOL":
            children = defaultdict(list)
            title = ""
            for head, body in doc["items"]:
                if isinstance(body, dict) and body.get("parentId"):
                    children[body["parentId"]].append((head, body))
                if head.get("type") == "META":
                    title = body.get("title") or ""
            pins = []
            for head, body in doc["items"]:
                if head.get("type") != "PIN":
                    continue
                attrs = {
                    child_body.get("key"): child_body.get("value")
                    for child_head, child_body in children[head["id"]]
                    if child_head.get("type") == "ATTR"
                }
                pins.append({
                    "id": head.get("id"),
                    "partId": body.get("partId"),
                    "x": body.get("x") or 0,
                    "y": body.get("y") or 0,
                    "number": str(attrs.get("Pin Number") or ""),
                    "name": str(attrs.get("Pin Name") or ""),
                    "type": str(attrs.get("Pin Type") or ""),
                })
            symbols[uuid] = {"title": title, "pins": pins}
        elif kind == "DEVICE":
            devices[uuid] = meta_of(doc).get("attributes") or {}
        elif kind == "FOOTPRINT":
            footprints[uuid] = meta_of(doc).get("title") or uuid

    pages = []
    for doc in docs:
        if doc["head"].get("docType") != "SCH_PAGE":
            continue
        info = meta_of(doc)
        pages.append({
            "uuid": doc["head"].get("uuid"),
            "title": info.get("title") or "",
            "updated": stamp(doc["head"].get("updateTime")),
            "items": doc["items"],
        })

    no_connect = set()
    for page in pages:
        for head, body in page["items"]:
            if head.get("type") == "ATTR" and body.get("key") == "NO_CONNECT" and body.get("value") == "yes":
                parent = body.get("parentId") or ""
                comp_id, _, pin_id = parent.partition("-")
                if comp_id and pin_id:
                    no_connect.add((comp_id, pin_id))

    parts = []
    clusters = []
    warnings = []
    for page in pages:
        attrs = defaultdict(dict)
        components = []
        lines = []
        for head, body in page["items"]:
            if head.get("type") == "ATTR" and isinstance(body, dict) and body.get("parentId"):
                attrs[body["parentId"]][body.get("key")] = body.get("value")
            elif head.get("type") == "COMPONENT":
                components.append((head, body))
            elif head.get("type") == "LINE" and body.get("lineGroup"):
                lines.append(body)

        wire_nets = {}
        for head, body in page["items"]:
            if head.get("type") == "ATTR" and body.get("key") == "NET" and body.get("value"):
                wire_nets[body.get("parentId")] = str(body.get("value")).strip()

        uf = UnionFind()
        for line in lines:
            start = snap(line["startX"], line["startY"])
            end = snap(line["endX"], line["endY"])
            uf.union(("xy", start), ("xy", end))
            uf.union(("wire", line.get("lineGroup")), ("xy", start))

        sheet_parts = []
        labels = []
        for head, body in components:
            inst = attrs.get(head["id"], {})
            device_attrs = devices.get(inst.get("Device"), {})
            merged = dict(device_attrs)
            merged.update(inst)
            symbol_uuid = inst.get("Symbol") or device_attrs.get("Symbol")
            symbol = symbols.get(symbol_uuid)
            if symbol is None:
                warnings.append(f"missing symbol {symbol_uuid} on {page['title']} {head['id']}")
                symbol = {"title": "", "pins": []}
            designator = inst.get("Designator") or ""
            pin_set = [pin for pin in symbol["pins"] if pin["partId"] == body.get("partId")]
            if not pin_set:
                pin_set = list(symbol["pins"])
            placed = []
            for pin in pin_set:
                dx, dy = rotate_cw(pin["x"], pin["y"], body.get("rotation") or 0, body.get("isMirror"))
                point = snap((body.get("x") or 0) + dx, (body.get("y") or 0) + dy)
                placed.append({**pin, "point": point, "noConnect": (head["id"], pin["id"]) in no_connect})

            is_label = (not designator) and len(placed) == 1 and (
                symbol["title"] in NET_LABEL_TITLES or bool(inst.get("Name") or inst.get("Global Net Name"))
            )
            if is_label:
                raw_name = inst.get("Global Net Name") or inst.get("Name") or ""
                name = resolve(raw_name, merged).strip()
                if symbol["title"] in GROUND_TITLES and not name:
                    name = "GND"
                if name:
                    labels.append({
                        "name": name,
                        "symbol": symbol["title"],
                        "point": placed[0]["point"],
                        "compId": head["id"],
                    })
                    uf.union(("xy", placed[0]["point"]), ("label", page["title"], head["id"]))
                else:
                    warnings.append(f"unnamed label on {page['title']} {symbol['title']}")
                continue
            if not designator:
                continue

            footprint_raw = resolve(merged.get("Footprint") or "", merged)
            footprint = footprints.get(footprint_raw, footprint_raw)
            part = {
                "designator": designator,
                "sheet": page["title"],
                "symbol": symbol["title"],
                "value": resolve(merged.get("Value") or "", merged),
                "manufacturer": resolve(merged.get("Manufacturer") or "", merged),
                "manufacturerPart": resolve(merged.get("Manufacturer Part") or "", merged),
                "lcscPartName": resolve(merged.get("LCSC Part Name") or "", merged),
                "lcsc": resolve(merged.get("Supplier Part") or "", merged),
                "footprint": footprint,
                "package": resolve(merged.get("Supplier Footprint") or "", merged),
                "datasheet": resolve(merged.get("Datasheet") or "", merged),
                "description": resolve(merged.get("Description") or "", merged),
                "inBom": {"yes": True, "no": False}.get(str(merged.get("Add into BOM") or "").lower()),
                "pins": [],
            }
            for pin in placed:
                node = ("pin", page["title"], designator, pin["number"], pin["id"])
                uf.union(node, ("xy", pin["point"]))
                part["pins"].append({
                    "number": pin["number"],
                    "name": pin["name"],
                    "type": pin["type"],
                    "net": None,
                    "noConnect": pin["noConnect"],
                    "_node": node,
                })
            part["pins"].sort(key=lambda item: pin_key(item["number"]))
            sheet_parts.append(part)

        groups = defaultdict(lambda: {"pins": [], "labels": [], "wireNames": []})
        for part in sheet_parts:
            for pin in part["pins"]:
                groups[uf.find(pin["_node"])]["pins"].append((part, pin))
        for label in labels:
            groups[uf.find(("label", page["title"], label["compId"]))]["labels"].append(label)
        for wire_id, name in wire_nets.items():
            if ("wire", wire_id) not in uf.parent:
                continue
            groups[uf.find(("wire", wire_id))]["wireNames"].append(name)
        for root, group in groups.items():
            if not group["pins"] and not group["labels"] and not group["wireNames"]:
                continue
            names = []
            for name in [label["name"] for label in group["labels"]] + group["wireNames"]:
                if name and name not in names:
                    names.append(name)
            clusters.append({
                "sheet": page["title"],
                "names": names,
                "labelSymbols": sorted({label["symbol"] for label in group["labels"]}),
                "pins": group["pins"],
            })
        parts.extend(sheet_parts)

    # Merge clusters that share a net label. Same name is global across sheets.
    merge = UnionFind()
    by_name = defaultdict(list)
    for index, cluster in enumerate(clusters):
        merge.find(index)
        for name in cluster["names"]:
            by_name[name].append(index)
    for indexes in by_name.values():
        for index in indexes[1:]:
            merge.union(indexes[0], index)

    merged = defaultdict(list)
    for index, cluster in enumerate(clusters):
        merged[merge.find(index)].append(cluster)

    nets = []
    unnamed_serial = 0
    for group in merged.values():
        names = []
        for cluster in group:
            for name in cluster["names"]:
                if name not in names:
                    names.append(name)
        symbols = []
        for cluster in group:
            for title in cluster["labelSymbols"]:
                if title not in symbols:
                    symbols.append(title)
        pins = []
        seen = set()
        for cluster in group:
            for part, pin in cluster["pins"]:
                key = (part["designator"], pin["number"])
                if key in seen:
                    continue
                seen.add(key)
                pins.append({
                    "designator": part["designator"],
                    "pin": pin["number"],
                    "name": pin["name"],
                    "sheet": part["sheet"],
                    "_pin": pin,
                })
        if not pins:
            continue
        if len(names) > 1:
            warnings.append("net name alias: " + ", ".join(names))
        if names:
            net_name = names[0]
            aliases = names[1:]
            if any(title in GROUND_TITLES for title in symbols):
                kind = "ground"
            elif any(title in POWER_TITLES for title in symbols):
                kind = "power"
            else:
                kind = "signal"
            sheet = None
        else:
            if len(pins) < 2:
                for pin in pins:
                    pin["_pin"]["net"] = None
                continue
            unnamed_serial += 1
            net_name = f"UNNAMED-{unnamed_serial}"
            aliases = []
            kind = "unnamed"
            sheets = []
            for pin in pins:
                if pin["sheet"] not in sheets:
                    sheets.append(pin["sheet"])
            sheet = sheets[0] if len(sheets) == 1 else None
        for pin in pins:
            pin["_pin"]["net"] = net_name
        nets.append({
            "name": net_name,
            "aliases": aliases,
            "kind": kind,
            "sheet": sheet,
            "pins": [{k: v for k, v in pin.items() if k != "_pin"} for pin in pins],
        })

    def net_sort(net):
        if net["kind"] == "unnamed":
            return (2, net["name"])
        if net["name"] in RAIL_ORDER:
            return (0, RAIL_ORDER.index(net["name"]))
        return (1, net["name"])

    nets.sort(key=net_sort)
    parts.sort(key=lambda part: (pages_index(pages, part["sheet"]), designator_key(part["designator"])))
    for part in parts:
        for pin in part["pins"]:
            pin.pop("_node", None)

    duplicates = defaultdict(list)
    for part in parts:
        duplicates[part["designator"]].append(part["sheet"])
    for designator, sheets in duplicates.items():
        if len(sheets) > 1:
            warnings.append(f"duplicate designator {designator}: {sheets}")

    data = {
        "source": relative_to_root(source_path),
        "editorVersion": project.get("editorVersion"),
        "title": schematic_title(docs),
        "updated": max((page["updated"] for page in pages if page["updated"]), default=None),
        "sheets": [{"title": page["title"], "updated": page["updated"]} for page in pages],
        "parts": parts,
        "nets": nets,
        "rails": [net["name"] for net in nets if net["kind"] in ("power", "ground")],
    }
    return data, warnings, pages


def pages_index(pages, title):
    for index, page in enumerate(pages):
        if page["title"] == title:
            return index
    return 99


def passive_value(part):
    value = (part or {}).get("value") or ""
    if re.search(r"(Ω|F|H)$", value):
        return value
    return ""


def endpoint_text(pin, part_by_des, home_sheet, show_sheet=False):
    part = part_by_des.get(pin["designator"])
    label = f"{pin['designator']}.{pin['pin']}"
    if pin["name"] and pin["name"] != pin["pin"]:
        label += f" {pin['name']}"
    value = passive_value(part)
    if value and value not in label:
        label += f" {value}"
    if show_sheet or (home_sheet and pin["sheet"] != home_sheet):
        label += f"（{pin['sheet']}）"
    return label


def pin_summary(pin, net, part_by_des):
    if net is None:
        if pin["noConnect"]:
            return "—", "未连接，图纸标了 NC"
        return "—", "未连接"
    others = [item for item in net["pins"] if not (item["designator"] == "U2" and item["pin"] == pin["number"])]
    if net["name"] in ("GND",) or (net["kind"] in ("power", "ground") and len(others) > 6):
        return net["name"], f"{net['name']}（{len(net['pins'])} 端，见电源）"
    if not others:
        return net["name"], "仅这一端"
    text = "、".join(endpoint_text(item, part_by_des, "主控") for item in others)
    return net["name"], text


def markdown(data, json_note):
    part_by_des = {part["designator"]: part for part in data["parts"]}
    net_by_name = {net["name"]: net for net in data["nets"]}
    u2 = part_by_des.get("U2")
    lines = []
    lines.append(f"# {Path(data['source']).stem} 原理图")
    lines.append("")
    lines.append(
        f"源文件 `{data['source']}`，嘉立创 EDA 专业版 {data['editorVersion']}。"
        f"工程名 {data['title']}，图纸更新到 {data['updated']}。"
        "这份导出只含原理图。"
    )
    lines.append("")
    sheet_names = "、".join(item["title"] for item in data["sheets"])
    intro = f"图纸共 {len(data['sheets'])} 张：{sheet_names}。"
    if u2:
        module = u2.get("manufacturerPart") or u2.get("symbol") or ""
        intro = f"主控是 **U2 {module}**。" + intro
    lines.append(intro + "同名网络标号跨图纸连通。只挂着一个引脚、又没有网络名的线不列入网络表。")
    lines.append("")
    lines.append(f"同一份数据的结构化版本在 `{json_note}`。")
    lines.append("")
    floating = []
    for part in data["parts"]:
        for pin in part["pins"]:
            if not pin["net"] and not pin["noConnect"]:
                floating.append(f"{part['designator']}.{pin['number']} {pin['name']}（{part['sheet']}）")
    if floating:
        lines.append("悬空且图纸未标 NC 的引脚：" + "、".join(floating) + "。")
        lines.append("")
    lines.append("## 引脚")
    lines.append("")
    lines.append("U2 的模组引脚。网络名来自原理图上的网络标号。")
    lines.append("")
    lines.append("| 模组脚 | 名称 | 网络 | 对端 |")
    lines.append("| --- | --- | --- | --- |")
    if u2:
        for pin in sorted(u2["pins"], key=lambda item: pin_key(item["number"])):
            net = net_by_name.get(pin["net"])
            net_name, other = pin_summary(pin, net, part_by_des)
            gpio = gpio_label(pin["name"])
            name = pin["name"]
            if gpio and gpio != name:
                name = f"{name}（{gpio}）"
            lines.append(f"| {esc(pin['number'])} | {esc(name)} | {esc(net_name)} | {esc(other)} |")
    lines.append("")
    lines.append("## 电源")
    lines.append("")
    rail_nets = [net for net in data["nets"] if net["kind"] in ("power", "ground")]
    for net in rail_nets:
        kind = "地" if net["kind"] == "ground" else "电源"
        lines.append(f"### {net['name']}")
        lines.append("")
        lines.append(f"{kind}，{len(net['pins'])} 个端点。")
        lines.append("")
        by_sheet = defaultdict(list)
        for pin in net["pins"]:
            by_sheet[pin["sheet"]].append(pin)
        for sheet in [item["title"] for item in data["sheets"]]:
            pins = by_sheet.get(sheet)
            if not pins:
                continue
            pins = sorted(pins, key=lambda item: (designator_key(item["designator"]), pin_key(item["pin"])))
            text = "、".join(endpoint_text(pin, part_by_des, sheet) for pin in pins)
            lines.append(f"- {sheet}：{text}")
        lines.append("")
    lines.append("## 网络")
    lines.append("")
    lines.append("| 网络 | 连接 |")
    lines.append("| --- | --- |")
    for net in data["nets"]:
        if net["kind"] != "signal":
            continue
        sheet_order = {item["title"]: index for index, item in enumerate(data["sheets"])}
        pins = sorted(net["pins"], key=lambda item: (sheet_order.get(item["sheet"], 99), designator_key(item["designator"]), pin_key(item["pin"])))
        text = "、".join(endpoint_text(pin, part_by_des, "", show_sheet=True) for pin in pins)
        alias = f"（又名 {'、'.join(net['aliases'])}）" if net["aliases"] else ""
        lines.append(f"| {esc(net['name'])}{esc(alias)} | {esc(text)} |")
    lines.append("")
    unnamed = [net for net in data["nets"] if net["kind"] == "unnamed"]
    if unnamed:
        lines.append("## 未命名网络")
        lines.append("")
        lines.append("这几段线没有网络标号，两端器件是直接连着的。")
        lines.append("")
        lines.append("| 网络 | 图纸 | 连接 |")
        lines.append("| --- | --- | --- |")
        for net in unnamed:
            pins = sorted(net["pins"], key=lambda item: (designator_key(item["designator"]), pin_key(item["pin"])))
            text = "、".join(endpoint_text(pin, part_by_des, net["sheet"] or "") for pin in pins)
            lines.append(f"| {esc(net['name'])} | {esc(net['sheet'] or '')} | {esc(text)} |")
        lines.append("")
    lines.append("## 器件")
    lines.append("")
    for sheet in data["sheets"]:
        sheet_parts = [part for part in data["parts"] if part["sheet"] == sheet["title"]]
        lines.append(f"### {sheet['title']}")
        lines.append("")
        lines.append("| 位号 | 型号 | 值 | 封装 | 立创料号 |")
        lines.append("| --- | --- | --- | --- | --- |")
        for part in sheet_parts:
            model = part["manufacturerPart"] or part["symbol"]
            value = part["value"]
            if not value and part["lcscPartName"] and part["lcscPartName"] != model and len(part["lcscPartName"]) <= 24:
                value = part["lcscPartName"]
            if value == model:
                value = ""
            footprint = part["footprint"] if part["footprint"] not in ("", "-") else part["package"]
            lines.append(
                f"| {esc(part['designator'])} | {esc(model)} | {esc(value)} | {esc(footprint)} | {esc(part['lcsc'])} |"
            )
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def resolve_path(path):
    if path.is_absolute():
        return path
    return ROOT / path


def main():
    parser = argparse.ArgumentParser(description="Extract an EasyEDA Pro schematic into JSON and Markdown.")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT, help="EasyEDA Pro project (.epro2)")
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON, help="structured netlist output")
    parser.add_argument("--markdown", type=Path, default=DEFAULT_MARKDOWN, help="readable schematic notes")
    args = parser.parse_args()
    source = resolve_path(args.input)
    json_path = resolve_path(args.json)
    markdown_path = resolve_path(args.markdown)
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    data, warnings, _pages = build(source)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    with json_path.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    with markdown_path.open("w", encoding="utf-8") as handle:
        handle.write(markdown(data, relative_to_root(json_path)))
    print(f"wrote {relative_to_root(json_path)}")
    print(f"wrote {relative_to_root(markdown_path)}")
    print("parts", len(data["parts"]))
    print("nets", len(data["nets"]), "rails", data["rails"])
    print("warnings", len(warnings))
    for item in warnings:
        print(" WARN", item)
    u2 = next((part for part in data["parts"] if part["designator"] == "U2"), None)
    if u2 is None:
        return
    print("--- U2 ---")
    for pin in u2["pins"]:
        mark = " NC" if pin["noConnect"] else ""
        print(f"  {pin['number']:>3} {pin['name']:<16} {pin['net'] or '-'}{mark}")


if __name__ == "__main__":
    main()
