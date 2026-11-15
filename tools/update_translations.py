#!/usr/bin/env python3
"""Regenerate src/i18n/echoPlayer_*.ts from the tr() strings in the sources.

The project ships a QTranslator that reads .ts (XML) directly, so this script
writes finished translations instead of lrelease binaries.

Usage:  python3 tools/update_translations.py
Adding a new tr("...") string without adding an English entry below makes the
script fail with a list of missing entries, so translations cannot silently rot.
"""

from collections import defaultdict
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

# tr() inside a Q_OBJECT class uses the class name as its QTranslator context.
CONTEXT_OF_FILE = {
    "src/ui/mainwindow.cpp": "MainWindow",
    "src/ui/titlebar.cpp": "TitleBar",
    "src/ui/controlbar.cpp": "ControlBar",
    "src/ui/adjustpanel.cpp": "AdjustPanel",
    "src/ui/spectrumwidget.cpp": "SpectrumWidget",
    "src/ui/mpvwidget.cpp": "MpvWidget",
    "src/ui/thumbnailprovider.cpp": "ThumbnailProvider",
    "src/playlist/playlistapp.cpp": "PlaylistApp",
    "src/i18n/languagemanager.cpp": "LanguageManager",
}

# ---------------------------------------------------------------- English ----
# Source text -> English. Strings without CJK characters default to themselves.
EN = {
    " (外挂)": " (external)",
    "%1%2 秒 → %3": "%1%2 s → %3",
    "A 点已设：%1，再按 A 设 B 点": "A point set at %1, press A again to set B",
    "A/B 循环已关闭": "A/B loop cleared",
    "A/B 循环：%1 - %2": "A/B loop: %1 - %2",
    "CUE 引用的音频文件不存在": "Audio file referenced by CUE not found",
    "上一集": "Previous file",
    "下一集": "Next file",
    "不循环": "No repeat",
    "亮度 %1": "Brightness %1",
    "亮度 + (B)": "Brightness + (B)",
    "亮度 - (N)": "Brightness - (N)",
    "亮度": "Brightness",
    "人声增强": "Vocal boost",
    "低音增强": "Bass boost",
    "倍速 %1x": "Speed %1x",
    "倍速: %1x": "Speed: %1x",
    "偏右声道": "Right channel",
    "偏左声道": "Left channel",
    "关": "Off",
    "关闭": "Off",
    "关闭时最小化到托盘": "Minimize to tray on close",
    "减速 ([)": "Slower ([)",
    "列表循环": "Repeat all",
    "加载外部字幕": "Load external subtitle",
    "加载外部字幕...": "Load external subtitle...",
    "加速 (])": "Faster (])",
    "单曲循环": "Repeat one",
    "原始": "Original",
    "原始 (1)": "Original (1)",
    "取消静音": "Unmute",
    "声道": "Channels",
    "声道: %1": "Channel: %1",
    "声音": "Audio",
    "媒体信息 (I)": "Media info (I)",
    "字幕": "Subtitles",
    "字幕: %1": "Subtitle: %1",
    "字幕大小 %1": "Subtitle size %1",
    "字幕延迟 %1 秒": "Subtitle delay %1 s",
    "字幕放大": "Larger subtitles",
    "字幕缩小": "Smaller subtitles",
    "容器: %1": "Container: %1",
    "工具": "Tools",
    r"左键：快进\n右键：下一个": r"Left click: forward\nRight click: next",
    r"左键：快退\n右键：上一个": r"Left click: rewind\nRight click: previous",
    "已加载字幕：%1": "Subtitle loaded: %1",
    "已接着上次看到 %1": "Resumed from %1",
    "已最小化到托盘，右键托盘图标可退出": "Minimized to tray; right-click the tray icon to quit",
    "已添加 %1 个文件到播放列表": "Added %1 files to the playlist",
    "已经是第一个": "Already the first file",
    "帧率: %1": "Frame rate: %1",
    "延后 0.1 秒 (X)": "Delay +0.1 s (X)",
    "开": "On",
    "循环：%1": "Repeat: %1",
    "循环：不循环": "Repeat: off",
    "恢复正常速度": "Reset to normal speed",
    "截图": "Screenshot",
    "截图 (S)": "Screenshot (S)",
    "截图已保存到 Pictures/EchoPlayer": "Screenshot saved to Pictures/EchoPlayer",
    "打开媒体文件": "Open media file",
    "打开文件": "Open file",
    "打开文件...": "Open file...",
    "打开网络流": "Open network stream",
    "打开网络流... (Ctrl+U)": "Open network stream... (Ctrl+U)",
    "打开...": "Open...",
    "提前 0.1 秒 (Z)": "Delay -0.1 s (Z)",
    "播放": "Play",
    "播放 / 暂停": "Play / Pause",
    "播放列表": "Playlist",
    "播放列表已清空": "Playlist cleared",
    "播放结束": "Playback finished",
    "文件: %1": "File: %1",
    "旋转 %1°": "Rotate %1°",
    "旋转 90° (R)": "Rotate 90° (R)",
    "无法打开文件：%1": "Cannot open file: %1",
    "无音轨": "No audio tracks",
    "时长: %1": "Duration: %1",
    "显示 / 隐藏": "Show / Hide",
    "显示 / 隐藏播放列表": "Show / hide playlist",
    "暂停": "Pause",
    "未找到播放列表程序": "Playlist program not found",
    "添加文件到列表": "Add files to playlist",
    "添加文件到列表...": "Add files to playlist...",
    "添加文件到播放列表": "Add files to playlist",
    "清空": "Clear",
    "清空列表": "Clear playlist",
    "清除 A/B 循环": "Clear A/B loop",
    "画面比例": "Aspect ratio",
    "画面比例 %1": "Aspect ratio %1",
    "画面设置已重置": "Picture settings reset",
    "硬件解码": "Hardware decoding",
    "硬件解码: %1": "Hardware decoding: %1",
    "立体声": "Stereo",
    "网络流": "Network stream",
    "自动": "Auto",
    "视频": "Video",
    "视频: %1  %2x%3": "Video: %1  %2x%3",
    "解码: %1": "Codec: %1",
    "设置 A/B 循环点 (A)": "Set A/B loop point (A)",
    "请输入带协议头的完整地址，例如 https://...": "Enter a full URL including the scheme, e.g. https://...",
    "调整面板": "Adjustments",
    "调整面板... (Ctrl+P)": "Adjustments... (Ctrl+P)",
    "输入 http / https / rtsp / rtmp 地址：": "Enter an http / https / rtsp / rtmp address:",
    "退出": "Quit",
    "选择字幕": "Select subtitle",
    "选择音轨": "Select audio track",
    "逐帧前进 (.)": "Frame step (.)",
    "重置画面": "Reset picture",
    "错误：%1": "Error: %1",
    "随机 %1 / 循环 %2": "Shuffle %1 / Repeat %2",
    "随机播放 (Ctrl+R)": "Shuffle (Ctrl+R)",
    "随机播放：关": "Shuffle: off",
    "随机播放：开": "Shuffle: on",
    "随机": "Shuffle",
    "随机播放": "Shuffle",
    "静音": "Mute",
    "音效 (E)": "Sound effect (E)",
    "音效: %1": "Sound effect: %1",
    "音量 %1%": "Volume %1%",
    "音频: %1": "Audio: %1",
    "高音增强": "Treble boost",
    "停止": "Stop",
    "切换静音": "Toggle mute",
    "列表": "Playlist",
    "最小化": "Minimize",
    "最大化": "Maximize",
    "全屏": "Fullscreen",
    "语言": "Language",
    "配置": "Preferences",
    "中文(简体)": "Chinese (Simplified)",
    "中文(繁體)": "Chinese (Traditional)",
    "上移": "Move up",
    "下移": "Move down",
    "从列表移除": "Remove from playlist",
    "切换循环模式": "Cycle repeat mode",
    "添加": "Add",
    "画面": "Picture",
    "画面调整": "Picture adjustments",
    "图形均衡器": "Graphic equalizer",
    "其他音频设置": "Other audio settings",
    "声道平衡": "Channel balance",
    "大小与位置": "Size and position",
    "字体": "Font",
    "字体与样式": "Font and style",
    "字号": "Font size",
    "字幕延迟": "Subtitle delay",
    "字幕样式": "Subtitle style",
    "对比度": "Contrast",
    "居中": "Center",
    "左": "Left",
    "右": "Right",
    "描边": "Outline",
    "描边粗细": "Outline width",
    "文字": "Text",
    "斜体": "Italic",
    "水平对齐": "Horizontal alignment",
    "粗体": "Bold",
    "色相": "Hue",
    "边缘模糊": "Edge blur",
    "选择颜色": "Choose color",
    "重置本页": "Reset this page",
    "阴影": "Shadow",
    "阴影偏移": "Shadow offset",
    "音效预设": "Sound preset",
    "音轨延迟": "Audio delay",
    "音量归一化": "Volume normalization",
    "音频": "Audio",
    "颜色": "Colors",
    "饱和度": "Saturation",
    "伽马": "Gamma",
    "垂直位置": "Vertical position",
    "平直": "Flat",
}

