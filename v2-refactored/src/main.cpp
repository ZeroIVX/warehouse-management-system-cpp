// =============================================================================
//  main.cpp —— 程序入口
//
//  与 v1 的差异：
//    * 数据目录取自「可执行文件所在目录」而非当前工作目录，
//      从任何位置启动都能读到同一份数据（v1 用相对路径，双击与命令行的数据是两份）
//    * 控制台代码页由 ConsoleGuard 用 RAII 管理，异常退出也会还原
//    * 顶层 catch 兜底，避免未捕获异常直接崩掉进程
// =============================================================================
#include <windows.h>

#include <exception>
#include <string>

#include "wh/console.h"
#include "wh/console_ui.h"
#include "wh/repository.h"
#include "wh/service.h"

namespace {

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr,
                                        0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
}

// 可执行文件所在目录（带结尾反斜杠）
std::string executableDirectory() {
    wchar_t buffer[MAX_PATH * 2] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(MAX_PATH * 2));
    if (len == 0) return std::string(".\\");

    std::wstring path(buffer, len);
    const std::size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos + 1);
    } else {
        path = L".\\";
    }
    return wideToUtf8(path);
}

}  // namespace

int main() {
    wh::ConsoleGuard guard;  // 构造切 UTF-8，析构还原
    wh::setTitle("仓库管理系统 v2.0");

    try {
        const std::string dataDir = executableDirectory();
        wh::Repository repository(dataDir);
        wh::WarehouseService service(repository);

        std::string error;
        if (!service.initialize(error) && !error.empty()) {
            wh::clearScreen();
            wh::printWarn("数据加载告警：" + error);
            wh::printWarn("已忽略异常记录，程序继续运行。");
            wh::pauseForEnter();
        }

        wh::ConsoleApp app(service);
        return app.run();
    } catch (const std::exception& ex) {
        wh::printError(std::string("程序异常终止：") + ex.what());
        wh::pauseForEnter();
        return 1;
    } catch (...) {
        wh::printError("程序遇到未知异常，已终止。");
        wh::pauseForEnter();
        return 1;
    }
}
