// =============================================================================
//  hash_table.h —— 链地址法（Separate Chaining）哈希表
//
//  设计要点：
//    * 桶数组 + 单链表解决冲突（拉链法），比开放定址法更适合动态增删
//    * 装载因子超过 0.75 时桶数翻倍并 rehash —— 保证平均查找长度恒定
//    * rehash 只重新链接节点指针，不重新分配节点，避免二次内存分配
//    * 节点单独 new 分配：与 std::unordered_map 的"桶内节点"结构对应
//
//  复杂度：插入/查找/删除 平均 O(1)，最坏 O(n)（全部冲突到同一条链）
//          空间 O(n + m)，m 为桶数
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace wh {

template <class Key, class Value,
          class Hash = std::hash<Key>,
          class Eq = std::equal_to<Key>>
class HashTable {
public:
    // 装载因子阈值：超过则扩容。0.75 是链地址法的经验最优值
    static constexpr double kMaxLoadFactor = 0.75;

    explicit HashTable(std::size_t initialBuckets = 16)
        : buckets_(initialBuckets == 0 ? 1 : initialBuckets) {}

    ~HashTable() { clear(); }

    // 内含裸指针链表，禁止浅拷贝
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // ---------------- 容量与状态 ----------------
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::size_t bucketCount() const { return buckets_.size(); }

    double loadFactor() const {
        return buckets_.empty() ? 0.0
                                : static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }

    void clear() {
        for (auto& head : buckets_) {
            Node* p = head;
            while (p) {
                Node* next = p->next;
                delete p;
                p = next;
            }
            head = nullptr;
        }
        size_ = 0;
    }

    // ---------------- 增删改查 ----------------
    // 返回 true 表示新增，false 表示键已存在、值被覆盖
    bool insert(const Key& key, const Value& value) {
        if (Node* hit = findNode(key)) {
            hit->value = value;
            return false;
        }
        if (loadFactor() >= kMaxLoadFactor) {
            rehash(buckets_.size() * 2);
        }
        const std::size_t idx = indexOf(key);
        buckets_[idx] = new Node{key, value, buckets_[idx]};
        ++size_;
        return true;
    }

    Value* find(const Key& key) {
        Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }

    const Value* find(const Key& key) const {
        const Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }

    bool contains(const Key& key) const { return findNode(key) != nullptr; }

    bool erase(const Key& key) {
        const std::size_t idx = indexOf(key);
        Node* prev = nullptr;
        Node* cur = buckets_[idx];
        while (cur) {
            if (eq_(cur->key, key)) {
                if (prev) {
                    prev->next = cur->next;
                } else {
                    buckets_[idx] = cur->next;
                }
                delete cur;
                --size_;
                return true;
            }
            prev = cur;
            cur = cur->next;
        }
        return false;
    }

    // 遍历全部键值（顺序不保证）
    template <class Fn>
    void forEach(Fn fn) const {
        for (const Node* head : buckets_) {
            for (const Node* p = head; p; p = p->next) {
                fn(p->key, p->value);
            }
        }
    }

    // ---------------- 哈希函数质量诊断 ----------------
    // 最长链长度：接近 size() 说明哈希函数退化，查找退化为 O(n)
    std::size_t maxBucketLength() const {
        std::size_t best = 0;
        for (const Node* head : buckets_) {
            std::size_t len = 0;
            for (const Node* p = head; p; p = p->next) ++len;
            if (len > best) best = len;
        }
        return best;
    }

    std::size_t emptyBuckets() const {
        std::size_t n = 0;
        for (const Node* head : buckets_) {
            if (!head) ++n;
        }
        return n;
    }

private:
    struct Node {
        Key key;
        Value value;
        Node* next;
    };

    std::size_t indexOf(const Key& key) const {
        return static_cast<std::size_t>(hash_(key)) % buckets_.size();
    }

    Node* findNode(const Key& key) const {
        for (Node* p = buckets_[indexOf(key)]; p; p = p->next) {
            if (eq_(p->key, key)) return p;
        }
        return nullptr;
    }

    // 扩容：桶数翻倍，把已有节点重新挂到新桶上（节点不复制）
    void rehash(std::size_t newBucketCount) {
        std::vector<Node*> fresh(newBucketCount, nullptr);
        for (Node* head : buckets_) {
            Node* p = head;
            while (p) {
                Node* next = p->next;
                const std::size_t idx =
                    static_cast<std::size_t>(hash_(p->key)) % newBucketCount;
                p->next = fresh[idx];
                fresh[idx] = p;
                p = next;
            }
        }
        buckets_.swap(fresh);
    }

    std::vector<Node*> buckets_;
    std::size_t size_ = 0;
    Hash hash_{};
    Eq eq_{};
};

}  // namespace wh
