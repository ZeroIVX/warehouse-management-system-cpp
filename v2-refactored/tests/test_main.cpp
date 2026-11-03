// =============================================================================
//  test_main.cpp —— 单元测试（零第三方依赖的轻量断言框架）
//
//  覆盖：哈希表 / AVL / 双向链表 / 排序与查找算法 / KMP / 文本排版 /
//        业务服务三索引一致性 / 持久化往返 / CSV 编解码
// =============================================================================
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "wh/algorithms.h"
#include "wh/avl_tree.h"
#include "wh/console.h"
#include "wh/encoding.h"
#include "wh/hash_table.h"
#include "wh/linked_list.h"
#include "wh/repository.h"
#include "wh/service.h"
#include "wh/text_util.h"

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_currentTest = "";

void reportFailure(const char* file, int line, const std::string& expr) {
    ++g_failures;
    std::cout << "    [失败] " << g_currentTest << " @ " << file << ":" << line << "  " << expr
              << "\n";
}

#define CHECK(cond)                                              \
    do {                                                         \
        ++g_checks;                                              \
        if (!(cond)) reportFailure(__FILE__, __LINE__, #cond);    \
    } while (false)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        ++g_checks;                                                           \
        const auto a_ = (actual);                                             \
        const auto e_ = (expected);                                           \
        if (!(a_ == e_)) {                                                    \
            std::ostringstream os_;                                           \
            os_ << #actual << " == " << #expected << "  实际值: " << a_;      \
            reportFailure(__FILE__, __LINE__, os_.str());                     \
        }                                                                     \
    } while (false)

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                        \
    void name();                                          \
    Registrar registrar_##name(#name, &name);             \
    void name()

// ---------------------------------------------------------------------------
//  哈希表
// ---------------------------------------------------------------------------
TEST(HashTableBasic) {
    wh::HashTable<int, std::string> table(4);
    CHECK(table.empty());
    CHECK(table.insert(1, "一"));
    CHECK(table.insert(2, "二"));
    CHECK(!table.insert(1, "壹"));  // 键已存在 -> 覆盖

    CHECK_EQ(table.size(), 2u);
    CHECK(table.contains(1));
    CHECK(!table.contains(3));
    CHECK_EQ(*table.find(1), std::string("壹"));
    CHECK(table.find(3) == nullptr);

    CHECK(table.erase(1));
    CHECK(!table.erase(1));
    CHECK_EQ(table.size(), 1u);
    CHECK(table.find(1) == nullptr);
}

TEST(HashTableGrowAndRehash) {
    // 用类型别名而不是直接写 HashTable<int, int>：宏参数里的逗号会被当成参数分隔符
    using IntTable = wh::HashTable<int, int>;
    IntTable table(2);
    constexpr int kN = 2000;
    for (int i = 0; i < kN; ++i) table.insert(i, i * 3);

    CHECK_EQ(table.size(), static_cast<std::size_t>(kN));
    CHECK(table.bucketCount() > 2);  // 确实扩容了
    CHECK(table.loadFactor() <= IntTable::kMaxLoadFactor + 1e-9);

    int missing = 0;
    for (int i = 0; i < kN; ++i) {
        const int* v = table.find(i);
        if (!v || *v != i * 3) ++missing;
    }
    CHECK_EQ(missing, 0);
    CHECK(table.maxBucketLength() < 32);  // 哈希函数未退化
}

TEST(HashTableDecadeStability) {
    // 连续整数在低位高度规律，验证取模分布仍然可用
    wh::HashTable<int, int> table;
    for (int i = 0; i < 5000; ++i) table.insert(i, i);
    const double avgChain = static_cast<double>(table.size()) /
                            static_cast<double>(table.bucketCount());
    // 平均链长应接近装载因子，最长链不应远大于平均值
    CHECK(table.maxBucketLength() <= static_cast<std::size_t>(avgChain * 4) + 4);
}

// ---------------------------------------------------------------------------
//  AVL 树
// ---------------------------------------------------------------------------
TEST(AvlSequentialInsertStaysBalanced) {
    wh::AVLTree<int, int> tree;
    constexpr int kN = 1024;
    for (int i = 0; i < kN; ++i) tree.insert(i, i);  // 递增插入，普通 BST 会退化成链

    CHECK_EQ(tree.size(), static_cast<std::size_t>(kN));
    CHECK_EQ(tree.nodeCount(), static_cast<std::size_t>(kN));
    CHECK(tree.validate());

    // AVL 高度上界：1.44 * log2(n + 2) ≈ 14.4
    const double bound = 1.44 * std::log2(static_cast<double>(kN) + 2.0);
    CHECK(static_cast<double>(tree.height()) <= bound + 1.0);
}

