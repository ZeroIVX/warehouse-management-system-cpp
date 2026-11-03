// =============================================================================
//  bench.cpp —— 性能基准（对照 v1 的实现方式，量化优化收益）
//
//  对照口径：
//    基线 = v1 的写法：std::vector + 线性遍历 + 无索引
//    优化 = v2 的写法：哈希表主索引 + AVL 有序索引 + 预编译 KMP
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "wh/algorithms.h"
#include "wh/avl_tree.h"
#include "wh/console.h"
#include "wh/hash_table.h"
#include "wh/item.h"
#include "wh/text_util.h"

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

volatile long long g_sink = 0;  // 防止编译器优化掉被测代码

std::string fmt(double v, int decimals = 3) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

std::vector<wh::Item> makeItems(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> qty(0, 1000);
    std::uniform_real_distribution<double> price(1.0, 999.0);
    static const char* kPrefix[] = {"内六角螺丝刀", "一字螺丝刀", "活动扳手", "梅花扳手",
                                    "十字螺丝",   "自攻螺丝",   "六角螺母", "平垫圈"};
    std::vector<wh::Item> items;
    items.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        wh::Item item;
        item.id = i + 1;
        item.name = std::string(kPrefix[i % 8]) + std::to_string(i);
        item.quantity = qty(rng);
        item.price = price(rng);
        items.push_back(item);
    }
    return items;
}

void row(const std::string& label, double baseline, double optimized, int ops) {
    const double speedup = optimized > 1e-9 ? baseline / optimized : 0.0;
    std::cout << "  " << wh::padRight(label, 36) << wh::padLeft(fmt(baseline), 11)
              << wh::padLeft(fmt(optimized), 11) << wh::padLeft(fmt(speedup, 1) + "x", 11);
    if (ops > 0) {
        // 单次操作耗时（纳秒），比总耗时更能说明复杂度差异
        const double nsBase = baseline * 1e6 / static_cast<double>(ops);
        const double nsOpt = optimized * 1e6 / static_cast<double>(ops);
        std::cout << "   " << wh::padLeft(fmt(nsBase, 0) + " → " + fmt(nsOpt, 0) + " ns", 22);
    }
    std::cout << "\n";
}

void header(const std::string& title) {
    std::cout << "\n" << title << "\n";
    std::cout << "  " << wh::padRight("测试项", 36) << wh::padLeft("v1 (ms)", 11)
              << wh::padLeft("v2 (ms)", 11) << wh::padLeft("加速比", 11) << "   单次耗时\n";
    std::cout << "  " << std::string(100, '-') << "\n";
}

// 多方案横向对比用（不强调"加速比"，只给相对基线的比值）
void algoRow(const std::string& label, double ms, double baselineMs) {
    const double rel = ms > 1e-9 ? baselineMs / ms : 0.0;
    std::cout << "  " << wh::padRight(label, 38) << wh::padLeft(fmt(ms), 12)
              << wh::padLeft(fmt(rel, 2) + "x", 12) << "\n";
}

// ---------------------------------------------------------------------------
//  1. 按 ID 查询：三组规模保持相同的线性扫描总工作量（约 2×10^8 次比较），
//     从而直观展示「规模越大，O(1) 相对 O(n) 的优势越显著」
// ---------------------------------------------------------------------------
void benchLookup(int n, int queries) {
    const std::vector<wh::Item> items = makeItems(n, 11);
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> pick(1, n);

    std::vector<int> targets(static_cast<std::size_t>(queries));
    for (auto& t : targets) t = pick(rng);

    // v1：vector 线性扫描
    auto t0 = Clock::now();
    long long found1 = 0;
    for (int target : targets) {
        for (const wh::Item& item : items) {
            if (item.id == target) {
                ++found1;
                break;
            }
        }
    }
    const double base = elapsedMs(t0);

    // v2：哈希表 O(1)
    wh::HashTable<int, std::size_t> index;
    for (std::size_t i = 0; i < items.size(); ++i) index.insert(items[i].id, i);

    t0 = Clock::now();
    long long found2 = 0;
    for (int target : targets) {
        if (index.find(target)) ++found2;
    }
    const double opt = elapsedMs(t0);

    g_sink += found1 + found2;

    char label[96];
    std::snprintf(label, sizeof(label), "按ID查询 %d 次 (N=%d)", queries, n);
    row(label, base, opt, queries);
}

// ---------------------------------------------------------------------------
//  2. 构建索引（查重开销）
// ---------------------------------------------------------------------------
void benchBuild(int n) {
    const std::vector<wh::Item> items = makeItems(n, 13);

    // v1：追加前线性查重 O(n^2)
    auto t0 = Clock::now();
    std::vector<wh::Item> plain;
    for (const wh::Item& item : items) {
        bool dup = false;
        for (const wh::Item& existing : plain) {
            if (existing.id == item.id) {
                dup = true;
                break;
            }
        }
        if (!dup) plain.push_back(item);
    }
    const double base = elapsedMs(t0);

    // v2：哈希表查重 O(n)
    t0 = Clock::now();
    wh::HashTable<int, wh::Item> table;
    for (const wh::Item& item : items) table.insert(item.id, item);
    const double opt = elapsedMs(t0);

    g_sink += static_cast<long long>(plain.size() + table.size());

    char label[96];
    std::snprintf(label, sizeof(label), "构建索引 N=%d", n);
    row(label, base, opt, n);
}

