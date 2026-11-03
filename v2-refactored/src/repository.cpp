#include "wh/repository.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "wh/encoding.h"
#include "wh/text_util.h"

namespace wh {
namespace {

constexpr char kMagicV2[4] = {'W', 'H', 'v', '2'};
constexpr char kMagicV1[4] = {'W', 'H', 'v', '1'};
constexpr char kTail[4] = {'E', 'N', 'D', '!'};
constexpr std::uint32_t kMaxNameBytes = 4096;      // 防御畸形文件导致的超大分配
constexpr std::uint64_t kMaxItemCount = 10000000;  // 同上

template <class T>
void writePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T>
bool readPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<std::size_t>(is.gcount()) == sizeof(T);
}

// 读取指定长度的字符串（UTF-8）
bool readString(std::istream& is, std::uint32_t length, std::string& out) {
    if (length > kMaxNameBytes) return false;
    out.assign(length, '\0');
    if (length == 0) return true;
    is.read(out.data(), length);
    return static_cast<std::size_t>(is.gcount()) == length;
}

// CSV 字段转义：含逗号 / 引号 / 换行时用双引号包裹，内部引号双写
std::string csvEscape(const std::string& field) {
    const bool needQuote = field.find_first_of(",\"\r\n") != std::string::npos;
    if (!needQuote) return field;
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

// 单行 CSV 解析，支持引号包裹与转义
bool parseCsvLine(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string cur;
    bool inQuote = false;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (inQuote) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    i += 2;
                    continue;
                }
                inQuote = false;
                ++i;
                continue;
            }
            cur += c;
            ++i;
        } else {
            if (c == '"') {
                inQuote = true;
                ++i;
            } else if (c == ',') {
                fields.push_back(cur);
                cur.clear();
                ++i;
            } else {
                cur += c;
                ++i;
            }
        }
    }
    if (inQuote) return false;  // 引号未闭合
    fields.push_back(cur);
    return true;
}

bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// 文件路径一律转成宽字符串打开：MSVC 的 char* 重载走 ANSI 代码页，中文路径会静默失败
std::wstring widen(const std::string& utf8Path) { return utf8ToWide(utf8Path); }

std::string trimCopy(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

}  // namespace

Repository::Repository(std::string directory) : directory_(std::move(directory)) {
    if (!directory_.empty()) {
        const char last = directory_.back();
        if (last != '\\' && last != '/') directory_ += '\\';
    }
    itemsPath_ = resolve("warehouse.dat");
    historyPath_ = resolve("history.dat");
}

std::string Repository::resolve(const std::string& fileName) const {
    return directory_ + fileName;
}

// ---------------------------------------------------------------------------
//  商品数据
// ---------------------------------------------------------------------------
bool Repository::loadItems(std::vector<Item>& out, std::string& error, bool& wasLegacyFormat) {
    out.clear();
    wasLegacyFormat = false;

    std::ifstream file(widen(itemsPath_), std::ios::binary);
    if (!file) {
        return true;  // 首次运行，没有数据文件属正常情况
    }

    char magic[4] = {0, 0, 0, 0};
    file.read(magic, 4);
    if (file.gcount() != 4) {
        error = "数据文件头部不完整，可能已损坏。";
        return false;
    }

    if (std::memcmp(magic, kMagicV2, 4) == 0) {
        std::uint64_t count = 0;
        if (!readPod(file, count) || count > kMaxItemCount) {
            error = "数据文件记录数异常。";
            return false;
        }
        out.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            Item item;
            std::int32_t id = 0;
            std::uint32_t nameLen = 0;
            std::int32_t quantity = 0;
            double price = 0.0;
            if (!readPod(file, id) || !readPod(file, nameLen) || !readString(file, nameLen, item.name) ||
                !readPod(file, quantity) || !readPod(file, price)) {
                error = "数据文件在读取第 " + std::to_string(i + 1) + " 条记录时中断。";
                out.clear();
                return false;
            }
            item.id = id;
            item.quantity = quantity;
            item.price = price;
            out.push_back(std::move(item));
        }

        char tail[4] = {0, 0, 0, 0};
        file.read(tail, 4);
        if (file.gcount() != 4 || std::memcmp(tail, kTail, 4) != 0) {
            error = "数据文件结尾校验失败，文件可能被截断（已读取的记录仍然可用）。";
            return false;
        }
        return true;
    }

    if (std::memcmp(magic, kMagicV1, 4) == 0) {
        // 兼容旧版本：原程序用 sizeof(size_t) 写入计数与长度，x64 下为 8 字节
        wasLegacyFormat = true;
        std::uint64_t count = 0;
        if (!readPod(file, count) || count > kMaxItemCount) {
            error = "旧版数据文件记录数异常。";
            return false;
        }
        out.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            Item item;
            std::int32_t id = 0;
            std::uint64_t nameLen = 0;
            std::int32_t quantity = 0;
            double price = 0.0;
            if (!readPod(file, id) || !readPod(file, nameLen) ||
                nameLen > kMaxNameBytes ||
                !readString(file, static_cast<std::uint32_t>(nameLen), item.name) ||
                !readPod(file, quantity) || !readPod(file, price)) {
                error = "旧版数据文件在读取第 " + std::to_string(i + 1) + " 条记录时中断。";
                out.clear();
                return false;
            }
            item.id = id;
            item.quantity = quantity;
            item.price = price;
            item.name = normalizeToUtf8(item.name);  // 旧文件为 GBK
            out.push_back(std::move(item));
        }
        return true;
    }

    error = "无法识别的数据文件格式。";
    return false;
}