TEST(AvlInOrderIsSorted) {
    wh::AVLTree<int, int> tree;
    std::mt19937 rng(20261028);
    std::uniform_int_distribution<int> dist(0, 1000);
    for (int i = 0; i < 500; ++i) tree.insert(dist(rng), i);

    std::vector<int> keys;
    tree.inOrder([&keys](int key, const std::vector<int>&) { keys.push_back(key); });
    CHECK(!keys.empty());
    CHECK(std::is_sorted(keys.begin(), keys.end()));
    CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());  // 键不重复
}

TEST(AvlDuplicateKeysShareNode) {
    wh::AVLTree<int, int> tree;
    tree.insert(5, 1);
    tree.insert(5, 2);
    tree.insert(5, 3);
    tree.insert(7, 4);

    CHECK_EQ(tree.size(), 4u);      // 元素 4 个
    CHECK_EQ(tree.nodeCount(), 2u); // 节点 2 个（5 的桶里有 3 个值）

    const std::vector<int>* bucket = tree.find(5);
    CHECK(bucket != nullptr);
    CHECK_EQ(bucket->size(), 3u);
    CHECK(tree.validate());
}

TEST(AvlEraseByValue) {
    wh::AVLTree<int, int> tree;
    for (int i = 0; i < 8; ++i) tree.insert(i % 4, i);  // 每个键 2 个值

    CHECK(tree.erase(0, 0));
    CHECK(!tree.erase(0, 0));       // 已删过
    CHECK_EQ(tree.size(), 7u);
    CHECK_EQ(tree.nodeCount(), 4u); // 桶未空，节点还在

    CHECK(tree.erase(0, 4));        // 删掉最后的值
    CHECK_EQ(tree.size(), 6u);
    CHECK_EQ(tree.nodeCount(), 3u); // 节点被摘除
    CHECK(tree.find(0) == nullptr);
    CHECK(tree.validate());
}

TEST(AvlEraseManyKeepsBalance) {
    wh::AVLTree<int, int> tree;
    std::vector<int> keys;
    for (int i = 0; i < 600; ++i) {
        const int k = (i * 37) % 601;
        keys.push_back(k);
        tree.insert(k, i);
    }
    CHECK(tree.validate());

    for (std::size_t i = 0; i < keys.size(); i += 2) tree.erase(keys[i], static_cast<int>(i));
    CHECK(tree.validate());
}

TEST(AvlRangeQuery) {
    wh::AVLTree<int, int> tree;
    for (int i = 0; i < 100; ++i) tree.insert(i, i);

    std::vector<int> hit;
    tree.rangeQuery(20, 29, [&hit](int key, const std::vector<int>&) { hit.push_back(key); });
    CHECK_EQ(hit.size(), 10u);
    CHECK_EQ(hit.front(), 20);
    CHECK_EQ(hit.back(), 29);

    hit.clear();
    tree.rangeQuery(200, 300, [&hit](int, const std::vector<int>&) { hit.push_back(0); });
    CHECK(hit.empty());
}

TEST(AvlMinMax) {
    wh::AVLTree<int, int> tree;
    int key = 0;
    std::vector<int> values;
    CHECK(!tree.first(key, values));

    for (int i : {5, 3, 9, 1, 7}) tree.insert(i, i);

    CHECK(tree.first(key, values));
    CHECK_EQ(key, 1);
    CHECK(tree.last(key, values));
    CHECK_EQ(key, 9);
}

// ---------------------------------------------------------------------------
//  双向链表
// ---------------------------------------------------------------------------
TEST(DoublyLinkedListBasics) {
    wh::DoublyLinkedList<int> list;
    CHECK(list.empty());
    for (int i = 1; i <= 5; ++i) list.pushBack(i);

    CHECK_EQ(list.size(), 5u);
    std::vector<int> forward;
    list.forEach([&forward](int v) { forward.push_back(v); });
    CHECK_EQ(forward.size(), 5u);
    CHECK_EQ(forward.front(), 1);
    CHECK_EQ(forward.back(), 5);

    std::vector<int> backward;
    list.forEachReverse([&backward](int v) { backward.push_back(v); });
    CHECK_EQ(backward.front(), 5);
    CHECK_EQ(backward.back(), 1);

    CHECK(list.popFront());
    CHECK(list.popBack());
    CHECK_EQ(list.size(), 3u);
}

