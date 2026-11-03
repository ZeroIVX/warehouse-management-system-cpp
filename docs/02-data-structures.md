# 02 · 数据结构与算法

> 本文记录 v2 中四个手写核心结构的实现要点、复杂度与关键取舍。
> 全部为 header-only 模板，位于 `v2-refactored/include/wh/`。

---

## 一、哈希表 · `hash_table.h`

**用途**：主索引 `id → Item`。

### 设计要点

| 决策 | 选择 | 理由 |
|---|---|---|
| 冲突处理 | 拉链法（桶数组 + 单链表） | 相比开放定址法，删除不需要墓碑标记，更适合增删频繁的场景 |
| 扩容阈值 | 装载因子 ≥ **0.75** | 链地址法的经验最优值；低于它哈希冲突少，高于它链变长 |
| 扩容方式 | 桶数翻倍 + rehash | 均摊 `O(1)`；节点**只重连指针**，不重新分配内存 |
| 桶索引 | `hash(key) % bucketCount` | 实现简单；依赖 `std::hash` 的位混合质量 |

### 关键实现

rehash 只做指针重挂，不复用节点的内存分配：

```cpp
void rehash(std::size_t newBucketCount) {
    std::vector<Node*> fresh(newBucketCount, nullptr);
    for (Node* head : buckets_) {
        Node* p = head;
        while (p) {
            Node* next = p->next;                       // 先存后继
            const std::size_t idx = hash_(p->key) % newBucketCount;
            p->next = fresh[idx];                       // 头插到新桶
            fresh[idx] = p;
            p = next;
        }
    }
    buckets_.swap(fresh);
}
```

### 复杂度

| 操作 | 平均 | 最坏 |
|---|---|---|
| `insert` / `find` / `erase` | `O(1)` | `O(n)`（全部冲突到同一条链） |
| 空间 | `O(n + m)`，`m` 为桶数 | — |

### 可观测性

提供两个诊断接口，用于判断哈希函数是否退化：

```cpp
std::size_t maxBucketLength() const;   // 最长链长度
std::size_t emptyBuckets() const;      // 空桶数量
```

主界面的诊断页会显示"桶数 / 最长链"。若最长链明显大于平均链长，说明哈希分布有问题。
单元测试里用连续整数（低位高度规律）插入 5000 个键，断言最长链不超过平均链长的 4 倍。

---

## 二、AVL 树 · `avl_tree.h`

**用途**：价格索引与库存索引，支持有序遍历与区间查询。

### 设计要点

- **多重集语义**：相同键不新建节点，收进节点的 `values` 桶。节点数 = 不同键数，元素数 = 商品总数。
- **四种旋转**由 `rebalance()` 统一分派，插入 / 删除沿递归路径回溯修复：

```cpp
static Node* rebalance(Node* n) {
    updateHeight(n);
    const int bf = balanceFactor(n);
    if (bf > 1) {                                   // 左重
        if (balanceFactor(n->left) < 0) n->left = rotateLeft(n->left);   // LR
        return rotateRight(n);                                           // LL
    }
    if (bf < -1) {                                  // 右重
        if (balanceFactor(n->right) > 0) n->right = rotateRight(n->right); // RL
        return rotateLeft(n);                                             // RR
    }
    return n;
}
```

### 删除：两个孩子的处理（最容易写错的地方）

标准做法是用中序后继替换内容，再从右子树摘除后继节点。这里有一个**计数陷阱**：

```cpp
} else {
    removedValues += n->values.size();
    if (!n->left || !n->right) {         // 至多一个孩子
        --nodes_;                        // 物理删除，节点数减一
        Node* child = n->left ? n->left : n->right;
        delete n;
        return child;
    }
    // 两个孩子：本体保留，只搬内容；节点数由后继节点的物理删除负责减一
    // —— 此处【不能】再 --nodes_，否则计数少算一个，validate() 立刻失败
    Node* succ = n->right;
    while (succ->left) succ = succ->left;
    n->key    = succ->key;
    n->values = succ->values;            // 先复制
    succ->values.clear();                // 再清空，保证后继被摘除时不会重复计入
    n->right = detachNodeRec(n->right, succ->key, removedValues);
}
```

这个 bug 在开发中被 `validate()` 的"节点计数一致"断言当场抓出，详见 [04-engineering-notes.md](04-engineering-notes.md)。

### 区间查询

```cpp
void rangeQueryRec(const Node* n, const Key& lo, const Key& hi, Fn& fn) const {
    if (!n) return;
    if (less_(lo, n->key))          rangeQueryRec(n->left,  lo, hi, fn);  // lo < key 才可能有命中
    if (!less_(n->key, lo) && !less_(hi, n->key)) fn(n->key, n->values);   // lo ≤ key ≤ hi
    if (less_(n->key, hi))          rangeQueryRec(n->right, lo, hi, fn);  // key < hi 才可能有命中
}
```

两个剪枝条件把全树扫描压到 `O(log n + k)`。

### 复杂度

| 操作 | 复杂度 |
|---|---|
| `insert` / `erase` / `eraseKey` | `O(log n)` |
| `find` | `O(log n)` |
| `inOrder`（全量有序输出） | `O(n)` |
| `rangeQuery(lo, hi)` | `O(log n + k)` |
| 空间 | `O(n)` |

### 自检接口

```cpp
bool validate() const;
// 校验：BST 严格有序（左 < 根 < 右）、高度记录正确、
//       每个节点 |bf| ≤ 1、nodeCount 与 nodes_ 一致、valueCount 与 size_ 一致
```