bool Repository::saveItems(const std::vector<Item>& items, std::string& error) {
    // 先写临时文件再原子替换，避免写入过程中崩溃导致数据全丢
    const std::string tmpPath = itemsPath_ + ".tmp";
    {
        std::ofstream file(widen(tmpPath), std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "无法创建数据文件：" + tmpPath;
            return false;
        }

        file.write(kMagicV2, 4);
        const std::uint64_t count = static_cast<std::uint64_t>(items.size());
        writePod(file, count);

        for (const Item& item : items) {
            const std::int32_t id = item.id;
            const std::uint32_t nameLen = static_cast<std::uint32_t>(item.name.size());
            const std::int32_t quantity = item.quantity;
            writePod(file, id);
            writePod(file, nameLen);
            if (nameLen > 0) file.write(item.name.data(), nameLen);
            writePod(file, quantity);
            writePod(file, item.price);
        }

        file.write(kTail, 4);
        file.flush();
        if (!file) {
            error = "写入数据文件失败（磁盘空间不足或权限不足）。";
            return false;
        }
    }

    _wremove(widen(itemsPath_).c_str());
    if (_wrename(widen(tmpPath).c_str(), widen(itemsPath_).c_str()) != 0) {
        error = "数据文件替换失败：" + itemsPath_;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  操作日志
// ---------------------------------------------------------------------------
bool Repository::loadHistory(std::vector<HistoryEntry>& out, std::string& error) {
    out.clear();
    std::ifstream file(widen(historyPath_), std::ios::binary);
    if (!file) return true;

    std::uint64_t count = 0;
    if (!readPod(file, count) || count > kMaxItemCount) {
        error = "日志文件记录数异常。";
        return false;
    }
    out.reserve(static_cast<std::size_t>(count));

    for (std::uint64_t i = 0; i < count; ++i) {
        HistoryEntry entry;
        std::uint32_t codeLen = 0;
        std::string code;
        std::int32_t itemId = 0;
        std::int64_t ts = 0;
        std::uint32_t detailLen = 0;
        if (!readPod(file, codeLen) || !readString(file, codeLen, code) ||
            !readPod(file, itemId) || !readPod(file, ts) ||
            !readPod(file, detailLen) || !readString(file, detailLen, entry.detail)) {
            error = "日志文件在读取第 " + std::to_string(i + 1) + " 条记录时中断。";
            out.clear();
            return false;
        }
        entry.action = actionFromCode(code);
        entry.itemId = itemId;
        entry.timestamp = static_cast<std::time_t>(ts);
        entry.detail = normalizeToUtf8(entry.detail);
        out.push_back(std::move(entry));
    }
    return true;
}

bool Repository::saveHistory(const std::vector<HistoryEntry>& entries, std::string& error) {
    const std::string tmpPath = historyPath_ + ".tmp";
    {
        std::ofstream file(widen(tmpPath), std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "无法创建日志文件：" + tmpPath;
            return false;
        }
        const std::uint64_t count = static_cast<std::uint64_t>(entries.size());
        writePod(file, count);

        for (const HistoryEntry& entry : entries) {
            const std::string code = actionCode(entry.action);
            const std::uint32_t codeLen = static_cast<std::uint32_t>(code.size());
            const std::int32_t itemId = entry.itemId;
            const std::int64_t ts = static_cast<std::int64_t>(entry.timestamp);
            const std::uint32_t detailLen = static_cast<std::uint32_t>(entry.detail.size());

            writePod(file, codeLen);
            file.write(code.data(), codeLen);
            writePod(file, itemId);
            writePod(file, ts);
            writePod(file, detailLen);
            if (detailLen > 0) file.write(entry.detail.data(), detailLen);
        }
        file.flush();
        if (!file) {
            error = "写入日志文件失败。";
            return false;
        }
    }

    _wremove(widen(historyPath_).c_str());
    if (_wrename(widen(tmpPath).c_str(), widen(historyPath_).c_str()) != 0) {
        error = "日志文件替换失败：" + historyPath_;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  CSV 导出
// ---------------------------------------------------------------------------
bool Repository::exportCsv(const std::string& path, const std::vector<Item>& items,
                           std::string& error) {
    std::ofstream file(widen(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "无法创建文件：" + path;
        return false;
    }
    // UTF-8 BOM：Excel 才能正确识别编码，否则中文全是乱码
    file << "\xEF\xBB\xBF";
    file << "编号,名称,库存,价格,库存金额\n";
    for (const Item& item : items) {
        file << item.id << ',' << csvEscape(item.name) << ',' << item.quantity << ','
             << formatMoney(item.price) << ',' << formatMoney(item.totalValue()) << '\n';
    }
    file.flush();
    if (!file) {
        error = "写入文件失败（磁盘空间不足或文件被占用）。";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  CSV 导入
// ---------------------------------------------------------------------------
bool Repository::importCsv(const std::string& path, std::vector<Item>& out, ImportStats& stats,
                           std::string& error) {
    out.clear();
    stats = ImportStats();

    std::ifstream file(widen(path), std::ios::binary);
    if (!file) {
        error = "文件不存在或无法打开：" + path;
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    if (content.empty()) {
        error = "文件内容为空。";
        return false;
    }

    // 去掉 UTF-8 BOM
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    std::size_t lineNo = 0;
    std::size_t pos = 0;
    std::vector<std::string> fields;

    while (pos <= content.size()) {
        std::size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string line = content.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trimCopy(line).empty()) continue;

        ++stats.totalLines;

        if (!parseCsvLine(line, fields)) {
            ++stats.malformed;
            if (stats.errors.size() < 5) {
                stats.errors.push_back("第 " + std::to_string(lineNo) + " 行：引号未闭合");
            }
            continue;
        }

        // 跳过表头（首字段不是纯数字）
        if (!isAllDigits(trimCopy(fields.empty() ? std::string() : fields[0]))) {
            if (stats.totalLines == 1) {
                --stats.totalLines;
                continue;
            }
            ++stats.malformed;
            if (stats.errors.size() < 5) {
                stats.errors.push_back("第 " + std::to_string(lineNo) + " 行：编号不是数字");
            }
            continue;
        }

        if (fields.size() < 4) {
            ++stats.malformed;
            if (stats.errors.size() < 5) {
                stats.errors.push_back("第 " + std::to_string(lineNo) + " 行：字段数不足 4 列");
            }
            continue;
        }

        Item item;
        try {
            item.id = std::stoi(trimCopy(fields[0]));
            item.name = normalizeToUtf8(trimCopy(fields[1]));
            item.quantity = std::stoi(trimCopy(fields[2]));
            item.price = std::stod(trimCopy(fields[3]));
        } catch (...) {
            ++stats.malformed;
            if (stats.errors.size() < 5) {
                stats.errors.push_back("第 " + std::to_string(lineNo) + " 行：数字格式错误");
            }
            continue;
        }

        if (item.id <= 0 || item.quantity < 0 || item.price <= 0.0 || item.name.empty()) {
            ++stats.malformed;
            if (stats.errors.size() < 5) {
                stats.errors.push_back("第 " + std::to_string(lineNo) +
                                       " 行：编号需 > 0、库存需 >= 0、价格需 > 0、名称不能为空");
            }
            continue;
        }

        out.push_back(std::move(item));
        ++stats.parsed;
    }

    if (stats.parsed == 0) {
        error = "没有解析出任何有效记录。";
        return false;
    }
    return true;
}

}  // namespace wh