TEST(DoublyLinkedListCapacityWindow) {
    wh::DoublyLinkedList<int> list;
    list.setCapacity(3);
    for (int i = 1; i <= 10; ++i) list.pushBack(i);

    CHECK_EQ(list.size(), 3u);
    const std::vector<int> recent = list.takeRecent(3);
    CHECK_EQ(recent.size(), 3u);
    CHECK_EQ(recent[0], 10);  // 最新在前
    CHECK_EQ(recent[2], 8);
}

// ---------------------------------------------------------------------------
//  排序与查找
// ---------------------------------------------------------------------------
std::vector<int> randomVector(int n, int maxValue, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, maxValue);
    std::vector<int> v(static_cast<std::size_t>(n));
    for (auto& x : v) x = dist(rng);
    return v;
}

TEST(SortAlgorithmsAgreeWithStdSort) {
    const std::vector<int> base = randomVector(3000, 500, 7);  // 大量重复值
    auto less = [](int a, int b) { return a < b; };

    std::vector<int> expected = base;
    std::sort(expected.begin(), expected.end());

    std::vector<int> quick = base;
    wh::quickSort(quick.begin(), quick.end(), less);
    CHECK(quick == expected);

    std::vector<int> merge = base;
    wh::mergeSort(merge, less);
    CHECK(merge == expected);

    std::vector<int> heap = base;
    wh::heapSort(heap, less);
    CHECK(heap == expected);
}

TEST(SortEdgeCases) {
    auto less = [](int a, int b) { return a < b; };

    std::vector<int> empty;
    wh::quickSort(empty.begin(), empty.end(), less);
    wh::mergeSort(empty, less);
    wh::heapSort(empty, less);
    CHECK(empty.empty());

    std::vector<int> single{42};
    wh::quickSort(single.begin(), single.end(), less);
    CHECK_EQ(single[0], 42);

    std::vector<int> same(500, 9);  // 全部相等：三路分区应保持 O(n)
    wh::quickSort(same.begin(), same.end(), less);
    CHECK(std::all_of(same.begin(), same.end(), [](int v) { return v == 9; }));

    std::vector<int> reversed{9, 7, 5, 3, 1};
    wh::quickSort(reversed.begin(), reversed.end(), less);
    CHECK(std::is_sorted(reversed.begin(), reversed.end()));
}

