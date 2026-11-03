// =============================================================================
//  service.h —— 业务服务层
//
//  三个索引同时维护，把原版「一切靠线性扫描」改为「按访问路径选最优结构」：
//    byId_    哈希表  int -> Item      按编号增删改查 O(1)
//    byPrice_ AVL     price -> [id]    按价格排序/区间查询 O(n) / O(log n + k)
//    byStock_ AVL     stock -> [id]    低库存告警、按库存排序
//
//  数据一致性：任何写操作都必须同步更新三个索引，selfCheck() 用于验证不变量。
//  持久化策略：写操作只改内存，由调用方在合适的时机 flush()，避免逐条落盘的 O(n²) 抖动。
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "wh/avl_tree.h"
#include "wh/hash_table.h"
#include "wh/history.h"
#include "wh/item.h"
#include "wh/linked_list.h"
#include "wh/repository.h"

namespace wh {

struct Result {
    bool ok = true;
    std::string message;

    static Result success(std::string msg = std::string()) { return Result{true, std::move(msg)}; }
    static Result failure(std::string msg) { return Result{false, std::move(msg)}; }
    explicit operator bool() const noexcept { return ok; }
};

// 列表结果附带「本次走了哪条算法路径」的说明，便于演示时讲清楚复杂度来源
struct Listing {
    std::vector<Item> items;
    std::string strategy;
};

enum class OverwritePolicy { Skip, Overwrite };

struct ImportOutcome {
    bool ok = false;
    std::size_t inserted = 0;
    std::size_t overwritten = 0;
    std::size_t skipped = 0;
    std::string error;
};

class WarehouseService {
public:
    explicit WarehouseService(Repository& repository, std::size_t historyCapacity = 2000);

    // 加载数据并构建三套索引
    bool initialize(std::string& error);

    // ---------------- 写操作 ----------------
    Result add(const Item& item);
    Result update(int id, const ItemPatch& patch);
    Result remove(int id);

    ImportOutcome importItems(const std::vector<Item>& parsed, OverwritePolicy policy);

    // ---------------- 读操作 ----------------
    const Item* findById(int id) const;
    std::size_t size() const { return byId_.size(); }
    bool empty() const { return byId_.empty(); }

    std::vector<Item> allItems() const;
    Listing list(SortField field, bool ascending) const;
    std::vector<Item> searchByName(const std::string& keyword) const;
    std::vector<Item> priceRange(double low, double high) const;
    std::vector<Item> lowStock(int threshold) const;

    double totalInventoryValue() const;

    // ---------------- 日志 ----------------
    void log(Action action, int itemId, const std::string& detail = std::string());
    std::vector<HistoryEntry> recentHistory(std::size_t n) const;
    std::size_t historySize() const { return history_.size(); }

    // ---------------- 持久化 ----------------
    bool flush(std::string& error);
    bool exportItems(const std::string& path, std::string& error, std::size_t& exported) const;

    // ---------------- 诊断 ----------------
    std::size_t hashBuckets() const { return byId_.bucketCount(); }
    std::size_t hashMaxChain() const { return byId_.maxBucketLength(); }
    std::size_t priceIndexNodes() const { return byPrice_.nodeCount(); }
    std::size_t priceIndexHeight() const { return byPrice_.height(); }
    std::size_t stockIndexNodes() const { return byStock_.nodeCount(); }
    std::size_t stockIndexHeight() const { return byStock_.height(); }
    bool selfCheck(std::string& report) const;

    const std::string& itemsPath() const { return repository_.itemsPath(); }
    const std::string& historyPath() const { return repository_.historyPath(); }

private:
    void indexAdd(const Item& item);
    void indexRemove(const Item& item);

    Repository& repository_;
    HashTable<int, Item> byId_;
    AVLTree<double, int> byPrice_;
    AVLTree<int, int> byStock_;
    DoublyLinkedList<HistoryEntry> history_;
};

}  // namespace wh
