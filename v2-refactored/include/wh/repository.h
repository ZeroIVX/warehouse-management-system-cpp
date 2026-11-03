// =============================================================================
//  repository.h —— 持久化层
//
//  二进制格式 WHv2（相较原版 WHv1 的改进）：
//    * 全部字段使用定宽类型（int32 / uint32 / double），不再写 sizeof(size_t)
//      —— 原版写 size_t 导致 32 位与 64 位编译产物互相读不了文件
//    * 名称长度用 uint32 显式声明，字符串按 UTF-8 存储
//    * 结尾写入 "END!" 校验标记，截断损坏的文件能被识别而不是读到垃圾数据
//    * 兼容读取 WHv1 旧文件，读取后自动按新格式重写（无损迁移）
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "wh/history.h"
#include "wh/item.h"

namespace wh {

struct ImportStats {
    std::size_t totalLines = 0;
    std::size_t parsed = 0;
    std::size_t malformed = 0;
    std::vector<std::string> errors;  // 最多保留前 5 条错误明细
};

class Repository {
public:
    explicit Repository(std::string directory);

    // ---------------- 二进制读写 ----------------
    bool loadItems(std::vector<Item>& out, std::string& error, bool& wasLegacyFormat);
    bool saveItems(const std::vector<Item>& items, std::string& error);
    bool loadHistory(std::vector<HistoryEntry>& out, std::string& error);
    bool saveHistory(const std::vector<HistoryEntry>& entries, std::string& error);

    // ---------------- CSV 交换 ----------------
    static bool exportCsv(const std::string& path, const std::vector<Item>& items,
                          std::string& error);
    static bool importCsv(const std::string& path, std::vector<Item>& out, ImportStats& stats,
                          std::string& error);

    const std::string& itemsPath() const { return itemsPath_; }
    const std::string& historyPath() const { return historyPath_; }

private:
    std::string directory_;
    std::string itemsPath_;
    std::string historyPath_;

    std::string resolve(const std::string& fileName) const;
};

}  // namespace wh
