#include "wh/service.h"

#include <algorithm>
#include <ctime>
#include <utility>

#include "wh/algorithms.h"
#include "wh/text_util.h"

namespace wh {
namespace {

constexpr int kMinId = 1;
constexpr double kMaxPrice = 1e9;
constexpr int kMaxQuantity = 1000000000;

constexpr int kMaxNameLength = 60;  // 按显示列计，中文算 2 列

std::string validateItem(const Item& item) {
    if (item.id < kMinId) return "商品编号必须为正整数。";
    if (item.name.empty()) return "商品名称不能为空。";
    if (displayWidth(item.name) > kMaxNameLength) {
        return "商品名称过长（最多 " + std::to_string(kMaxNameLength) + " 列宽）。";
    }
    if (item.quantity < 0 || item.quantity > kMaxQuantity) return "库存数量超出允许范围。";
    if (item.price <= 0.0 || item.price > kMaxPrice) return "单价必须大于 0 且在合理范围内。";
    return std::string();
}

}  // namespace

WarehouseService::WarehouseService(Repository& repository, std::size_t historyCapacity)
    : repository_(repository) {
    history_.setCapacity(historyCapacity);
}

// ---------------------------------------------------------------------------
//  初始化
// ---------------------------------------------------------------------------
bool WarehouseService::initialize(std::string& error) {
    std::vector<Item> items;
    bool legacy = false;
    if (!repository_.loadItems(items, error, legacy)) {
        // 文件损坏时不阻塞启动：保留可读部分，由调用方提示用户
        if (items.empty()) {
            items.clear();
        }
    }

    for (const Item& item : items) {
        if (byId_.contains(item.id)) continue;  // 容错：跳过重复编号
        byId_.insert(item.id, item);
        byPrice_.insert(item.price, item.id);
        byStock_.insert(item.quantity, item.id);
    }

    std::vector<HistoryEntry> entries;
    std::string historyError;
    if (!repository_.loadHistory(entries, historyError)) {
        // 日志读取失败不影响主流程
    }
    for (const HistoryEntry& e : entries) history_.pushBack(e);

    // 旧格式迁移：立即按新格式回写，避免下次还要走兼容分支
    if (legacy && !items.empty()) {
        std::string saveError;
        repository_.saveItems(items, saveError);
    }

    return byId_.size() == items.size() || items.empty();
}

// ---------------------------------------------------------------------------
//  索引维护
// ---------------------------------------------------------------------------
void WarehouseService::indexAdd(const Item& item) {
    byPrice_.insert(item.price, item.id);
    byStock_.insert(item.quantity, item.id);
}

void WarehouseService::indexRemove(const Item& item) {
    byPrice_.erase(item.price, item.id);
    byStock_.erase(item.quantity, item.id);
}

// ---------------------------------------------------------------------------
//  写操作
// ---------------------------------------------------------------------------
Result WarehouseService::add(const Item& item) {
    const std::string invalid = validateItem(item);
    if (!invalid.empty()) return Result::failure(invalid);

    if (byId_.contains(item.id)) {
        return Result::failure("编号 " + std::to_string(item.id) + " 已存在。");
    }

    byId_.insert(item.id, item);
    indexAdd(item);
    log(Action::Add, item.id, item.name);
    return Result::success("已新增商品「" + item.name + "」");
}

Result WarehouseService::update(int id, const ItemPatch& patch) {
    Item* existing = byId_.find(id);
    if (!existing) return Result::failure("编号 " + std::to_string(id) + " 不存在。");
    if (patch.empty()) return Result::failure("没有任何字段被修改。");

    // 先摘掉旧索引项，改完再按新值挂回去
    const Item before = *existing;
    Item after = before;
    if (patch.hasName) after.name = patch.name;
    if (patch.hasQuantity) after.quantity = patch.quantity;
    if (patch.hasPrice) after.price = patch.price;

    const std::string invalid = validateItem(after);
    if (!invalid.empty()) return Result::failure(invalid);

    indexRemove(before);
    *existing = after;
    indexAdd(after);
    log(Action::Modify, id, after.name);
    return Result::success("已更新商品「" + after.name + "」");
}

Result WarehouseService::remove(int id) {
    Item* existing = byId_.find(id);
    if (!existing) return Result::failure("编号 " + std::to_string(id) + " 不存在。");

    const Item snapshot = *existing;
    indexRemove(snapshot);
    byId_.erase(id);
    log(Action::Remove, id, snapshot.name);
    return Result::success("已删除商品「" + snapshot.name + "」");
}

// ---------------------------------------------------------------------------
//  批量导入
// ---------------------------------------------------------------------------
ImportOutcome WarehouseService::importItems(const std::vector<Item>& parsed,
                                            OverwritePolicy policy) {
    ImportOutcome outcome;
    for (const Item& item : parsed) {
        if (!validateItem(item).empty()) {
            ++outcome.skipped;
            continue;
        }
        Item* existing = byId_.find(item.id);
        if (existing) {
            if (policy == OverwritePolicy::Skip) {
                ++outcome.skipped;
                continue;
            }
            indexRemove(*existing);
            *existing = item;
            indexAdd(item);
            ++outcome.overwritten;
        } else {
            byId_.insert(item.id, item);
            indexAdd(item);
            ++outcome.inserted;
        }
    }
    log(Action::Import, 0,
        "新增 " + std::to_string(outcome.inserted) + " 条，覆盖 " +
            std::to_string(outcome.overwritten) + " 条，跳过 " +
            std::to_string(outcome.skipped) + " 条");
    outcome.ok = true;
    return outcome;
}

// ---------------------------------------------------------------------------
//  读操作
// ---------------------------------------------------------------------------
const Item* WarehouseService::findById(int id) const { return byId_.find(id); }

std::vector<Item> WarehouseService::allItems() const {
    std::vector<Item> out;
    out.reserve(byId_.size());
    byId_.forEach([&out](int, const Item& item) { out.push_back(item); });
    return out;
}

Listing WarehouseService::list(SortField field, bool ascending) const {
    Listing result;

    // 有索引的字段：直接中序遍历索引，O(n) 拿到有序序列，无需比较排序
    if (field == SortField::Price || field == SortField::Quantity) {
        std::vector<int> ids;
        ids.reserve(byId_.size());
        if (field == SortField::Price) {
            byPrice_.inOrder([&ids](double, const std::vector<int>& bucket) {
                ids.insert(ids.end(), bucket.begin(), bucket.end());
            });
        } else {
            byStock_.inOrder([&ids](int, const std::vector<int>& bucket) {
                ids.insert(ids.end(), bucket.begin(), bucket.end());
            });
        }
        if (!ascending) std::reverse(ids.begin(), ids.end());

        result.items.reserve(ids.size());
        for (int id : ids) {
            if (const Item* item = byId_.find(id)) result.items.push_back(*item);
        }
        result.strategy = "AVL 索引中序遍历 O(n)（不需比较排序）";
        return result;
    }

    // 无索引字段：排序「下标」而不是元素本身。
    // Item 内含 std::string，直接排序会产生大量大对象搬移；排下标只需交换 8 字节整数。
    std::vector<Item> items = allItems();
    std::vector<std::size_t> order(items.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;

    if (field == SortField::Id) {
        quickSort(order.begin(), order.end(), [&items](std::size_t a, std::size_t b) {
            return items[a].id < items[b].id;
        });
        if (!ascending) std::reverse(order.begin(), order.end());
        result.strategy = "下标快速排序 O(n log n)，三数取中 + 三路分区";
    } else {
        // 名称：用稳定排序，保证同名商品维持原有相对顺序
        mergeSort(order, [&items, ascending](std::size_t a, std::size_t b) {
            return ascending ? (items[a].name < items[b].name) : (items[b].name < items[a].name);
        });
        result.strategy = "下标归并排序 O(n log n)，稳定排序";
    }

    result.items.reserve(items.size());
    for (std::size_t idx : order) result.items.push_back(items[idx]);
    return result;
}

std::vector<Item> WarehouseService::searchByName(const std::string& keyword) const {
    std::vector<Item> hits;
    if (keyword.empty()) return hits;

    // 模式只编译一次，在 N 条商品名之间复用（每次重建 lps 会被堆分配拖慢数倍）
    const TextMatcher matcher(keyword);
    byId_.forEach([&](int, const Item& item) {
        if (matcher.contains(item.name)) hits.push_back(item);
    });
    std::sort(hits.begin(), hits.end(), [](const Item& a, const Item& b) { return a.id < b.id; });
    return hits;
}

std::vector<Item> WarehouseService::priceRange(double low, double high) const {
    std::vector<Item> out;
    if (low > high) std::swap(low, high);
    byPrice_.rangeQuery(low, high, [&](double, const std::vector<int>& bucket) {
        for (int id : bucket) {
            if (const Item* item = byId_.find(id)) out.push_back(*item);
        }
    });
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.price < b.price; });
    return out;
}