// ---------------------------------------------------------------------------
//  3. 有序输出
// ---------------------------------------------------------------------------
void benchSortedOutput(int n) {
    const std::vector<wh::Item> items = makeItems(n, 17);
    auto byPrice = [](const wh::Item& a, const wh::Item& b) { return a.price < b.price; };

    // 基线：直接对元素排序（v1 若要做排序只能这么写）
    auto t0 = Clock::now();
    std::vector<wh::Item> byStdSort = items;
    std::sort(byStdSort.begin(), byStdSort.end(), byPrice);
    const double stdSortMs = elapsedMs(t0);

    // 以下均为「排下标」：Item 内含 std::string，直接排序会产生大对象搬移
    auto indexOrder = [&items]() {
        std::vector<std::size_t> order(items.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        return order;
    };

    t0 = Clock::now();
    std::vector<std::size_t> quick = indexOrder();
    wh::quickSort(quick.begin(), quick.end(),
                  [&items](std::size_t a, std::size_t b) { return items[a].price < items[b].price; });
    const double quickMs = elapsedMs(t0);

    t0 = Clock::now();
    std::vector<std::size_t> merge = indexOrder();
    wh::mergeSort(merge, [&items](std::size_t a, std::size_t b) {
        return items[a].price < items[b].price;
    });
    const double mergeMs = elapsedMs(t0);

    t0 = Clock::now();
    std::vector<std::size_t> heap = indexOrder();
    wh::heapSort(heap, [&items](std::size_t a, std::size_t b) {
        return items[a].price < items[b].price;
    });
    const double heapMs = elapsedMs(t0);

    // AVL：建一次索引，之后每次「排序」只是中序遍历
    t0 = Clock::now();
    wh::AVLTree<double, std::size_t> priceIndex;
    for (std::size_t i = 0; i < items.size(); ++i) priceIndex.insert(items[i].price, i);
    const double buildMs = elapsedMs(t0);

    t0 = Clock::now();
    std::size_t count = 0;
    priceIndex.inOrder([&count](double, const std::vector<std::size_t>&) { ++count; });
    const double traverseMs = elapsedMs(t0);

    g_sink += static_cast<long long>(count + byStdSort.size() + quick.size() + merge.size() +
                                     heap.size());

    std::cout << "  N = " << n << "\n";
    std::cout << "  " << wh::padRight("方案", 38) << wh::padLeft("耗时 (ms)", 12)
              << wh::padLeft("相对基线", 12) << "\n";
    std::cout << "  " << std::string(100, '-') << "\n";
    algoRow("std::sort 排序元素（基线）", stdSortMs, stdSortMs);
    algoRow("手写快排 · 排下标", quickMs, stdSortMs);
    algoRow("手写归并 · 排下标（稳定）", mergeMs, stdSortMs);
    algoRow("手写堆排 · 排下标", heapMs, stdSortMs);
    algoRow("AVL 中序（索引已建好）", traverseMs, stdSortMs);
    std::cout << "  " << wh::padRight("  └ AVL 建索引一次性成本", 38)
              << wh::padLeft(fmt(buildMs), 12) << " ms\n\n";
}

// ---------------------------------------------------------------------------
//  4. 区间查询
// ---------------------------------------------------------------------------
void benchRangeQuery(int n, int queries) {
    const std::vector<wh::Item> items = makeItems(n, 19);

    wh::AVLTree<double, std::size_t> priceIndex;
    for (std::size_t i = 0; i < items.size(); ++i) priceIndex.insert(items[i].price, i);

    std::mt19937 rng(23);
    std::uniform_real_distribution<double> low(1.0, 500.0);
    std::vector<std::pair<double, double>> ranges;
    ranges.reserve(static_cast<std::size_t>(queries));
    for (int i = 0; i < queries; ++i) {
        const double lo = low(rng);
        ranges.emplace_back(lo, lo + 40.0);
    }

    auto t0 = Clock::now();
    long long hit1 = 0;
    for (const auto& r : ranges) {
        for (const wh::Item& item : items) {
            if (item.price >= r.first && item.price <= r.second) ++hit1;
        }
    }
    const double base = elapsedMs(t0);

    t0 = Clock::now();
    long long hit2 = 0;
    for (const auto& r : ranges) {
        priceIndex.rangeQuery(r.first, r.second,
                              [&hit2](double, const std::vector<std::size_t>& bucket) {
                                  hit2 += static_cast<long long>(bucket.size());
                              });
    }
    const double opt = elapsedMs(t0);

    g_sink += hit1 + hit2;

    char label[96];
    std::snprintf(label, sizeof(label), "价格区间查询 %d 次 (N=%d)", queries, n);
    row(label, base, opt, queries);
}

// ---------------------------------------------------------------------------
//  5. 名称模糊搜索
// ---------------------------------------------------------------------------
void benchNameSearch(int n, int queries) {
    const std::vector<wh::Item> items = makeItems(n, 29);
    const std::vector<std::string> keywords{"螺丝", "扳手", "螺母", "垫圈", "不存在的关键词"};

    // 模式预编译一次，在 N 条商品名之间复用
    std::vector<wh::TextMatcher> matchers;
    matchers.reserve(keywords.size());
    for (const std::string& kw : keywords) matchers.emplace_back(kw);

    auto t0 = Clock::now();
    long long hit1 = 0;
    for (int q = 0; q < queries; ++q) {
        const std::string& kw = keywords[static_cast<std::size_t>(q) % keywords.size()];
        for (const wh::Item& item : items) {
            if (wh::naiveFind(item.name, kw) != std::string::npos) ++hit1;
        }
    }
    const double base = elapsedMs(t0);

    t0 = Clock::now();
    long long hit2 = 0;
    for (int q = 0; q < queries; ++q) {
        const wh::TextMatcher& matcher = matchers[static_cast<std::size_t>(q) % matchers.size()];
        for (const wh::Item& item : items) {
            if (matcher.contains(item.name)) ++hit2;
        }
    }
    const double opt = elapsedMs(t0);

    g_sink += hit1 + hit2;

    char label[96];
    std::snprintf(label, sizeof(label), "名称模糊搜索 %d 轮 (N=%d)", queries, n);
    row(label, base, opt, queries * n);
}

// ---------------------------------------------------------------------------
//  6. AVL 高度验证
// ---------------------------------------------------------------------------
void benchAvlHeight() {
    std::cout << "\n  有序键插入后的树高（普通 BST 会退化成链表）\n";
    std::cout << "  " << wh::padRight("元素数 N", 20) << wh::padLeft("AVL 树高", 10)
              << wh::padLeft("log2(N)", 10) << wh::padLeft("链式最坏", 12) << "\n";
    std::cout << "  " << std::string(100, '-') << "\n";

    for (int n : {100, 1000, 10000, 100000}) {
        wh::AVLTree<int, int> tree;
        for (int i = 0; i < n; ++i) tree.insert(i, i);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", n);
        std::cout << "  " << wh::padRight(buf, 20) << wh::padLeft(std::to_string(tree.height()), 10)
                  << wh::padLeft(fmt(std::log2(static_cast<double>(n)), 2), 10)
                  << wh::padLeft(std::to_string(n), 12) << "\n";
        g_sink += tree.height();
    }
}

// ---------------------------------------------------------------------------
//  5b. KMP 最坏情况：长文本 + 高重复模式
//      朴素算法每次失配都要重头比，退化为 O(n·m)；KMP 的文本指针永不回退
// ---------------------------------------------------------------------------
void benchKmpWorstCase(int repeats) {
    const std::string text(20000, 'a');
    const std::string pattern = std::string(1000, 'a') + "b";
    const wh::TextMatcher matcher(pattern);

    auto t0 = Clock::now();
    std::size_t r1 = 0;
    for (int i = 0; i < repeats; ++i) r1 += wh::naiveFind(text, pattern);
    const double base = elapsedMs(t0);

    t0 = Clock::now();
    bool r2 = false;
    for (int i = 0; i < repeats; ++i) r2 = matcher.contains(text);
    const double opt = elapsedMs(t0);

    g_sink += static_cast<long long>(r1) + (r2 ? 1 : 0);

    char label[96];
    std::snprintf(label, sizeof(label), "最坏情况 %d 轮 (文本20KB/模式1KB)", repeats);
    row(label, base, opt, repeats);
}

}  // namespace

