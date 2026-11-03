# v2 · 重构版

> 分层架构 + 三套手写索引（哈希表 / AVL ×2 / 双向链表）+ 手写算法库（快排 / 归并 / 堆排 / 二分 / KMP）
> + 单元测试 + 性能基准。C++17，零第三方依赖。

---

## 快速开始

```bat
run.bat            :: 需要时编译，然后启动
run.bat -r         :: 强制重新编译
run.bat -t         :: 编译并运行单元测试
run.bat -p         :: 编译并运行性能基准
run.bat -b         :: 只编译不运行
```

脚本自动通过 `vswhere` 定位 Visual Studio 并调用 `vcvars64.bat`，**无需手动打开开发者命令提示符**。

### 或使用 CMake

```bat
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

也可以直接用 Visual Studio 打开本目录（"打开文件夹"），CMake 工程会被自动识别。

---

## 目录结构

```
v2-refactored/
├── CMakeLists.txt          三目标（warehouse / wh_tests / wh_bench）+ CTest
├── run.bat                 一键构建与运行
├── sample_data.csv         导入样例（含逗号、引号等边界用例）
│
├── include/wh/             12 个头文件，数据结构为 header-only 模板
│   ├── hash_table.h        链地址法哈希表
│   ├── avl_tree.h          AVL 平衡树（多重集 + 区间查询）
│   ├── linked_list.h       双向链表（日志滚动窗口）
│   ├── algorithms.h        快排 / 归并 / 堆排 / 二分 / KMP
│   ├── text_util.h         UTF-8 显示宽度与表格对齐
│   ├── encoding.h          UTF-8/GBK 探测、宽字符路径
│   ├── item.h              商品模型
│   ├── history.h           日志模型
│   ├── repository.h        持久化接口
│   ├── service.h           业务服务接口
│   ├── console.h           控制台抽象（颜色/清屏/健壮输入/代码页 RAII）
│   └── console_ui.h        表现层接口
│
├── src/
│   ├── main.cpp            入口
│   ├── console.cpp         控制台抽象实现
│   ├── console_ui.cpp      表现层
│   ├── service.cpp         业务层
│   └── repository.cpp      持久化层
│
├── tests/test_main.cpp     30 个用例 / 203 项断言
└── bench/bench.cpp         性能基准
```

---

## 功能

| 菜单 | 功能 |
|---|---|
| 1 | 新增商品（编号唯一性、名称长度、库存/价格范围校验） |
| 2 | 商品列表 / 排序（编号、名称、库存、价格；升序/降序） |
| 3 | 搜索商品（名称模糊匹配，KMP，大小写不敏感） |
| 4 | 价格区间查询（AVL 范围查询） |
| 5 | 低库存告警（库存 AVL 范围查询） |
| 6 | 修改商品（回车保持原值） |
| 7 | 删除商品（二次确认） |
| 8 | 操作日志（最近 2000 条，最新在前） |
| 9 | 导出 CSV（UTF-8 BOM，可直接用 Excel 打开） |
| 10 | 导入 CSV（自动识别编码，编号冲突可选覆盖/跳过/取消） |
| 11 | 索引诊断（桶数、最长链、AVL 高度、一致性自检） |
| 0 | 退出（自动落盘） |

首次运行后选 `10`，输入 `sample_data.csv` 可载入示例数据。

---

## 核心设计

### 三索引协同

| 结构 | 映射 | 支撑 |
|---|---|---|
| `HashTable<int, Item>` | 编号 → 商品 | 主索引，`O(1)` 增删改查 |
| `AVLTree<double, int>` | 价格 → 编号集合 | 价格排序、区间查询 |
| `AVLTree<int, int>` | 库存 → 编号集合 | 库存排序、低库存告警 |
| `DoublyLinkedList<HistoryEntry>` | 有序日志流 | 审计、倒序展示、窗口淘汰 |

**不变量**：任何写操作完成后，三套索引的元素集合必须与主索引完全一致。
`selfCheck()` 从元素数、AVL 结构合法性、逐条回查三个维度验证；单元测试在每次增删改后都调用一次。

### 关键取舍

- **索引只存编号，不存实体副本**：实体单点存放于哈希表，避免多处同步
- **重复键挂在 AVL 节点桶里**：节点数 = 不同价格数，避免索引膨胀
- **排序下标而非元素**：`Item` 含 `std::string`，排下标避免大对象搬移（快 1.6 倍）
- **写操作不落盘**：由表现层择机 `flush()`，批量导入只写一次盘
- **自适应 KMP**：模式短于 8 字节走朴素匹配（实测更快），否则走 KMP

详见 [../docs/01-architecture.md](../docs/01-architecture.md) 与 [../docs/02-data-structures.md](../docs/02-data-structures.md)。

---

## 性能（对照 v1 的 `vector` + 线性扫描）

| 场景 | v1 | v2 | 提升 |
|---|---|---|---|
| 按编号查询（10 万条） | 39,725 ns/次 | **11 ns/次** | 约 3500× |
| 构建索引查重（2 万条） | 63.7 ms | 2.45 ms | 26× |
| 有序输出（20 万条） | 42.6 ms | 4.82 ms | 8.8× |
| 价格区间查询（10 万条） | 57.9 ms | 14.5 ms | 4.0× |
| KMP 最坏情况 | 32.3 ms | 0.109 ms | 295× |
| AVL 树高（10 万顺序插入） | 退化为 10 万层 | **17 层** | — |

完整数据与分析方法见 [../docs/03-benchmark.md](../docs/03-benchmark.md)。

---

## 测试

```bat
run.bat -t
```

```
=== 单元测试 ===
  [通过] HashTableBasic
  [通过] HashTableGrowAndRehash
  ...
断言 203 项，失败 0 项
```

30 个用例，覆盖：

- 哈希表增删改查、扩容 rehash、连续键的冲突分布
- AVL 平衡性（顺序插入）、重复键桶化、按值删除、大量删除后仍平衡、区间查询、`validate()` 自检
- 双向链表窗口淘汰
- 四种排序的正确性、边界（空/单元素/全相等/逆序）、归并排序的稳定性
- 二分查找命中与未命中
- KMP 与朴素算法逐例对拍、最坏情况线性
- UTF-8 显示宽度、安全截断、编码探测
- 业务层三索引一致性、导入冲突策略
- 持久化往返、CSV 引号转义、畸形数据拒绝、截断文件检测

测试框架是自研的 20 行断言宏，保持**零第三方依赖**。

---

## 环境要求

- Windows 10 / 11
- Visual Studio 2019 及以上（勾选「使用 C++ 的桌面开发」）
- C++17
- 仅使用 Win32 控制台 API，无其他外部依赖

> 项目依赖 `SetConsoleOutputCP` / `GetModuleFileNameW` 等 Win32 接口，**仅支持 Windows**。
> CMakeLists 中已加入平台检查，非 Windows 会直接报错退出。

---

<sub>文档维护：Zero ｜ 最后更新：2026-11-02</sub>