std::vector<Item> WarehouseService::lowStock(int threshold) const {
    std::vector<Item> out;
    if (threshold < 0) threshold = 0;
    byStock_.rangeQuery(0, threshold, [&](int, const std::vector<int>& bucket) {
        for (int id : bucket) {
            if (const Item* item = byId_.find(id)) out.push_back(*item);
        }
    });
    std::sort(out.begin(), out.end(),
              [](const Item& a, const Item& b) { return a.quantity < b.quantity; });
    return out;
}

double WarehouseService::totalInventoryValue() const {
    double sum = 0.0;
    byId_.forEach([&sum](int, const Item& item) { sum += item.totalValue(); });
    return sum;
}

// ---------------------------------------------------------------------------
//  日志
// ---------------------------------------------------------------------------
void WarehouseService::log(Action action, int itemId, const std::string& detail) {
    HistoryEntry entry;
    entry.action = action;
    entry.itemId = itemId;
    entry.timestamp = std::time(nullptr);
    entry.detail = detail;
    history_.pushBack(entry);
}

std::vector<HistoryEntry> WarehouseService::recentHistory(std::size_t n) const {
    return history_.takeRecent(n);
}

// ---------------------------------------------------------------------------
//  持久化
// ---------------------------------------------------------------------------
bool WarehouseService::flush(std::string& error) {
    const std::vector<Item> items = allItems();
    if (!repository_.saveItems(items, error)) return false;

    std::vector<HistoryEntry> entries;
    entries.reserve(history_.size());
    history_.forEach([&entries](const HistoryEntry& e) { entries.push_back(e); });
    return repository_.saveHistory(entries, error);
}