int main() {
    wh::ConsoleGuard guard;
    std::cout << "=== 仓库管理系统 v2 性能基准 ===\n";
    std::cout << "（基线 = v1 的 vector + 线性扫描；优化 = v2 的哈希表 + AVL + 自适应匹配器）\n";

    header("1. 按编号查询（线性扫描 vs 哈希表）");
    benchLookup(1000, 200000);
    benchLookup(10000, 20000);
    benchLookup(100000, 2000);

    header("2. 构建索引（线性查重 vs 哈希查重）");
    benchBuild(5000);
    benchBuild(20000);

    header("3. 有序输出（四种方案对照，基线为 std::sort 排序元素）");
    benchSortedOutput(50000);
    benchSortedOutput(200000);

    header("4. 价格区间查询（全表扫描 vs AVL 范围查询）");
    benchRangeQuery(10000, 500);
    benchRangeQuery(100000, 200);

    header("5a. 名称模糊搜索 · 真实商品名（短模式，自适应走朴素匹配）");
    benchNameSearch(20000, 20);

    header("5b. 名称模糊搜索 · 最坏情况（长文本 + 高重复模式）");
    benchKmpWorstCase(3);

    benchAvlHeight();

    std::cout << "\n（校验和 " << g_sink << "，用于防止编译器过度优化）\n";
    return 0;
}