# Context-specific overrides for strings that mean different things elsewhere.
EN_CTX = {
    ("TitleBar", "关闭"): "Close",
    ("AdjustPanel", "关闭"): "Close",
}

# ------------------------------------------------------------- Traditional ----
# Simplified -> Traditional single-character mapping (only the characters that
# actually occur in the UI strings; harmless for characters already traditional).
S2T = {
    "与": "與", "个": "個", "体": "體", "关": "關", "闭": "閉", "门": "門",
    "设": "設", "语": "語", "议": "議", "显": "顯", "隐": "隱", "图": "圖",
    "标": "標", "声": "聲", "频": "頻", "视": "視", "讯": "訊", "随": "隨",
    "环": "環", "复": "復", "单": "單", "选": "選", "择": "擇", "轨": "軌",
    "码": "碼", "编": "編", "帧": "幀", "画": "畫", "转": "轉", "静": "靜",
    "动": "動", "电": "電", "网": "網", "络": "絡", "连": "連", "时": "時",
    "长": "長", "错": "錯", "无": "無", "这": "這", "后": "後", "进": "進",
    "载": "載", "减": "減", "缩": "縮", "强": "強", "调": "調", "节": "節",
    "归": "歸", "经": "經", "过": "過", "还": "還", "来": "來", "对": "對",
    "当": "當", "为": "為", "会": "會", "说": "說", "内": "內", "协": "協",
    "输": "輸", "请": "請", "带": "帶", "头": "頭", "态": "態", "状": "狀",
    "暂": "暫", "记": "記", "储": "儲", "档": "檔", "线": "線", "边": "邊",
    "缘": "緣", "阴": "陰", "饱": "飽", "马": "馬", "齐": "齊", "页": "頁",
    "预": "預", "号": "號", "盖": "蓋", "资": "資", "属": "屬", "数": "數",
    "据": "據", "题": "題", "结": "結", "层": "層", "变": "變", "发": "發",
    "见": "見", "处": "處", "务": "務", "盘": "盤", "键": "鍵", "击": "擊",
    "启": "啟", "寻": "尋", "挂": "掛", "细": "細", "简": "簡", "开": "開",
    "间": "間", "问": "問", "软": "軟", "优": "優", "较": "較", "样": "樣",
    "释": "釋", "换": "換", "误": "誤", "迟": "遲", "颜": "顏", "着": "著",
    "点": "點", "钟": "鐘", "录": "錄", "现": "現", "观": "觀", "体": "體",
}

