// =============================================================================
//  console_ui.h —— 表现层
//
//  只负责交互与渲染，不含任何业务规则；所有数据操作通过 WarehouseService 完成。
// =============================================================================
#pragma once

#include <vector>

#include "wh/item.h"
#include "wh/service.h"

namespace wh {

class ConsoleApp {
public:
    explicit ConsoleApp(WarehouseService& service) : service_(service) {}

    // 返回进程退出码
    int run();

private:
    void renderMenu() const;
    void renderItems(const std::vector<Item>& items) const;
    void renderSummary() const;

    void handleAdd();
    void handleList();
    void handleSearch();
    void handlePriceRange();
    void handleLowStock();
    void handleModify();
    void handleRemove();
    void handleHistory();
    void handleExport();
    void handleImport();
    void handleDiagnostics();

    // 写操作成功后统一落盘
    void persistOrWarn();

    WarehouseService& service_;
    int historyPreview_ = 20;
};

}  // namespace wh
