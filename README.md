# 仓库管理系统 · 从课程设计到工程化重构

> 一个 C++17 控制台库存管理系统，保留了两个版本：**v1 课程提交版**（740 行单文件、`std::vector` + 线性扫描）与
> **v2 重构版**（分层架构、手写哈希表 / AVL 树 / 双向链表，带单元测试与性能基准）。
>
> 保留双版本的目的很直接：**让人看得见演进过程**——同一套业务语义，如何通过替换数据结构把 O(n) 变成 O(1)，并把收益量化出来。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey)
![Tests](https://img.shields.io/badge/Tests-203%20assertions%20passed-brightgreen)
![License](https://img.shields.io/badge/License-MIT-green)

---

## 目录结构

```
warehouse-management-system/
├── README.md                    本文件：项目总览与两版对照
├── CHANGELOG.md                 版本演进记录
├── LICENSE                      MIT
├── .gitignore
├── .gitattributes               换行与编码约定
│
├── docs/                        文档（两版共用）
│   ├── 01-architecture.md       架构设计与分层约定
│   ├── 02-data-structures.md    手写数据结构与算法详解
│   ├── 03-benchmark.md          性能基准报告（含原始数据）
│   ├── 04-engineering-notes.md  踩坑记录与工程细节
│   ├── 05-version-comparison.md v1 → v2 逐项差异对照
│   └── assets/
│       └── architecture.drawio  架构图（可用 draw.io / diagrams.net 打开）
│
├── v1-legacy/                   原始版：课程提交时的实现，仅做编码转换
│   ├── README.md                本版说明与已知缺陷清单
│   ├── run.bat                  一键编译运行
│   ├── sample_data.csv          导入样例（UTF-8 BOM）
│   └── src/
│       └── warehouse_v1.cpp     单文件源码（740 行）
│
└── v2-refactored/               重构版：分层架构 + 手写数据结构 + 测试 + 基准
    ├── README.md                本版说明与快速上手
    ├── CMakeLists.txt           CMake 工程（三目标 + CTest）
    ├── run.bat                  一键编译运行 / 测试 / 基准
    ├── sample_data.csv          导入样例（含逗号、引号等边界用例）
    ├── include/wh/              12 个头文件，数据结构为 header-only 模板
    ├── src/                     5 个源文件（表现层 / 业务层 / 持久化层 / 控制台抽象 / 入口）
    ├── tests/test_main.cpp      30 个用例 / 203 项断言
    └── bench/bench.cpp          性能基准
```

---

## 两版差异与用途

| 维度 | v1-legacy | v2-refactored |
|---|---|---|
| **定位** | 课程提交原稿，保持实现原貌 | 自选重构，面向工程实践与简历展示 |
| **用途** | 对照基线；用于展示"起点"与原始缺陷 | 主要成果；用于演示架构设计、算法选型与量化收益 |
| **代码规模** | 单文件 740 行 | 4274 行 / 12 头文件 + 5 源文件 |
| **主索引** | `vector` 线性扫描 `O(n)` | 手写哈希表 `O(1)` |
| **有序检索** | 无 | 手写 AVL ×2（价格 / 库存），`O(log n + k)` |
| **日志容器** | `vector` | 双向链表（`O(1)` 尾插 + 滚动窗口） |
| **排序算法** | 无 | 手写快排 / 归并 / 堆排 + 索引中序 |
| **字符串匹配** | 逐字符精确相等 | KMP 自适应模糊匹配 |
| **输入健壮性** | `cin >> int` 会死循环 | 全量校验 + EOF 优雅退出 |
| **持久化** | `sizeof(size_t)`、无校验 | 定宽类型 + 尾部校验 + 原子替换 + 兼容旧格式 |
| **文件编码** | GBK（已转 UTF-8 BOM 以适配 GitHub） | UTF-8 |
| **测试** | 无 | 30 用例 / 203 断言，零第三方依赖 |
| **可观测性** | 无 | 索引诊断页、哈希冲突统计、一致性自检 |

详细逐项对照见 [`docs/05-version-comparison.md`](docs/05-version-comparison.md)。

### 该看哪一版？

- **想了解项目做了什么** → 直接读 [v2-refactored/README.md](v2-refactored/README.md)
- **想看优化前后差多少** → 读 [docs/03-benchmark.md](docs/03-benchmark.md)
- **想看原始实现长什么样、有哪些坑** → 读 [v1-legacy/README.md](v1-legacy/README.md)

---

## 快速开始

### 环境要求

- Windows 10 / 11
- Visual Studio 2019 及以上（需勾选「使用 C++ 的桌面开发」工作负载）
- C++17

### 运行 v2（推荐）

```bat
cd v2-refactored
run.bat            :: 需要时编译，然后启动
run.bat -r         :: 强制重新编译
run.bat -t         :: 编译并运行单元测试
run.bat -p         :: 编译并运行性能基准
run.bat -b         :: 只编译不运行
```

脚本会通过 `vswhere` 自动定位 Visual Studio 并调用 `vcvars64.bat`，**不需要手动打开开发者命令提示符**。

也可以走 CMake：

```bat
cd v2-refactored
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

### 运行 v1

```bat
cd v1-legacy
run.bat            :: 编译并启动
run.bat -r         :: 强制重新编译
```

首次运行后可在菜单选 `8. 导入数据`，输入 `sample_data.csv` 载入示例数据。

---

## 核心性能数据（v2，实测）

> 环境：Windows 11 / 13th Gen Intel Core i9 / 32 GB RAM / MSVC 19.51 x64 `/O2`。完整数据、原始输出与分析见 [docs/03-benchmark.md](docs/03-benchmark.md)。
> 绝对值存在 ±30% 运行波动，量级与趋势稳定。

| 场景 | v1 做法 | v2 做法 | 提升 |
|---|---|---|---|
| 按编号查询（10 万条） | 线性扫描 39,725 ns/次 | 哈希表 **11 ns/次** | **约 3500×** |
| 构建索引查重（2 万条） | 线性查重 63.7 ms | 哈希查重 2.45 ms | 26.0× |
| 有序输出（20 万条） | 比较排序 42.57 ms | AVL 中序 4.82 ms | 8.8× |
| 价格区间查询（10 万条 ×200 次） | 全表扫描 57.9 ms | AVL 范围查询 14.5 ms | 4.0× |
| KMP 最坏情况（20 KB 文本） | 朴素匹配 32.3 ms | 自适应 KMP 0.109 ms | 295× |

AVL 顺序插入 10 万个键后树高仅 **17 层**（`log₂(10⁵) ≈ 16.6`），而普通 BST 会退化为 10 万层链表。

---

## 覆盖的功能

- 商品增删改查（编号唯一性校验、字段范围校验）
- 按编号 / 名称 / 库存 / 价格排序（升序 / 降序）
- 商品名称模糊搜索（KMP，大小写不敏感）
- 价格区间查询
- 低库存告警（阈值范围内全量命中）
- 操作日志（新增 / 修改 / 删除 / 导入 / 导出 / 排序，滚动窗口）
- CSV 导入导出（UTF-8 BOM、引号转义、GBK 自动识别、编号冲突策略）
- 索引一致性与哈希质量诊断页

---

## 文档索引

| 文档 | 内容 |
|---|---|
| [01 架构设计](docs/01-architecture.md) | 三层分层、索引协同、不变量与自检 |
| [02 数据结构](docs/02-data-structures.md) | 哈希表 / AVL / 双向链表 / 排序 / KMP 的实现要点与复杂度 |
| [03 性能基准](docs/03-benchmark.md) | 测试方法、原始数据、结论分析 |
| [04 工程笔记](docs/04-engineering-notes.md) | 中文路径、编码、AVL 删除、KMP 选型等踩坑记录 |
| [05 版本对照](docs/05-version-comparison.md) | v1 与 v2 的逐文件 / 逐指标差异 |

---

## 许可

[MIT](LICENSE) © 2026 Zero

---

<sub>文档维护：Zero ｜ 最后更新：2026-11-02</sub>