# Taiwanese terminology / whole-phrase overrides (longest first).
TW_WORDS = {
    "请输入带协议头的完整地址，例如 https://...": "請輸入包含通訊協定的完整網址，例如 https://...",
    "输入 http / https / rtsp / rtmp 地址：": "輸入 http / https / rtsp / rtmp 網址：",
    "已最小化到托盘，右键托盘图标可退出": "已最小化到系統匣，右鍵點選圖示可結束",
    "CUE 引用的音频文件不存在": "CUE 參照的音訊檔案不存在",
    "网络流": "網路串流",
    "播放列表": "播放清單",
    "媒体信息": "媒體資訊",
    "音量归一化": "音量正規化",
    "图形均衡器": "圖形等化器",
    "协议头": "通訊協定開頭",
    "关闭时最小化到托盘": "關閉時最小化到系統匣",
    "加载外部字幕": "載入外部字幕",
    "均衡器": "等化器",
    "归一化": "正規化",
    "网络": "網路", "视频": "視訊", "音频": "音訊", "信息": "資訊",
    "文件": "檔案", "列表": "清單", "配置": "設定", "设置": "設定",
    "重置": "重設", "加载": "載入", "保存": "儲存", "添加": "新增",
    "程序": "程式", "托盘": "系統匣", "图标": "圖示", "全屏": "全螢幕",
    "地址": "位址", "打开": "開啟", "关闭": "關閉", "字体": "字型",
    "字号": "字型大小", "居中": "置中", "伽马": "伽瑪", "逐帧": "逐幀",
    "帧率": "幀率", "截图": "截圖", "声道": "聲道", "硬件": "硬體",
    "解码": "解碼", "画面": "畫面", "颜色": "顏色", "粗体": "粗體",
    "斜体": "斜體", "阴影": "陰影", "描边": "描邊", "边缘": "邊緣",
    "对齐": "對齊", "接着": "接著", "放大": "放大", "缩小": "縮小",
}
TW_ASCII = {
    "Playlist": "播放清單",
    "Playlist (%1)": "播放清單 (%1)",
    "Playlist (0)": "播放清單 (0)",
    "English": "English",
}
CN_ASCII = {
    "Playlist": "播放列表",
    "Playlist (%1)": "播放列表 (%1)",
    "Playlist (0)": "播放列表 (0)",
    "English": "English",
}
CN_OVERRIDE = {"中文(繁體)": "中文(繁体)"}

