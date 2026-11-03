#include "wh/console.h"

// NOMINMAX：阻止 windows.h 把 min/max 定义成宏，否则会和 std::numeric_limits 冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace wh {
namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

void applyColor(int code) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), static_cast<WORD>(code));
}

// 去掉首尾空白
std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

// ---------------------------------------------------------------------------
//  ConsoleGuard
// ---------------------------------------------------------------------------
ConsoleGuard::ConsoleGuard()
    : oldOutputCp_(GetConsoleOutputCP()), oldInputCp_(GetConsoleCP()) {
    // 源码与运行时字符串统一为 UTF-8，控制台也切到 65001 才能正确显示中文
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

ConsoleGuard::~ConsoleGuard() {
    if (oldOutputCp_ != 0) SetConsoleOutputCP(oldOutputCp_);
    if (oldInputCp_ != 0) SetConsoleCP(oldInputCp_);
}

// ---------------------------------------------------------------------------
//  基础操作
// ---------------------------------------------------------------------------
void setColor(Color c) { applyColor(static_cast<int>(c)); }

void clearScreen() {
    // 使用 Win32 API 而不是 system("cls")：不依赖 shell，快且无闪烁
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(h, &info)) return;
    const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
    DWORD written = 0;
    COORD home{0, 0};
    FillConsoleOutputCharacterA(h, ' ', cells, home, &written);
    FillConsoleOutputAttribute(h, info.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
}

void setTitle(const std::string& utf8Title) {
    const std::wstring w = utf8ToWide(utf8Title);
    if (!w.empty()) SetConsoleTitleW(w.c_str());
}

// ---------------------------------------------------------------------------
//  输入
// ---------------------------------------------------------------------------
std::optional<std::string> readLine(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;  // 输入流关闭
    }
    return line;
}

std::optional<int> parseInteger(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return std::nullopt;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

std::optional<double> parseDouble(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

std::optional<int> readInt(const std::string& prompt, int lo, int hi) {
    while (true) {
        auto line = readLine(prompt);
        if (!line) return std::nullopt;
        const std::string s = trim(*line);
        if (s.empty()) {
            printError("输入不能为空，请重新输入。");
            continue;
        }
        const auto parsed = parseInteger(s);
        if (!parsed) {
            printError("需要整数，请重新输入。");
            continue;
        }
        if (*parsed < lo || *parsed > hi) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "取值范围 %d ~ %d，请重新输入。", lo, hi);
            printError(buf);
            continue;
        }
        return parsed;
    }
}

std::optional<double> readDouble(const std::string& prompt, double lo, double hi) {
    while (true) {
        auto line = readLine(prompt);
        if (!line) return std::nullopt;
        const std::string s = trim(*line);
        if (s.empty()) {
            printError("输入不能为空，请重新输入。");
            continue;
        }
        const auto parsed = parseDouble(s);
        if (!parsed) {
            printError("需要数字，请重新输入。");
            continue;
        }
        if (*parsed < lo || *parsed > hi) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "取值范围 %.2f ~ %.2f，请重新输入。", lo, hi);
            printError(buf);
            continue;
        }
        return parsed;
    }
}

std::optional<char> readConfirm(const std::string& prompt) {
    while (true) {
        auto line = readLine(prompt);
        if (!line) return std::nullopt;
        const std::string s = trim(*line);
        if (s.empty()) continue;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        if (c == 'y' || c == 'n') return c;
        printError("请输入 y 或 n。");
    }
}

void pauseForEnter(const std::string& msg) {
    std::cout << "\n" << msg;
    std::cout.flush();
    std::string dummy;
    std::getline(std::cin, dummy);
}

// ---------------------------------------------------------------------------
//  输出装饰
// ---------------------------------------------------------------------------
void printSeparator(char c, int width) {
    setColor(Color::Gray);
    std::cout << std::string(static_cast<std::size_t>(width), c) << "\n";
    setColor(Color::Default);
}

void printTitle(const std::string& text) {
    setColor(Color::Yellow);
    std::cout << text << "\n";
    setColor(Color::Default);
}

void printError(const std::string& msg) {
    setColor(Color::Red);
    std::cout << "[错误] " << msg << "\n";
    setColor(Color::Default);
}

void printSuccess(const std::string& msg) {
    setColor(Color::Green);
    std::cout << "[成功] " << msg << "\n";
    setColor(Color::Default);
}

void printInfo(const std::string& msg) {
    setColor(Color::Cyan);
    std::cout << msg << "\n";
    setColor(Color::Default);
}

void printWarn(const std::string& msg) {
    setColor(Color::Yellow);
    std::cout << "[提示] " << msg << "\n";
    setColor(Color::Default);
}

void beginPage(const std::string& title) {
    clearScreen();
    printTitle(title);
    printSeparator('-');
}

}  // namespace wh
