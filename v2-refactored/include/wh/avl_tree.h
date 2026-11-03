// =============================================================================
//  avl_tree.h —— AVL 平衡二叉搜索树（Multiset 语义，支持重复键）
//
//  设计要点：
//    * 用于「按价格排序 / 按库存排序 / 区间查询」的有序索引
//    * 重复键不新建节点，而是挂在节点的 values 桶里 —— 避免排序索引膨胀
//    * 四种旋转（LL / RR / LR / RL）由 rebalance() 统一分派
//    * 每次插入/删除后沿递归路径回溯并自动修复平衡
//
//  复杂度：插入/删除/查找 O(log n)；中序遍历 O(n) 且天然有序（优于排序的 O(n log n)）
//          区间查询 O(log n + k)，k 为命中元素数
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace wh {

template <class Key, class Value, class Less = std::less<Key>>
class AVLTree {
public:
    AVLTree() = default;
    ~AVLTree() { clear(); }

    AVLTree(const AVLTree&) = delete;
    AVLTree& operator=(const AVLTree&) = delete;

    // ---------------- 容量与状态 ----------------
    std::size_t size() const { return size_; }          // 元素总数（含重复键）
    std::size_t nodeCount() const { return nodes_; }    // 节点数（去重后的键数）
    bool empty() const { return nodes_ == 0; }
    int height() const { return heightOf(root_); }      // 空树为 0

    void clear() {
        destroy(root_);
        root_ = nullptr;
        size_ = 0;
        nodes_ = 0;
    }

    // ---------------- 插入 ----------------
    // 键已存在时追加到该节点的桶中
    void insert(const Key& key, const Value& value) {
        root_ = insertRec(root_, key, value);
    }

    // ---------------- 删除 ----------------
    // 删除「键 + 值」这一对，桶空则摘除节点
    bool erase(const Key& key, const Value& value) {
        bool removed = false;
        root_ = eraseValueRec(root_, key, value, removed);
        return removed;
    }

    // 删除整个键（含桶内全部值）
    bool eraseKey(const Key& key) {
        if (!findNode(root_, key)) return false;
        std::size_t removedValues = 0;
        root_ = detachNodeRec(root_, key, removedValues);
        size_ -= removedValues;
        return true;
    }

    // ---------------- 查询 ----------------
    const std::vector<Value>* find(const Key& key) const {
        const Node* n = findNode(root_, key);
        return n ? &n->values : nullptr;
    }

    // 中序遍历：按 key 升序回调 fn(key, values)
    template <class Fn>
    void inOrder(Fn fn) const {
        inOrderRec(root_, fn);
    }

    // 闭区间 [lo, hi] 查询，按 key 升序回调
    template <class Fn>
    void rangeQuery(const Key& lo, const Key& hi, Fn fn) const {
        rangeQueryRec(root_, lo, hi, fn);
    }

    // 最小 / 最大键
    bool first(Key& outKey, std::vector<Value>& outValues) const {
        if (!root_) return false;
        const Node* n = root_;
        while (n->left) n = n->left;
        outKey = n->key;
        outValues = n->values;
        return true;
    }

    bool last(Key& outKey, std::vector<Value>& outValues) const {
        if (!root_) return false;
        const Node* n = root_;
        while (n->right) n = n->right;
        outKey = n->key;
        outValues = n->values;
        return true;
    }

    // ---------------- 自检（供单元测试使用） ----------------
    // 校验 BST 有序性 + 高度记录正确 + 每个节点平衡因子绝对值 <= 1 + 计数一致
    bool validate() const {
        std::size_t nodeCount = 0;
        std::size_t valueCount = 0;
        return validateRec(root_, nodeCount, valueCount) &&
               nodeCount == nodes_ && valueCount == size_;
    }

private:
    struct Node {
        Key key;
        std::vector<Value> values;  // 重复键桶
        Node* left = nullptr;
        Node* right = nullptr;
        int height = 1;             // 叶子高度为 1，空为 0
    };

    // ---------------- 基础工具 ----------------
    static int heightOf(const Node* n) { return n ? n->height : 0; }

    static void updateHeight(Node* n) {
        if (!n) return;
        const int l = heightOf(n->left);
        const int r = heightOf(n->right);
        n->height = (l > r ? l : r) + 1;
    }

    static int balanceFactor(const Node* n) {
        return n ? heightOf(n->left) - heightOf(n->right) : 0;
    }

    static Node* rotateRight(Node* y) {
        Node* x = y->left;
        Node* t2 = x->right;
        x->right = y;
        y->left = t2;
        updateHeight(y);
        updateHeight(x);
        return x;
    }

    static Node* rotateLeft(Node* x) {
        Node* y = x->right;
        Node* t2 = y->left;
        y->left = x;
        x->right = t2;
        updateHeight(x);
        updateHeight(y);
        return y;
    }

    // 统一分派四种失衡情形
    static Node* rebalance(Node* n) {
        if (!n) return nullptr;
        updateHeight(n);
        const int bf = balanceFactor(n);

        if (bf > 1) {  // 左子树过高
            if (balanceFactor(n->left) < 0) {
                n->left = rotateLeft(n->left);  // LR
            }
            return rotateRight(n);              // LL
        }
        if (bf < -1) {  // 右子树过高
            if (balanceFactor(n->right) > 0) {
                n->right = rotateRight(n->right);  // RL
            }
            return rotateLeft(n);                  // RR
        }
        return n;
    }

    static void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    static Node* findNode(Node* n, const Key& key) {
        while (n) {
            if (less_(key, n->key)) {
                n = n->left;
            } else if (less_(n->key, key)) {
                n = n->right;
            } else {
                return n;
            }
        }
        return nullptr;
    }

    // ---------------- 插入 ----------------
    Node* insertRec(Node* n, const Key& key, const Value& value) {
        if (!n) {
            Node* fresh = new Node();
            fresh->key = key;
            fresh->values.push_back(value);
            ++size_;
            ++nodes_;
            return fresh;
        }
        if (less_(key, n->key)) {
            n->left = insertRec(n->left, key, value);
        } else if (less_(n->key, key)) {
            n->right = insertRec(n->right, key, value);
        } else {
            n->values.push_back(value);  // 重复键入桶，结构不变
            ++size_;
            return n;
        }
        return rebalance(n);
    }

    // ---------------- 删除：按「键 + 值」删一对 ----------------
    Node* eraseValueRec(Node* n, const Key& key, const Value& value, bool& removed) {
        if (!n) return nullptr;
        if (less_(key, n->key)) {
            n->left = eraseValueRec(n->left, key, value, removed);
        } else if (less_(n->key, key)) {
            n->right = eraseValueRec(n->right, key, value, removed);
        } else {
            auto it = n->values.begin();
            for (; it != n->values.end(); ++it) {
                if (*it == value) break;
            }
            if (it == n->values.end()) return n;  // 该键存在但没有这个值

            n->values.erase(it);
            --size_;
            removed = true;

            if (n->values.empty()) {
                std::size_t ignored = 0;  // 桶已空，摘除节点不再影响 size_
                return detachNodeRec(n, key, ignored);
            }
            return n;  // 桶非空，树结构不变
        }
        return rebalance(n);
    }

    // 物理摘除节点（nodes_ 自减 1），被摘掉的值通过 removedValues 返回
    Node* detachNodeRec(Node* n, const Key& key, std::size_t& removedValues) {
        if (!n) return nullptr;
        if (less_(key, n->key)) {
            n->left = detachNodeRec(n->left, key, removedValues);
        } else if (less_(n->key, key)) {
            n->right = detachNodeRec(n->right, key, removedValues);
        } else {
            removedValues += n->values.size();
            if (!n->left || !n->right) {  // 至多一个孩子，直接顶替
                --nodes_;
                Node* child = n->left ? n->left : n->right;
                delete n;
                return child;
            }
            // 两个孩子：用中序后继替换内容，再从右子树摘除后继节点。
            // 注意：节点数只在「物理删除后继节点」时减 1，这里不能减，
            //      否则 nodes_ 会少算一个，validate() 的计数校验立刻失败。
            // 后继的 values 先复制再清空，保证它被摘除时不会重复计入 removedValues。
            Node* succ = n->right;
            while (succ->left) succ = succ->left;
            n->key = succ->key;
            n->values = succ->values;
            succ->values.clear();
            n->right = detachNodeRec(n->right, succ->key, removedValues);
        }
        return rebalance(n);
    }

    // ---------------- 遍历 ----------------
    template <class Fn>
    static void inOrderRec(const Node* n, Fn& fn) {
        if (!n) return;
        inOrderRec(n->left, fn);
        fn(n->key, n->values);
        inOrderRec(n->right, fn);
    }

    template <class Fn>
    void rangeQueryRec(const Node* n, const Key& lo, const Key& hi, Fn& fn) const {
        if (!n) return;
        // 只有 lo < n->key 时左子树才可能有命中
        if (less_(lo, n->key)) rangeQueryRec(n->left, lo, hi, fn);
        // lo <= n->key <= hi
        if (!less_(n->key, lo) && !less_(hi, n->key)) fn(n->key, n->values);
        // 只有 n->key < hi 时右子树才可能有命中
        if (less_(n->key, hi)) rangeQueryRec(n->right, lo, hi, fn);
    }

    static bool validateRec(const Node* n, std::size_t& nodeCount, std::size_t& valueCount) {
        if (!n) return true;
        if (n->left && !less_(n->left->key, n->key)) return false;   // 左子树必须严格更小
        if (n->right && !less_(n->key, n->right->key)) return false;  // 右子树必须严格更大
        const int l = heightOf(n->left);
        const int r = heightOf(n->right);
        if (n->height != (l > r ? l : r) + 1) return false;
        if (l - r > 1 || r - l > 1) return false;                     // 平衡因子约束
        ++nodeCount;
        valueCount += n->values.size();
        return validateRec(n->left, nodeCount, valueCount) &&
               validateRec(n->right, nodeCount, valueCount);
    }

    Node* root_ = nullptr;
    std::size_t size_ = 0;
    std::size_t nodes_ = 0;

    static inline Less less_{};
};

}  // namespace wh
