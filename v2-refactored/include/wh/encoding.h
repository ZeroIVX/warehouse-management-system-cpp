// =============================================================================
//  encoding.h —— 编码探测与转换
//
//  背景：历史数据文件可能是 GBK（Excel 中文版默认）也可能是 UTF-8。
//        导入时先校验字节是否是合法 UTF-8，不是再按 GBK 转码，
//        从而同时兼容新旧数据，避免出现乱码商品名。
// =============================================================================
#pragma once

#include <windows.h>

#include <string>

namespace wh {

// UTF-8 -> UTF-16 宽字符
//
// 为什么必须用它：MSVC 的 std::ifstream / std::ofstream 接受 const char* 时，
// 会用「当前系统 ANSI 代码页」（中文系统即 GBK）去解释路径。
// 我们的路径字符串是 UTF-8，一旦含中文就会打不开文件，而且失败是静默的。
// 换成 const wchar_t* 重载后，路径统一走 UTF-16，中文路径不再出错。
inline std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

// 判断字节序列是否为合法 UTF-8
inline bool isValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t need = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            need = 1;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
        } else {
            return false;  // 孤立的延续字节或非法起始字节
        }
        if (i + need >= s.size()) return false;
        for (std::size_t k = 1; k <= need; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += need + 1;
    }
    return true;
}

// GBK -> UTF-8
inline std::string gbkToUtf8(const std::string& gbk) {
    if (gbk.empty()) return std::string();
    const int wlen = MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()),
                                        nullptr, 0);
    if (wlen <= 0) return gbk;
    std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(936, 0, gbk.c_str(), static_cast<int>(gbk.size()), wide.data(), wlen);

    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, nullptr, 0, nullptr,
                                        nullptr);
    if (len <= 0) return gbk;
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, out.data(), len, nullptr, nullptr);
    return out;
}

// 自动识别：合法 UTF-8 原样返回，否则按 GBK 转码
inline std::string normalizeToUtf8(const std::string& raw) {
    if (isValidUtf8(raw)) return raw;
    return gbkToUtf8(raw);
}

}  // namespace wh
