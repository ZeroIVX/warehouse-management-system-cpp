# 01 · 架构设计

> 本文说明 v2 的分层结构、依赖方向、核心不变量的维护方式，以及为什么这样切分。

---

## 一、分层与依赖方向

```
┌───────────────────────────────────────────────────────────────┐
│  表现层  ConsoleApp  (console_ui.h / console_ui.cpp)          │
│  · 菜单与页面渲染        · 表格对齐与颜色                       │
│  · 输入采集与校验        · 决定何时落盘                         │
└───────────────────────────────┬───────────────────────────────┘
                                │ 只调用业务方法，不碰数据文件
┌───────────────────────────────▼───────────────────────────────┐
│  业务层  WarehouseService  (service.h / service.cpp)          │
│  · 领域规则校验          · 多索引协同                           │
│  · 检索与排序编排        · 一致性自检                           │
│                                                                │
│   ┌────────────┬─────────────┬─────────────┬───────────────┐   │
│   │ HashTable  │  AVLTree    │  AVLTree    │DoublyLinkedList│  │
│   │ id → Item  │ price → [id]│ stock → [id]│  HistoryEntry  │  │
│   │ 主索引     │ 价格序索引   │ 库存序索引   │ 操作日志        │  │
│   └────────────┴─────────────┴─────────────┴───────────────┘   │
└───────────────────────────────┬───────────────────────────────┘
                                │ 只知道"读写一批 Item"
┌───────────────────────────────▼───────────────────────────────┐
│  持久化层  Repository  (repository.h / repository.cpp)        │
│  · WHv2 二进制读写       · WHv1 旧格式兼容读取                  │
│  · CSV 导入导出          · 编码探测与转换                       │
└───────────────────────────────────────────────────────────────┘

          横切关注点：console.h（颜色 / 清屏 / 输入 / 代码页 RAII）
                     text_util.h（UTF-8 显示宽度）
                     encoding.h（UTF-8 ↔ GBK ↔ 宽字符路径）
```

**依赖单向**：表现层 → 业务层 → 持久化层。反向没有任何依赖：
持久化层不知道`WarehouseService` 存在，业务层不知道菜单长什么样，表现层不直接读写文件。

### 为什么这样切

| 切分 | 动机 |
|---|---|
| 表现层独立 | 业务规则可以脱离控制台被测试；换 UI 不动业务 |
| 业务层持有索引 | 索引是"加速手段"，属于业务实现细节，不应泄漏到表现层 |
| 持久化层独立 | 文件格式演进（WHv1 → WHv2）不影响业务逻辑；CSV 交换也归这里 |
| 数据结构 header-only | 模板必须可见才能实例化；同时让测试与基准直接复用，无需链接 |

---

## 二、三索引协同

同一个商品集合被三个结构同时持有，各自负责一类访问路径：

| 结构 | 映射 | 支撑的操作 | 复杂度 |
|---|---|---|---|
| `HashTable<int, Item>` | 编号 → 商品实体 | 按编号增删改查、判重 | `O(1)` 平均 |
| `AVLTree<double, int>` | 价格 → 编号集合 | 价格排序、价格区间查询 | `O(log n)` / `O(log n + k)` |
| `AVLTree<int, int>` | 库存 → 编号集合 | 库存排序、低库存告警 | `O(log n)` / `O(log n + k)` |
| `DoublyLinkedList<HistoryEntry>` | 有序日志流 | 追加审计、倒序展示、窗口淘汰 | `O(1)` 尾插 |

**为什么用"价格 → 编号"而不是"价格 → 商品实体"**：
实体只存一份（在哈希表里），索引只存轻量的整型编号。这样更新商品时不必在多处同步实体副本，
避免出现"索引里的价格是旧的、实体里的价格是新的"这类不一致。

**为什么重复键挂在节点桶里**：1000 件商品可能有大量相同价格。若每个（价格, 编号）组合都建节点，
索引规模会膨胀且大量节点只有单个孩子，反而降低平衡树的检索效率。把同价商品收进节点的 `values` 桶，
节点数等于"不同价格数"，中序遍历时再展开桶即可。

---

## 三、核心不变量

> **任何写操作完成后，三套索引的元素集合必须与主索引完全一致。**

这是整个项目最容易出错的地方——任何一个 `add` / `update` / `remove` 漏掉某个索引的同步，
就会产生"按价格搜不到、按编号却存在"的幽灵数据。

### 维护方式

```cpp
void WarehouseService::indexAdd(const Item& item) {
    byPrice_.insert(item.price,    item.id);
    byStock_.insert(item.quantity, item.id);
}

void WarehouseService::indexRemove(const Item& item) {
    byPrice_.erase(item.price,    item.id);
    byStock_.erase(item.quantity, item.id);
}
```

写操作的统一模式：

