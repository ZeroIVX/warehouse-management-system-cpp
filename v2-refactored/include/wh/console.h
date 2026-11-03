// =============================================================================
//  console.h —— 控制台抽象层（颜色 / 清屏 / 健壮输入）
//
//  设计要点：
//    * ConsoleGuard 用 RAII 管理控制台代码页：构造时切 UTF-8，析构时无条件还原，
//      即使中间抛异常也不会把用户的终端留在 UTF-8 状态。
//    * 所有输入走 getline + 显式解析，不使用 cin >> int。
//      原版 `cin >> choice` 在输入字母后会置位 failbit，导致菜单无限刷屏——这是硬 bug。
//    * 输入流关闭（EOF / Ctrl+Z）时返回 std::nullopt，由上层优雅退出。
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace wh {

enum class Color {
    Default = 7,
    Gray = 8,
    Green = 10,
    Cyan = 11,
    Red = 12,
    Yellow = 14,
    White = 15,
};

// RAII：控制台代码页守卫
class ConsoleGuard {
public:
    ConsoleGuard();
    ~ConsoleGuard();
    ConsoleGuard(const ConsoleGuard&) = delete;
    ConsoleGuard& operator=(const ConsoleGuard&) = delete;

private:
    unsigned int oldOutputCp_ = 0;
    unsigned int oldInputCp_ = 0;
};

void setColor(Color c);
void clearScreen();
void setTitle(const std::string& utf8Title);

// ---------------- 输入（均可能返回 nullopt：输入流已关闭）----------------
std::optional<std::string> readLine(const std::string& prompt);
std::optional<int> readInt(const std::string& prompt, int lo, int hi);
std::optional<double> readDouble(const std::string& prompt, double lo, double hi);
std::optional<char> readConfirm(const std::string& prompt);  // 返回 'y' 或 'n'

// 纯解析（不做范围校验，也不提示），用于「回车保持不变」这类编辑场景
std::optional<int> parseInteger(const std::string& text);
std::optional<double> parseDouble(const std::string& text);

// 等待用户按回车（替代原版的 _getch()，兼容管道输入与终端）
void pauseForEnter(const std::string& msg = "按回车键继续...");

// ---------------- 输出装饰 ----------------
void printSeparator(char c = '=', int width = 64);
void printTitle(const std::string& text);
void printError(const std::string& msg);
void printSuccess(const std::string& msg);
void printInfo(const std::string& msg);
void printWarn(const std::string& msg);

// 清屏后打印页面标题
void beginPage(const std::string& title);

}  // namespace wh