CJK = re.compile(r"[一-鿿]")


def collect():
    contexts = defaultdict(set)
    pattern_tr = re.compile(r'(?<!\w)tr\s*\(\s*"((?:[^"\\]|\\.)*)"')
    pattern_xt = re.compile(
        r'QCoreApplication::translate\s*\(\s*"([A-Za-z]+)"\s*,\s*"((?:[^"\\]|\\.)*)"'
    )
    for path in sorted((ROOT / "src").rglob("*.cpp")):
        rel = path.relative_to(ROOT).as_posix()
        ctx = CONTEXT_OF_FILE.get(rel)
        if not ctx:
            continue
        text = path.read_text(encoding="utf-8")
        contexts[ctx].update(pattern_tr.findall(text))
        for c, s in pattern_xt.findall(text):
            contexts[c].add(s)
    return contexts


def xml_escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def english(ctx, src):
    if not CJK.search(src):
        return src
    if (ctx, src) in EN_CTX:
        return EN_CTX[(ctx, src)]
    return EN.get(src)


def traditional(ctx, src):
    if not CJK.search(src):
        return TW_ASCII.get(src, src)
    for phrase, replacement in sorted(TW_WORDS.items(), key=lambda kv: -len(kv[0])):
        src = src.replace(phrase, replacement)
    return "".join(S2T.get(c, c) for c in src)


def simplified(ctx, src):
    if not CJK.search(src):
        return CN_ASCII.get(src, src)
    return CN_OVERRIDE.get(src, src)


def write_ts(path, lang, contexts, translate):
    out = [
        '<?xml version="1.0" encoding="utf-8"?>',
        "<!DOCTYPE TS>",
        f'<TS version="2.1" language="{lang}">',
    ]
    for ctx in sorted(contexts):
        if not contexts[ctx]:
            continue
        out.append("<context>")
        out.append(f"    <name>{ctx}</name>")
        for src in sorted(contexts[ctx]):
            dst = translate(ctx, src)
            out.append("    <message>")
            out.append(f"        <source>{xml_escape(src)}</source>")
            out.append(f"        <translation>{xml_escape(dst)}</translation>")
            out.append("    </message>")
        out.append("</context>")
    out.append("</TS>")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")


def main():
    contexts = collect()
    missing = sorted(
        {(ctx, src) for ctx, srcs in contexts.items() for src in srcs
         if english(ctx, src) is None}
    )
    if missing:
        print("Missing English translations:", file=sys.stderr)
        for ctx, src in missing:
            print(f"  [{ctx}] {src}", file=sys.stderr)
        return 1

    total = sum(len(v) for v in contexts.values())
    targets = {
        "en": ("src/i18n/echoPlayer_en.ts", english),
        "zh_CN": ("src/i18n/echoPlayer_zh_CN.ts", simplified),
        "zh_TW": ("src/i18n/echoPlayer_zh_TW.ts", traditional),
    }
    for lang, (rel, fn) in targets.items():
        write_ts(ROOT / rel, lang, contexts, fn)
        print(f"wrote {rel}")

    # Sanity: a Traditional translation should not still contain simplified
    # characters from our table (word overrides may legitimately keep some).
    leftovers = sorted(
        {c for ctx, srcs in contexts.items() for src in srcs
         for c in traditional(ctx, src) if c in S2T}
    )
    if leftovers:
        print("warning: simplified chars left in zh_TW:", "".join(leftovers))
    print(f"{total} strings in {len(contexts)} contexts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