```cpp
Result WarehouseService::update(int id, const ItemPatch& patch) {
    Item* existing = byId_.find(id);
    if (!existing) return Result::failure("编号不存在。");

    const Item before = *existing;   // 先留快照
    Item after = before;
    /* 应用 patch 并校验 */
    indexRemove(before);             // 按旧键摘除
    *existing = after;
    indexAdd(after);                 // 按新键挂回
    ...
}
```

**关键点**：`indexRemove` 必须用**修改前的快照**。若用修改后的对象去摘除，
价格已经变了，`AVLTree::erase(新价格, id)` 找不到对应节点，旧键就成了永久孤儿。

### 校验方式

`selfCheck()` 从四个维度验证：

1. 元素数：`byPrice_.size() == byStock_.size() == byId_.size()`
2. AVL 结构：`validate()` 检查 BST 有序性 + 高度记录 + 平衡因子 ≤ 1 + 节点/元素计数
3. 反向回查：遍历主索引，每个商品都能在其价格桶与库存桶中找到自己的编号
4. 诊断输出：桶数 / 最长链 / 树高，供人工判断哈希函数与平衡性是否健康

单元测试在**每次增删改之后**都调用一次 `selfCheck()`，任一处漏同步立刻暴露——
这条规则在开发期抓到了 AVL 删除路径上的节点计数 bug（详见 [04-engineering-notes.md](04-engineering-notes.md)）。

---

## 四、数据流

### 启动

```
main()
 ├─ ConsoleGuard         切控制台到 UTF-8（析构时还原）
 ├─ 取可执行文件所在目录  以 exe 为基准，避免"双击 vs 命令行跑出两份数据"
 ├─ Repository(dir)      定位 warehouse.dat / history.dat
 ├─ WarehouseService     依次 loadItems / loadHistory，逐条建三套索引
 └─ ConsoleApp::run()    进入菜单循环
```

旧格式迁移在此处发生：读到 WHv1 时立即按 WHv2 回写一次，下次启动不再走兼容分支。

### 写操作

```
用户输入 → 表现层采集与格式校验
        → 业务层做领域校验（编号唯一、名称长度、库存/价格范围）
        → 改主索引 + 改有序索引 + 追加日志
        → 表现层调用 persistOrWarn() → Repository 原子落盘
```

**为什么不在业务层里直接落盘**：批量导入 1 万条时逐条落盘会产生 1 万次全量序列化，
复杂度退化为 `O(n²)`。把"何时落盘"的决策上移到表现层后，单次导入只写一次盘。

代价是崩溃可能丢失"最后一次操作之后"的内存变更。这个取舍对单机控制台工具是可接受的，
如果要求更高，可以在业务层加一个脏标记 + 定时 flush。

### 读操作

| 请求 | 路径 |
|---|---|
| 按编号查 | 哈希表直接命中 |
| 按价格/库存排序 | AVL 中序遍历，天然有序，**不做比较排序** |
| 按编号/名称排序 | 取全量 → **排序下标** → 按下标取回（避免搬移大对象） |
| 价格区间 | AVL 范围查询 → 命中编号 → 回哈希表取实体 |
| 低库存 | 库存 AVL 范围查询 `[0, threshold]` |
| 名称模糊 | 遍历主索引 + 预编译 KMP 匹配器 |

---

## 五、模块职责与文件映射

| 层 | 头文件 | 实现 | 行数 |
|---|---|---|---|
| 表现 | `console_ui.h` | `console_ui.cpp` | 46 / 446 |
| 业务 | `service.h` | `service.cpp` | 111 / 373 |
| 持久化 | `repository.h` | `repository.cpp` | 55 / 469 |
| 控制台抽象 | `console.h` | `console.cpp` | 69 / 233 |
| 数据结构 | `hash_table.h` `avl_tree.h` `linked_list.h` | header-only | 182 / 316 / 136 |
| 算法 | `algorithms.h` | header-only | 368 |
| 文本与编码 | `text_util.h` `encoding.h` | header-only | 137 / 82 |
| 模型 | `item.h` `history.h` | header-only | 44 / 61 |
| 入口 | — | `main.cpp` | 79 |

---

## 六、可扩展点

| 需求 | 改动范围 | 说明 |
|---|---|---|
| 换 UI（Web / GUI） | 新增一个表现层实现 | 业务层接口不变 |
| 换存储（SQLite / 服务端） | 新增一个 `Repository` 实现 | 需先抽象为接口 |
| 名称搜索加速 | 业务层 + 新增 Trie/倒排索引 | 见下文限制 |
| 多字段复合排序 | `service.cpp` 的 `list()` | 增加比较器组合即可 |

### 已知架构取舍

- **`Repository` 未抽象为纯虚接口**：目前只有一个实现，过早抽象收益低。若接入数据库，应提取 `IStorage` 接口。
- **名称搜索无索引**：模糊匹配需扫描全部商品，`O(N·m)`。精确匹配场景可加名称哈希，模糊场景需 Trie 或倒排索引。
- **单线程**：无并发控制。若做服务化，需加读写锁或双缓冲快照。

---

<sub>文档维护：Zero ｜ 最后更新：2026-11-02</sub>