bool WarehouseService::exportItems(const std::string& path, std::string& error,
                                   std::size_t& exported) const {
    const Listing listing = list(SortField::Id, true);
    exported = listing.items.size();
    if (!Repository::exportCsv(path, listing.items, error)) return false;
    return true;
}

// ---------------------------------------------------------------------------
//  自检：验证三套索引与哈希表内容完全一致
// ---------------------------------------------------------------------------
bool WarehouseService::selfCheck(std::string& report) const {
    const std::size_t n = byId_.size();
    if (byPrice_.size() != n) {
        report = "价格索引元素数 " + std::to_string(byPrice_.size()) + " 与主索引 " +
                 std::to_string(n) + " 不一致";
        return false;
    }
    if (byStock_.size() != n) {
        report = "库存索引元素数 " + std::to_string(byStock_.size()) + " 与主索引 " +
                 std::to_string(n) + " 不一致";
        return false;
    }
    if (!byPrice_.validate()) {
        report = "价格 AVL 树结构失衡或 BST 序被破坏";
        return false;
    }
    if (!byStock_.validate()) {
        report = "库存 AVL 树结构失衡或 BST 序被破坏";
        return false;
    }

    bool consistent = true;
    std::string detail;
    byId_.forEach([&](int id, const Item& item) {
        if (!consistent) return;
        const std::vector<int>* priceBucket = byPrice_.find(item.price);
        if (!priceBucket ||
            std::find(priceBucket->begin(), priceBucket->end(), id) == priceBucket->end()) {
            consistent = false;
            detail = "编号 " + std::to_string(id) + " 未出现在价格索引中";
            return;
        }
        const std::vector<int>* stockBucket = byStock_.find(item.quantity);
        if (!stockBucket ||
            std::find(stockBucket->begin(), stockBucket->end(), id) == stockBucket->end()) {
            consistent = false;
            detail = "编号 " + std::to_string(id) + " 未出现在库存索引中";
        }
    });

    if (!consistent) {
        report = detail;
        return false;
    }

    report = "主索引 " + std::to_string(n) + " 条；价格 AVL 高度 " +
             std::to_string(byPrice_.height()) + "、节点 " +
             std::to_string(byPrice_.nodeCount()) + "；库存 AVL 高度 " +
             std::to_string(byStock_.height()) + "、节点 " +
             std::to_string(byStock_.nodeCount()) + "；哈希桶 " +
             std::to_string(byId_.bucketCount()) + "、最长链 " +
             std::to_string(byId_.maxBucketLength());
    return true;
}

}  // namespace wh
