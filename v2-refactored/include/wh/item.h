// =============================================================================
//  item.h —— 商品领域模型
// =============================================================================
#pragma once

#include <string>

namespace wh {

struct Item {
    int id = 0;            // 商品编号（业务主键，全局唯一）
    std::string name;      // 商品名称（UTF-8）
    int quantity = 0;      // 库存数量
    double price = 0.0;    // 单价（元）

    double totalValue() const { return price * static_cast<double>(quantity); }
};

// 排序字段：有索引的字段可走 AVL 中序（O(n)），无索引字段走手写排序（O(n log n)）
enum class SortField { Id, Name, Quantity, Price };

inline const char* sortFieldName(SortField f) {
    switch (f) {
        case SortField::Id: return "编号";
        case SortField::Name: return "名称";
        case SortField::Quantity: return "库存";
        case SortField::Price: return "价格";
    }
    return "未知";
}

// 部分更新：未设置的字段保持原值
struct ItemPatch {
    bool hasName = false;
    std::string name;
    bool hasQuantity = false;
    int quantity = 0;
    bool hasPrice = false;
    double price = 0.0;

    bool empty() const { return !hasName && !hasQuantity && !hasPrice; }
};

}  // namespace wh