**这是单元测试的主力工具**。所有 AVL 用例在插入 / 删除后都断言一次 `validate()`。

### 实测树高

顺序插入（最恶劣的情况，普通 BST 会退化成链表）：

| N | 100 | 1,000 | 10,000 | 100,000 |
|---|---|---|---|---|
| AVL 树高 | 7 | 10 | 14 | **17** |
| `log₂(N)` | 6.64 | 9.97 | 13.29 | 16.61 |
| 普通 BST 最坏 | 100 | 1,000 | 10,000 | 100,000 |

结果稳定贴近理论下界 `1.44·log₂(N+2)`。

---

## 三、双向链表 · `linked_list.h`

**用途**：操作日志。

### 为什么不用 `vector`

日志的访问模式是"只追加 + 反向遍历 + 规模持续增长"：

| 维度 | `vector` | 双向链表 |
|---|---|---|
| 尾部追加 | 均摊 `O(1)`，但扩容时要整体搬迁 | 稳定 `O(1)`，无搬迁 |
| 丢弃最旧记录 | 头部 `erase` 需搬移全部元素 `O(n)` | `O(1)` 摘头 |
| 反向遍历 | 需 `rbegin/rend`（可以） | 天然支持 |
| 随机访问 | `O(1)` | `O(n)`（本项目不需要） |

日志只需要顺序访问，链表在追加与淘汰两项上都更优，因此选择链表。

### 滚动窗口

```cpp
void setCapacity(std::size_t cap);   // 0 表示不限制
```

超出容量时每次都从头部弹出最旧记录，保证内存占用有上界（默认 2000 条）。
`takeRecent(n)` 从尾部反向取最近 n 条，供 UI 显示。

---

## 四、算法库 · `algorithms.h`

### 1. 快速排序

| 优化 | 作用 |
|---|---|
| 三数取中 | 避免有序输入退化为 `O(n²)` |
| 三路分区 | 大量重复键（真实业务常见：相同价格）退化为 `O(n)` |
| 小数组切换插入排序 | 长度 < 16 时插入排序常数更小 |
| 只递归较短一侧 | 栈深度稳定在 `O(log n)`，避免恶意输入爆栈 |

### 2. 归并排序

稳定排序。当业务要求"同名商品保持录入顺序"时必须用它，快排无法保证。
实现上用 `<=` 判定取左半部分：

```cpp
if (comp(v[j], v[i])) buf[k++] = v[j++];   // 右侧严格小于才先取
else                  buf[k++] = v[i++];   // 相等时优先左侧 → 稳定
```

### 3. 堆排序

原地 `O(1)` 额外空间，最坏仍 `O(n log n)`。实测最慢（跳跃访问、缓存局部性差），
作为算法完整性与教学对照保留。

### 4. 二分查找

提供 `lowerBound` 语义，用于在已排序结果上做 `O(log n)` 定位。

### 5. KMP（自适应）

```cpp
class TextMatcher {
    explicit TextMatcher(std::string pattern);   // 预编译：折叠大小写 + 构造 lps
    std::size_t find(const std::string& text) const;
    bool contains(const std::string& text) const;
    bool usesKmp() const;                        // 本次实际走的是哪条路径
};
```

三个关键设计：

1. **模式预编译一次**，在 N 条商品名之间复用。若每次调用重建 lps，堆分配开销会盖过算法优势（实测慢 3.5 倍）。
2. **大小写折叠下沉为编译期模板参数**，逐字节比较的热路径上没有分支。
3. **按模式长度自适应**：模式短于 8 字节时改走朴素匹配。短模式的 KMP 优势不足以抵消 lps 间接寻址开销。

第四点值得单独说明——这是**实测倒逼出来的决策**，不是教科书上的标准答案：

| 场景 | 朴素 | KMP | 结论 |
|---|---|---|---|
| 商品名（约 6 字节模式） | 4.93 ms | 6.48 ms | 朴素更快，走朴素 |
| 20 KB 文本 + 1 KB 高重复模式 | 26.2 ms | 0.10 ms | KMP 快 260 倍，走 KMP |

UTF-8 兼容性：按字节匹配，但 UTF-8 具有自同步性，合法模式的字节匹配必然落在字符边界上，
因此对中文同样正确。含多字节字符的模式不启用大小写折叠（避免破坏字节序列）。

---

## 五、辅助结构

### `text_util.h` · UTF-8 显示宽度

`std::setw` 按**字节数**补空格，而中文在 UTF-8 下占 3 字节、终端只显示 2 列，直接输出必然错位。

解决方式：按 Unicode 码点解码 → 判断是否 CJK 宽字符 → 累加显示列宽 → 按列数补空格。

```cpp
std::size_t displayWidth(const std::string& s);           // 终端列数
std::string padRight / padLeft / center(s, width);        // 按列补齐
std::string ellipsize(s, maxWidth);                       // 按列截断且不切断 UTF-8 字符
```

### `encoding.h` · 编码与路径

- `isValidUtf8()` / `gbkToUtf8()` / `normalizeToUtf8()`：导入 CSV 时自动识别编码
- `utf8ToWide()`：文件路径转宽字符，规避 MSVC 按 ANSI 解释路径的问题

### `console.h` · 代码页 RAII

```cpp
class ConsoleGuard {
    ConsoleGuard();   // 切 UTF-8（65001）
    ~ConsoleGuard();  // 无条件还原原代码页
};
```

保证程序异常退出也不会把用户的终端留在 UTF-8 状态，同时让批处理脚本（GBK）与程序（UTF-8）在同一控制台共存。

---

<sub>文档维护：Zero ｜ 最后更新：2026-11-02</sub>