TEST(MergeSortIsStable) {
    struct Rec {
        int key;
        int seq;
    };
    std::vector<Rec> v{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    wh::mergeSort(v, [](const Rec& a, const Rec& b) { return a.key < b.key; });

    // 相同 key 的原始顺序必须保留
    CHECK_EQ(v[0].seq, 1);
    CHECK_EQ(v[1].seq, 3);
    CHECK_EQ(v[2].seq, 0);
    CHECK_EQ(v[3].seq, 2);
    CHECK_EQ(v[4].seq, 4);
}

TEST(BinarySearchWorks) {
    std::vector<int> v{1, 3, 5, 7, 9, 11};
    auto less = [](int a, int b) { return a < b; };

    auto it = wh::binarySearch(v.begin(), v.end(), 7, less);
    CHECK(it != v.end());
    CHECK_EQ(*it, 7);

    CHECK(wh::binarySearch(v.begin(), v.end(), 4, less) == v.end());
    CHECK(wh::binarySearch(v.begin(), v.end(), 0, less) == v.end());
    CHECK(wh::binarySearch(v.begin(), v.end(), 99, less) == v.end());
}

// ---------------------------------------------------------------------------
//  KMP
// ---------------------------------------------------------------------------
TEST(KmpMatchesNaive) {
    const std::vector<std::string> texts = {
        "ababcababcabc",
        "aaaaaab",
        "hello world",
        "螺丝刀扳手螺丝钉",
        "",
        "abc",
    };
    const std::vector<std::string> patterns = {"abc", "aab", "aaa", "world", "螺丝", "z", "abc"};

    for (const auto& t : texts) {
        for (const auto& p : patterns) {
            CHECK_EQ(wh::kmpFind(t, p), wh::naiveFind(t, p));
        }
    }
}

TEST(KmpWorstCaseIsLinear) {
    // "aaaa...ab" 中找 "aaaa...a"：朴素算法会退化，KMP 不会
    const std::string text(20000, 'a');
    const std::string pattern(1000, 'a');
    CHECK_EQ(wh::kmpFind(text, pattern), 0u);
    CHECK_EQ(wh::kmpFind(text + "b", pattern), 0u);
    CHECK_EQ(wh::kmpFind("b" + text, pattern), 1u);
}

TEST(ContainsIgnoreCase) {
    CHECK(wh::containsIgnoreCaseAscii("AbcDef", "abc"));
    CHECK(wh::containsIgnoreCaseAscii("AbcDef", "DEF"));
    CHECK(!wh::containsIgnoreCaseAscii("AbcDef", "xyz"));
    CHECK(wh::containsIgnoreCaseAscii("螺丝刀", "螺丝"));  // 中文按字节匹配
}

// ---------------------------------------------------------------------------
//  文本排版与编码
// ---------------------------------------------------------------------------
TEST(DisplayWidthHandlesCjk) {
    CHECK_EQ(wh::displayWidth("abc"), 3u);
    CHECK_EQ(wh::displayWidth("商品"), 4u);       // 每个汉字 2 列
    CHECK_EQ(wh::displayWidth("A商品"), 5u);
    CHECK_EQ(wh::displayWidth(""), 0u);

    const std::string padded = wh::padRight("商品", 8);
    CHECK_EQ(wh::displayWidth(padded), 8u);
}

TEST(EllipsizeKeepsValidUtf8) {
    const std::string s = "很长很长很长很长的商品名称";
    const std::string cut = wh::ellipsize(s, 10);
    CHECK(wh::displayWidth(cut) <= 10u);
    CHECK(wh::isValidUtf8(cut));
    CHECK(cut.back() != '\xEF');  // 不能切在多字节字符中间
}

TEST(EncodingDetection) {
    CHECK(wh::isValidUtf8("商品ABC"));
    CHECK(!wh::isValidUtf8("\xC9\xCC\xC6\xB7"));  // GBK 的"商品"
    CHECK_EQ(wh::gbkToUtf8("\xC9\xCC\xC6\xB7"), std::string("商品"));
    CHECK_EQ(wh::normalizeToUtf8("商品"), std::string("商品"));
}

// ---------------------------------------------------------------------------
//  业务服务
// ---------------------------------------------------------------------------
wh::Repository makeTempRepository() { return wh::Repository(".\\wh_test_tmp"); }

wh::Item makeItem(int id, const std::string& name, int qty, double price) {
    wh::Item item;
    item.id = id;
    item.name = name;
    item.quantity = qty;
    item.price = price;
    return item;
}

TEST(ServiceCrudKeepsIndexesConsistent) {
    wh::Repository repo = makeTempRepository();
    wh::WarehouseService service(repo);
    std::string error;
    service.initialize(error);

    CHECK(service.add(makeItem(1, "螺丝刀", 100, 12.5)).ok);
    CHECK(service.add(makeItem(2, "扳手", 50, 30.0)).ok);
    CHECK(service.add(makeItem(3, "螺丝", 1000, 0.5)).ok);
    CHECK(!service.add(makeItem(1, "重复编号", 1, 1.0)).ok);  // 编号冲突

    std::string report;
    CHECK(service.selfCheck(report));

    // 修改价格与库存 -> 两个 AVL 的键都要跟着变
    wh::ItemPatch patch;
    patch.hasPrice = true;
    patch.price = 99.0;
    patch.hasQuantity = true;
    patch.quantity = 3;
    CHECK(service.update(1, patch).ok);
    CHECK(service.selfCheck(report));

    const wh::Item* updated = service.findById(1);
    CHECK(updated != nullptr);
    CHECK(std::fabs(updated->price - 99.0) < 1e-9);
    CHECK_EQ(updated->quantity, 3);

    // 旧价格不应再能查到
    CHECK(service.priceRange(12.5, 12.5).empty());
    CHECK_EQ(service.priceRange(99.0, 99.0).size(), 1u);

    CHECK(service.remove(2).ok);
    CHECK(!service.remove(2).ok);
    CHECK(service.selfCheck(report));
    CHECK_EQ(service.size(), 2u);
}

TEST(ServiceSortUsesIndexPath) {
    wh::Repository repo = makeTempRepository();
    wh::WarehouseService service(repo);
    std::string error;
    service.initialize(error);

    service.add(makeItem(1, "丙", 30, 5.0));
    service.add(makeItem(2, "甲", 10, 15.0));
    service.add(makeItem(3, "乙", 20, 1.0));

    const wh::Listing byPrice = service.list(wh::SortField::Price, true);
    CHECK_EQ(byPrice.items.size(), 3u);
    CHECK_EQ(byPrice.items[0].id, 3);  // 1.0
    CHECK_EQ(byPrice.items[2].id, 2);  // 15.0
    CHECK(byPrice.strategy.find("AVL") != std::string::npos);

    const wh::Listing byPriceDesc = service.list(wh::SortField::Price, false);
    CHECK_EQ(byPriceDesc.items[0].id, 2);

    const wh::Listing byQty = service.list(wh::SortField::Quantity, true);
    CHECK_EQ(byQty.items[0].id, 2);  // 10

    const wh::Listing byIdDesc = service.list(wh::SortField::Id, false);
    CHECK_EQ(byIdDesc.items[0].id, 3);
}

TEST(ServiceSearchAndRange) {
    wh::Repository repo = makeTempRepository();
    wh::WarehouseService service(repo);
    std::string error;
    service.initialize(error);

    service.add(makeItem(1, "内六角螺丝刀", 10, 20.0));
    service.add(makeItem(2, "一字螺丝刀", 5, 18.0));
    service.add(makeItem(3, "活动扳手", 8, 45.0));

    CHECK_EQ(service.searchByName("螺丝刀").size(), 2u);
    CHECK_EQ(service.searchByName("扳手").size(), 1u);
    CHECK(service.searchByName("不存在的商品").empty());

    CHECK_EQ(service.priceRange(15.0, 20.0).size(), 2u);
    CHECK_EQ(service.lowStock(8).size(), 2u);  // 5 和 8
    CHECK_EQ(service.lowStock(4).size(), 0u);
}

TEST(ServiceImportPolicies) {
    wh::Repository repo = makeTempRepository();
    wh::WarehouseService service(repo);
    std::string error;
    service.initialize(error);

    service.add(makeItem(1, "原商品", 10, 10.0));

    std::vector<wh::Item> incoming{makeItem(1, "覆盖商品", 20, 20.0), makeItem(2, "新商品", 30, 30.0)};

    const wh::ImportOutcome skipped =
        service.importItems(incoming, wh::OverwritePolicy::Skip);
    CHECK_EQ(skipped.inserted, 1u);
    CHECK_EQ(skipped.skipped, 1u);
    CHECK_EQ(service.findById(1)->name, std::string("原商品"));

    // 此时库里已有 1、2 两个编号，再来一次覆盖导入，两条都会被覆盖
    const wh::ImportOutcome overwritten =
        service.importItems(incoming, wh::OverwritePolicy::Overwrite);
    CHECK_EQ(overwritten.overwritten, 2u);
    CHECK_EQ(overwritten.inserted, 0u);
    CHECK_EQ(service.findById(1)->name, std::string("覆盖商品"));

    std::string report;
    CHECK(service.selfCheck(report));
}

// ---------------------------------------------------------------------------
//  持久化
// ---------------------------------------------------------------------------
TEST(RepositoryBinaryRoundTrip) {
    wh::Repository repo = makeTempRepository();
    std::string error;

    std::vector<wh::Item> items{makeItem(1, "螺丝刀", 10, 12.5), makeItem(2, "中文名称ABC", 0, 0.01)};
    CHECK(repo.saveItems(items, error));

    std::vector<wh::Item> loaded;
    bool legacy = false;
    CHECK(repo.loadItems(loaded, error, legacy));
    CHECK(!legacy);
    CHECK_EQ(loaded.size(), 2u);
    CHECK_EQ(loaded[0].name, std::string("螺丝刀"));
    CHECK_EQ(loaded[1].name, std::string("中文名称ABC"));
    CHECK(std::fabs(loaded[0].price - 12.5) < 1e-9);
}

TEST(RepositoryCsvRoundTrip) {
    wh::Repository repo = makeTempRepository();
    std::string error;

    std::vector<wh::Item> items{makeItem(1, "带,逗号的名字", 5, 3.5),
                                makeItem(2, "带\"引号\"的名字", 7, 8.25),
                                makeItem(3, "普通商品", 9, 1.0)};
    CHECK(wh::Repository::exportCsv(".\\wh_test_tmp\\roundtrip.csv", items, error));

    std::vector<wh::Item> loaded;
    wh::ImportStats stats;
    CHECK(wh::Repository::importCsv(".\\wh_test_tmp\\roundtrip.csv", loaded, stats, error));
    CHECK_EQ(loaded.size(), 3u);
    CHECK_EQ(loaded[0].name, std::string("带,逗号的名字"));
    CHECK_EQ(loaded[1].name, std::string("带\"引号\"的名字"));
    CHECK_EQ(loaded[1].quantity, 7);
}

TEST(RepositoryRejectsMalformedCsv) {
    wh::Repository repo = makeTempRepository();
    {
        std::FILE* f = std::fopen(".\\wh_test_tmp\\bad.csv", "wb");
        CHECK(f != nullptr);
        if (f) {
            const char* content =
                "\xEF\xBB\xBF编号,名称,库存,价格\n"
                "1,正常商品,10,5.5\n"
                "abc,编号非数字,10,5.5\n"
                "3,库存为负,-5,5.5\n"
                "4,价格为负,10,-1\n"
                "5,缺列,10\n";
            std::fwrite(content, 1, std::strlen(content), f);
            std::fclose(f);
        }
    }

    std::vector<wh::Item> loaded;
    wh::ImportStats stats;
    std::string error;
    CHECK(wh::Repository::importCsv(".\\wh_test_tmp\\bad.csv", loaded, stats, error));
    CHECK_EQ(loaded.size(), 1u);      // 只有第 1 行有效
    CHECK_EQ(stats.malformed, 4u);
}

TEST(RepositoryTruncatedFileDetected) {
    wh::Repository repo = makeTempRepository();
    std::string error;
    std::vector<wh::Item> items{makeItem(1, "商品", 1, 1.0), makeItem(2, "商品2", 2, 2.0)};
    CHECK(repo.saveItems(items, error));

    // 人为截断尾部校验标记
    {
        std::FILE* f = std::fopen(repo.itemsPath().c_str(), "rb");
        CHECK(f != nullptr);
        std::vector<char> buf;
        if (f) {
            char chunk[64];
            std::size_t n = 0;
            while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) buf.insert(buf.end(), chunk, chunk + n);
            std::fclose(f);
        }
        CHECK(buf.size() > 4);
        if (buf.size() > 4) {
            std::FILE* out = std::fopen(repo.itemsPath().c_str(), "wb");
            CHECK(out != nullptr);
            if (out) {
                std::fwrite(buf.data(), 1, buf.size() - 4, out);
                std::fclose(out);
            }
        }
    }

    std::vector<wh::Item> loaded;
    bool legacy = false;
    // 尾部校验失败应返回 false，但已读取的记录仍可恢复
    CHECK(!repo.loadItems(loaded, error, legacy));
    CHECK_EQ(loaded.size(), 2u);
    CHECK(!error.empty());
}

}  // namespace

int main() {
    wh::ConsoleGuard guard;  // 输出切 UTF-8，退出时还原，避免污染调用方的控制台代码页

    // 测试目录准备
    CreateDirectoryA(".\\wh_test_tmp", nullptr);

    std::cout << "=== 单元测试 ===\n";
    for (const TestCase& tc : registry()) {
        const int before = g_failures;
        g_currentTest = tc.name;
        tc.fn();
        std::cout << (g_failures == before ? "  [通过] " : "  [失败] ") << tc.name << "\n";
    }

    std::cout << "\n断言 " << g_checks << " 项，失败 " << g_failures << " 项\n";

    // 清理测试产物
    std::remove(".\\wh_test_tmp\\warehouse.dat");
    std::remove(".\\wh_test_tmp\\history.dat");
    std::remove(".\\wh_test_tmp\\roundtrip.csv");
    std::remove(".\\wh_test_tmp\\bad.csv");
    RemoveDirectoryA(".\\wh_test_tmp");

    return g_failures == 0 ? 0 : 1;
}
