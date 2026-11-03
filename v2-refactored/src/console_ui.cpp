#include "wh/console_ui.h"

#include <cstdio>
#include <iostream>
#include <string>

#include "wh/console.h"
#include "wh/text_util.h"

namespace wh {
namespace {

// 表格列宽（单位：终端显示列）
constexpr std::size_t kColId = 8;
constexpr std::size_t kColName = 26;
constexpr std::size_t kColQty = 8;
constexpr std::size_t kColPrice = 12;
constexpr std::size_t kColValue = 14;

std::string formatTime(std::time_t ts) {
    std::tm tmv{};
#if defined(_MSC_VER)
    localtime_s(&tmv, &ts);
#else
    localtime_r(&ts, &tmv);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf);
}

}  // namespace

// ---------------------------------------------------------------------------
//  主循环
// ---------------------------------------------------------------------------
int ConsoleApp::run() {
    while (true) {
        renderMenu();
        auto choice = readInt("请选择操作：", 0, 11);
        if (!choice) {
            // 输入流被关闭（Ctrl+Z / 管道结束），同样要走保存流程
            std::string error;
            service_.flush(error);
            return 0;
        }

        switch (*choice) {
            case 1: handleAdd(); break;
            case 2: handleList(); break;
            case 3: handleSearch(); break;
            case 4: handlePriceRange(); break;
            case 5: handleLowStock(); break;
            case 6: handleModify(); break;
            case 7: handleRemove(); break;
            case 8: handleHistory(); break;
            case 9: handleExport(); break;
            case 10: handleImport(); break;
            case 11: handleDiagnostics(); break;
            case 0: {
                std::string error;
                if (!service_.flush(error)) {
                    printError("退出前保存失败：" + error);
                    pauseForEnter();
                    return 1;
                }
                clearScreen();
                printInfo("数据已保存，感谢使用。");
                return 0;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
//  渲染
// ---------------------------------------------------------------------------
void ConsoleApp::renderMenu() const {
    clearScreen();
    printTitle("=== 仓库管理系统 v2.0（哈希表 + AVL + 双向链表）===");
    printSeparator('-');

    renderSummary();

    std::cout << "\n";
    std::cout << "  1. 新增商品          2. 商品列表 / 排序\n";
    std::cout << "  3. 搜索商品(模糊)     4. 价格区间查询\n";
    std::cout << "  5. 低库存告警         6. 修改商品\n";
    std::cout << "  7. 删除商品           8. 操作日志\n";
    std::cout << "  9. 导出 CSV          10. 导入 CSV\n";
    std::cout << " 11. 索引诊断           0. 退出\n";
    std::cout << "\n";
}

void ConsoleApp::renderSummary() const {
    setColor(Color::Gray);
    std::cout << "商品 " << service_.size() << " 种 | 库存总值 "
              << formatMoney(service_.totalInventoryValue()) << " 元 | 日志 "
              << service_.historySize() << " 条 | 哈希桶 " << service_.hashBuckets()
              << "（最长链 " << service_.hashMaxChain() << "）| 价格树高 "
              << service_.priceIndexHeight() << "\n";
    setColor(Color::Default);
}

void ConsoleApp::renderItems(const std::vector<Item>& items) const {
    setColor(Color::Cyan);
    std::cout << padRight("编号", kColId) << padRight("名称", kColName)
              << padLeft("库存", kColQty) << padLeft("单价", kColPrice)
              << padLeft("库存金额", kColValue) << "\n";
    setColor(Color::Default);
    printSeparator('-');

    if (items.empty()) {
        printWarn("没有符合条件的商品。");
        return;
    }

    for (const Item& item : items) {
        std::cout << padRight(std::to_string(item.id), kColId)
                  << padRight(ellipsize(item.name, kColName - 1), kColName)
                  << padLeft(std::to_string(item.quantity), kColQty)
                  << padLeft(formatMoney(item.price), kColPrice)
                  << padLeft(formatMoney(item.totalValue()), kColValue) << "\n";
    }
    printSeparator('-');
    std::cout << "共 " << items.size() << " 条\n";
}

// ---------------------------------------------------------------------------
//  各功能
// ---------------------------------------------------------------------------
void ConsoleApp::handleAdd() {
    beginPage("=== 新增商品 ===");

    auto id = readInt("商品编号：", 1, 999999999);
    if (!id) return;
    if (service_.findById(*id)) {
        printError("编号 " + std::to_string(*id) + " 已存在。");
        pauseForEnter();
        return;
    }

    auto name = readLine("商品名称：");
    if (!name) return;

    auto quantity = readInt("库存数量：", 0, 1000000000);
    if (!quantity) return;

    auto price = readDouble("单价（元）：", 0.01, 1000000000.0);
    if (!price) return;

    Item item;
    item.id = *id;
    item.name = *name;
    item.quantity = *quantity;
    item.price = *price;

    const Result r = service_.add(item);
    if (r.ok) {
        persistOrWarn();
        printSuccess(r.message);
    } else {
        printError(r.message);
    }
    pauseForEnter();
}

void ConsoleApp::handleList() {
    beginPage("=== 商品列表 ===");
    std::cout << "排序字段：1.编号  2.名称  3.库存  4.价格\n";
    auto field = readInt("选择排序字段（直接回车按编号）：", 0, 4);
    if (!field) return;

    SortField sf = SortField::Id;
    if (*field == 2) sf = SortField::Name;
    else if (*field == 3) sf = SortField::Quantity;
    else if (*field == 4) sf = SortField::Price;

    auto order = readInt("排序方式：1.升序  2.降序（直接回车升序）：", 0, 2);
    if (!order) return;

    const Listing listing = service_.list(sf, *order != 2);
    clearScreen();
    printTitle("=== 商品列表 ===");
    printSeparator('-');
    printInfo(std::string("排序策略：") + listing.strategy);
    std::cout << "\n";
    renderItems(listing.items);
    pauseForEnter();
}

void ConsoleApp::handleSearch() {
    beginPage("=== 搜索商品（名称模糊匹配，KMP 算法）===");
    auto keyword = readLine("输入名称关键词（支持部分匹配）：");
    if (!keyword) return;

    const std::vector<Item> hits = service_.searchByName(*keyword);
    std::cout << "\n";
    renderItems(hits);
    pauseForEnter();
}

void ConsoleApp::handlePriceRange() {
    beginPage("=== 价格区间查询（AVL 范围查询 O(log n + k)）===");
    auto low = readDouble("最低价：", 0.0, 1000000000.0);
    if (!low) return;
    auto high = readDouble("最高价：", 0.0, 1000000000.0);
    if (!high) return;

    const std::vector<Item> hits = service_.priceRange(*low, *high);
    std::cout << "\n";
    renderItems(hits);
    pauseForEnter();
}

void ConsoleApp::handleLowStock() {
    beginPage("=== 低库存告警（库存 AVL 范围查询）===");
    auto threshold = readInt("库存阈值（小于等于该值视为需要补货）：", 0, 1000000000);
    if (!threshold) return;

    const std::vector<Item> hits = service_.lowStock(*threshold);
    std::cout << "\n";
    if (!hits.empty()) {
        setColor(Color::Yellow);
        std::cout << "以下 " << hits.size() << " 种商品需要补货：\n";
        setColor(Color::Default);
        std::cout << "\n";
    }
    renderItems(hits);
    pauseForEnter();
}

void ConsoleApp::handleModify() {
    beginPage("=== 修改商品 ===");
    auto id = readInt("要修改的商品编号：", 1, 999999999);
    if (!id) return;

    const Item* current = service_.findById(*id);
    if (!current) {
        printError("编号 " + std::to_string(*id) + " 不存在。");
        pauseForEnter();
        return;
    }

    std::cout << "\n当前信息：\n";
    const std::vector<Item> single{*current};
    renderItems(single);

    std::cout << "\n直接回车表示保持原值。\n";
    ItemPatch patch;

    auto name = readLine("新名称（当前：" + current->name + "）：");
    if (!name) return;
    if (!name->empty()) {
        patch.hasName = true;
        patch.name = *name;
    }

    auto qtyText = readLine("新库存（当前：" + std::to_string(current->quantity) + "）：");
    if (!qtyText) return;
    if (!qtyText->empty()) {
        auto parsed = parseInteger(*qtyText);
        if (!parsed || *parsed < 0 || *parsed > 1000000000) {
            parsed = readInt("  库存需为 0 ~ 1000000000 的整数，请重新输入：", 0, 1000000000);
            if (!parsed) return;
        }
        patch.hasQuantity = true;
        patch.quantity = *parsed;
    }

    auto priceText = readLine("新单价（当前：" + formatMoney(current->price) + "）：");
    if (!priceText) return;
    if (!priceText->empty()) {
        auto parsed = parseDouble(*priceText);
        if (!parsed || *parsed < 0.01 || *parsed > 1000000000.0) {
            parsed = readDouble("  单价需为 0.01 ~ 1000000000 之间的数字，请重新输入：", 0.01,
                                1000000000.0);
            if (!parsed) return;
        }
        patch.hasPrice = true;
        patch.price = *parsed;
    }

    const Result r = service_.update(*id, patch);
    if (r.ok) {
        persistOrWarn();
        printSuccess(r.message);
    } else {
        printError(r.message);
    }
    pauseForEnter();
}

void ConsoleApp::handleRemove() {
    beginPage("=== 删除商品 ===");
    auto id = readInt("要删除的商品编号：", 1, 999999999);
    if (!id) return;

    const Item* current = service_.findById(*id);
    if (!current) {
        printError("编号 " + std::to_string(*id) + " 不存在。");
        pauseForEnter();
        return;
    }

    const std::vector<Item> single{*current};
    renderItems(single);

    auto confirm = readConfirm("\n确认删除该商品？(y/n)：");
    if (!confirm) return;
    if (*confirm != 'y') {
        printWarn("已取消删除。");
        pauseForEnter();
        return;
    }

    const Result r = service_.remove(*id);
    if (r.ok) {
        persistOrWarn();
        printSuccess(r.message);
    } else {
        printError(r.message);
    }
    pauseForEnter();
}

void ConsoleApp::handleHistory() {
    beginPage("=== 操作日志（双向链表反向遍历，最新在前）===");
    const std::vector<HistoryEntry> entries =
        service_.recentHistory(static_cast<std::size_t>(historyPreview_));

    if (entries.empty()) {
        printWarn("暂无操作记录。");
        pauseForEnter();
        return;
    }

    setColor(Color::Cyan);
    std::cout << padRight("时间", 21) << padRight("操作", 8) << padLeft("编号", 8) << "  说明\n";
    setColor(Color::Default);
    printSeparator('-');

    for (const HistoryEntry& e : entries) {
        std::cout << padRight(formatTime(e.timestamp), 21) << padRight(actionName(e.action), 8)
                  << padLeft(e.itemId == 0 ? std::string("-") : std::to_string(e.itemId), 8)
                  << "  " << e.detail << "\n";
    }
    printSeparator('-');
    std::cout << "显示最近 " << entries.size() << " 条，共 " << service_.historySize() << " 条\n";
    pauseForEnter();
}

void ConsoleApp::handleExport() {
    beginPage("=== 导出 CSV ===");
    auto name = readLine("导出文件名（直接回车使用 warehouse_export.csv）：");
    if (!name) return;
    std::string file = name->empty() ? "warehouse_export.csv" : *name;
    if (file.find('.') == std::string::npos) file += ".csv";

    std::string error;
    std::size_t exported = 0;
    if (service_.exportItems(file, error, exported)) {
        service_.log(Action::Export, 0, file);
        persistOrWarn();
        printSuccess("已导出 " + std::to_string(exported) + " 条记录到 " + file);
        std::cout << "（UTF-8 BOM 编码，可直接用 Excel 打开）\n";
    } else {
        printError(error);
    }
    pauseForEnter();
}

void ConsoleApp::handleImport() {
    beginPage("=== 导入 CSV ===");
    auto name = readLine("要导入的文件名：");
    if (!name || name->empty()) return;

    std::vector<Item> parsed;
    ImportStats stats;
    std::string error;
    if (!Repository::importCsv(*name, parsed, stats, error)) {
        printError(error);
        pauseForEnter();
        return;
    }

    printInfo("解析到 " + std::to_string(stats.parsed) + " 条有效记录，跳过 " +
              std::to_string(stats.malformed) + " 条格式错误");
    for (const std::string& e : stats.errors) printWarn(e);

    std::cout << "\n遇到已存在的编号时：1.覆盖  2.跳过  3.取消导入\n";
    auto policy = readInt("请选择：", 1, 3);
    if (!policy || *policy == 3) {
        printWarn("已取消导入。");
        pauseForEnter();
        return;
    }

    const ImportOutcome outcome = service_.importItems(
        parsed, *policy == 1 ? OverwritePolicy::Overwrite : OverwritePolicy::Skip);
    persistOrWarn();

    std::cout << "\n";
    printSuccess("导入完成");
    std::cout << "  新增：" << outcome.inserted << " 条\n";
    std::cout << "  覆盖：" << outcome.overwritten << " 条\n";
    std::cout << "  跳过：" << outcome.skipped << " 条\n";
    pauseForEnter();
}

void ConsoleApp::handleDiagnostics() {
    beginPage("=== 索引诊断 ===");

    std::string report;
    const bool healthy = service_.selfCheck(report);

    std::cout << "主索引容量：" << service_.size() << " 条\n";
    std::cout << "  哈希表桶数：" << service_.hashBuckets() << "，最长链 "
              << service_.hashMaxChain() << "\n";
    std::cout << "  价格 AVL：高度 " << service_.priceIndexHeight() << "，节点 "
              << service_.priceIndexNodes() << "\n";
    std::cout << "  库存 AVL：高度 " << service_.stockIndexHeight() << "，节点 "
              << service_.stockIndexNodes() << "\n\n";

    if (healthy) {
        printSuccess("一致性校验通过");
        std::cout << "  " << report << "\n";
    } else {
        printError("一致性校验失败");
        std::cout << "  " << report << "\n";
    }

    std::cout << "\n数据文件：" << service_.itemsPath() << "\n";
    std::cout << "日志文件：" << service_.historyPath() << "\n";
    pauseForEnter();
}

void ConsoleApp::persistOrWarn() {
    std::string error;
    if (!service_.flush(error)) {
        printError("保存失败：" + error);
    }
}

}  // namespace wh
