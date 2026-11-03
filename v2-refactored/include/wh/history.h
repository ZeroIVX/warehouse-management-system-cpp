// =============================================================================
//  history.h —— 操作日志领域模型
// =============================================================================
#pragma once

#include <ctime>
#include <string>

namespace wh {

enum class Action {
    Add,
    Modify,
    Remove,
    Import,
    Export,
    Sort,
};

inline const char* actionName(Action a) {
    switch (a) {
        case Action::Add: return "新增";
        case Action::Modify: return "修改";
        case Action::Remove: return "删除";
        case Action::Import: return "导入";
        case Action::Export: return "导出";
        case Action::Sort: return "排序";
    }
    return "未知";
}

// 序列化用短码，保证日志文件紧凑且与 UI 文案解耦
inline const char* actionCode(Action a) {
    switch (a) {
        case Action::Add: return "ADD";
        case Action::Modify: return "MOD";
        case Action::Remove: return "DEL";
        case Action::Import: return "IMP";
        case Action::Export: return "EXP";
        case Action::Sort: return "SRT";
    }
    return "UNK";
}

inline Action actionFromCode(const std::string& code) {
    if (code == "ADD") return Action::Add;
    if (code == "MOD") return Action::Modify;
    if (code == "DEL") return Action::Remove;
    if (code == "IMP") return Action::Import;
    if (code == "EXP") return Action::Export;
    return Action::Sort;
}

struct HistoryEntry {
    Action action = Action::Add;
    int itemId = 0;          // 0 表示非针对单个商品的批量操作
    std::time_t timestamp = 0;
    std::string detail;      // 附加说明，如商品名称
};

}  // namespace wh
