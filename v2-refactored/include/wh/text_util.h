// =============================================================================
//  text_util.h —— UTF-8 文本排版工具
//
//  核心问题：std::setw 按「字节数」补空格，而中文在 UTF-8 下占 3 字节、
//            显示宽度却是 2 列。直接用 setw 会导致表格严重错位。
//  解决：按 Unicode 码点解析字符串，按终端显示列宽重新补空格。
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace wh {

// 解码一个 UTF-8 码点，返回消耗的字节数（遇非法字节按 1 字节处理，容错不抛异常）
inline std::size_t decodeUtf8(const std::string& s, std::size_t i, char32_t& cp) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        return 1;
    }
    std::size_t need = 0;
    char32_t value = 0;
    if ((c0 & 0xE0) == 0xC0) {
        need = 2;
        value = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        need = 3;
        value = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        need = 4;
        value = c0 & 0x07;
    } else {
        cp = 0xFFFD;
        return 1;
    }
    if (i + need > s.size()) {
        cp = 0xFFFD;
        return 1;
    }
    for (std::size_t k = 1; k < need; ++k) {
        const unsigned char ck = static_cast<unsigned char>(s[i + k]);
        if ((ck & 0xC0) != 0x80) {
            cp = 0xFFFD;
            return 1;
        }
        value = (value << 6) | (ck & 0x3F);
    }
    cp = value;
    return need;
}

// 该码点在终端占两列（CJK 及全角字符）
inline bool isWideCodePoint(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||   // 谚文字母
           (cp >= 0x2E80 && cp <= 0x303E) ||   // CJK 部首、标点
           (cp >= 0x3041 && cp <= 0x33FF) ||   // 假名、CJK 兼容
           (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK 扩展 A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK 基本区
           (cp >= 0xA000 && cp <= 0xA4CF) ||   // 彝文
           (cp >= 0xAC00 && cp <= 0xD7A3) ||   // 谚文音节
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK 兼容表意
           (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK 兼容形式
           (cp >= 0xFF00 && cp <= 0xFF60) ||   // 全角形式
           (cp >= 0xFFE0 && cp <= 0xFFE6);
}

// 字符串在终端上占用的列数
inline std::size_t displayWidth(const std::string& s) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < s.size();) {
        char32_t cp = 0;
        const std::size_t len = decodeUtf8(s, i, cp);
        width += isWideCodePoint(cp) ? 2 : 1;
        i += len;
    }
    return width;
}

// 按字节截断，但不切断 UTF-8 字符，返回不超过 maxBytes 的合法前缀
inline std::string utf8Truncate(const std::string& s, std::size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp = 0;
        const std::size_t len = decodeUtf8(s, i, cp);
        if (i + len > maxBytes) break;
        i += len;
    }
    return s.substr(0, i);
}

// 按显示列宽截断，超出部分以省略号收尾
inline std::string ellipsize(const std::string& s, std::size_t maxWidth) {
    if (displayWidth(s) <= maxWidth) return s;
    if (maxWidth <= 1) return "…";
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp = 0;
        const std::size_t len = decodeUtf8(s, i, cp);
        const std::size_t w = isWideCodePoint(cp) ? 2 : 1;
        if (width + w > maxWidth - 1) break;
        width += w;
        i += len;
    }
    return s.substr(0, i) + "…";
}

inline std::string padRight(const std::string& s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    if (w >= width) return s;
    return s + std::string(width - w, ' ');
}

inline std::string padLeft(const std::string& s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    if (w >= width) return s;
    return std::string(width - w, ' ') + s;
}

inline std::string center(const std::string& s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    if (w >= width) return s;
    const std::size_t left = (width - w) / 2;
    return std::string(left, ' ') + s + std::string(width - w - left, ' ');
}

// 金额格式化：1234.5 -> "1234.50"
inline std::string formatMoney(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}

}  // namespace wh
