# 04 · 工程笔记：踩过的坑

> 记录重构过程中真实碰到、且值得写下来的问题。每条按「现象 → 根因 → 修复 → 教训」组织。
> 这些都是调试器不会直接告诉你的东西。

---

## 1. 输入一个字母，菜单无限刷屏

**现象**：菜单选 `1`，误输入 `abc`，程序开始疯狂滚动打印菜单，只能杀进程。

**根因**：`cin >> int` 失败后不会消费缓冲区里的 `abc`，同时给 `cin` 置上 `failbit`。
之后的每次 `cin >> choice` 都立即失败（不阻塞、不读取、不改变缓冲区），
`choice` 保持未初始化值，`switch` 落到 `default`，循环再打印一次菜单——永久空转。

**修复**：彻底不用 `cin >>`，统一改为 `getline` + 显式解析：

```cpp
std::optional<int> parseInteger(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return std::nullopt;
    ...
}
```

`getline` 一定会消费掉一整行，无论内容是否合法。解析失败就提示后重新读一行，
输入流关闭（Ctrl+Z / 管道结束）时返回 `std::nullopt`，由上层优雅退出。

**教训**：`operator>>` 的失败语义对交互式输入是灾难性的。**任何时候都不要用它读用户输入。**

---

## 2. 中文路径下文件"打开成功但一个字节都没读到"

**现象**：程序明明在工作目录下写数据文件，`ofstream` 也没报错，但重启后数据全没了。
日志里只有一句"无法创建数据文件"。

**根因**：MSVC 的 `std::ifstream` / `std::ofstream` 接受 `const char*` 时，
会按**系统 ANSI 代码页**（中文系统即 GBK）解释路径字符串。
本项目内部字符串统一是 UTF-8，路径里含中文（`D:\...\实验 作业\...`）时，
传给系统就是一堆 UTF-8 字节被当成 GBK，路径非法 → 打开失败。

要命的是这个失败**很安静**：`loadItems()` 里 `if (!file) return true;` 被当成"首次运行没有数据文件"，
完全看不出是路径问题。

**修复**：所有路径转宽字符，走 `const wchar_t*` 重载：

```cpp
inline std::wstring utf8ToWide(const std::string& utf8);   // encoding.h

std::ifstream file(utf8ToWide(itemsPath_), std::ios::binary);
_wremove(utf8ToWide(path).c_str());
_wrename(utf8ToWide(from).c_str(), utf8ToWide(to).c_str());
```

**教训**：Windows 上的文件路径**永远走宽字符**。`std::filesystem::path` 也能解决，
但为了保持零第三方、零 C++17 实验特性依赖，用了最直接的 Win32 转换。
另外——"打开失败被静默当成没有文件"这种写法本身也是隐患，现在会在诊断页显示实际路径。

---

## 3. `std::numeric_limits<int>::min()` 编译不过

**现象**：

```
error C2589: "(":"::"右边的非法标记
error C2059: 语法错误:"(  "
```

**根因**：`<windows.h>` 默认定义 `min` / `max` 宏。`std::numeric_limits<int>::min()` 里的 `min`
被宏展开成 `(((a) < (b)) ? (a) : (b))`，于是 `::min()` 变成了 `::(`。

**修复**：在包含 `<windows.h>` **之前**定义 `NOMINMAX`：

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
```

构建脚本里也加了 `/DNOMINMAX`，双保险。

**教训**：这类宏污染只在"先包含 `windows.h` 再包含 STL"时爆发，
错误信息完全指向 STL 头文件，很容易误判成标准库问题。看到 `min`/`max` 相关语法错先查 win 宏。

---

## 4. AVL 删除节点后自检失败

**现象**：单元测试 `AvlEraseManyKeepsBalance` 报 `validate()` false，但插入-only 的用例全过。
且只在"删除有两个孩子的节点"时出现。

**根因**：删除有两个孩子的节点时，标准做法是把中序后继的内容搬到当前节点，
再物理删除后继节点。我最初在进入删除分支时先 `--nodes_`，又在递归摘除后继时再 `--nodes_` 一次：

```cpp
// 错误版本
--nodes_;                       // ← 这里减了一次
if (两个孩子的分支) {
    n->key = succ->key;
    n->right = detachNodeRec(...);   // ← 内部又减了一次
}
```

结果 `nodes_` 比实际节点数少 1，`validate()` 里 `nodeCount == nodes_` 的断言立刻失败。

**修复**：`nodes_` 只在**物理删除节点**的那一处自减。两个孩子的分支只搬内容、不删本体，
节点数由后继节点的删除负责：

```cpp
} else {
    removedValues += n->values.size();
    if (!n->left || !n->right) {
        --nodes_;                      // 只有这里减
        Node* child = n->left ? n->left : n->right;
        delete n;
        return child;
    }
    Node* succ = n->right;
    while (succ->left) succ = succ->left;
    n->key    = succ->key;
    n->values = succ->values;
    succ->values.clear();              // 先清空，避免后继被摘除时重复计入 removedValues
    n->right = detachNodeRec(n->right, succ->key, removedValues);
}
```

**教训**：**计数类不变量必须能自证**。如果 `validate()` 只校验 BST 序和平衡因子、
不校验"节点计数一致"，这个 bug 会让 `size()` 慢慢漂移，
表现为"界面显示 100 条，实际只有 99 条"——极难定位。自检断言是这里唯一高效的防线。

---

## 5. KMP 比朴素匹配还慢 3.5 倍

**现象**：把名称搜索从逐字符 `==` 换成教科书 KMP，基准反而从 5.9 ms 涨到 23 ms。

**根因**：两个堆分配被放进了热路径。

1. `kmpFind(text, pattern)` 每次调用都 `buildLps(pattern)` → 每条商品名都新建一个 `vector`。
   2 万条商品 × 20 轮 = 40 万次堆分配。
2. 为做大小写不敏感，每次调用还拷贝一份小写化的 text。

算法省下的回溯，远远不够支付这 40 万次 `malloc/free`。

**修复**：三步。

```cpp
class TextMatcher {
    explicit TextMatcher(std::string pattern);   // ① 模式预编译一次，N 次匹配间复用
    // ② 大小写折叠下沉为编译期模板参数，热路径无分支
    template <bool Fold> std::size_t findKmpImpl(const std::string& text) const;
    // ③ 按模式长度自适应
    if (pattern_.size() < kKmpThreshold) return findNaive(text);
};
```

修完后短模式仍有约 10% 的常数劣势，于是保留自适应分支：**短模式走朴素，长模式走 KMP**。
最终最坏情况提速 295 倍，常见场景与朴素持平。

**教训**：**理论复杂度只是下界，常数因子可以毁掉一切**。
优化必须带基准，且基准要覆盖"最坏情况"和"真实场景"两类——只测最坏情况会做出错误决策。

---

## 6. 中文表格永远对不齐

**现象**：用 `std::setw(20)` 对齐商品名，英文名对齐良好，中文名全部错位。

**根因**：`setw` 数的是**字节数**。UTF-8 下一个汉字 3 字节，终端只显示 2 列。
中文名占 27 字节但只显示 18 列，`setw(20)` 会再补 2 字节，
视觉上就多出 1 列，逐行累积后整张表歪掉。

**修复**：按 Unicode 码点解码，累加**显示列宽**，再按列宽补空格（`text_util.h`）：

```cpp
std::size_t displayWidth(const std::string& s);   // CJK 码点记 2 列，其余 1 列
std::string padRight(const std::string& s, std::size_t width);
std::string ellipsize(const std::string& s, std::size_t maxWidth);   // 截断且不破坏 UTF-8
```

**教训**：任何面向终端的中文排版都不能用 `setw` / `printf("%-20s")`。
另外 `ellipsize` 必须按码点截断——按字节截断会把一个汉字劈成两半，产生非法 UTF-8。

---

## 7. 批处理被写成了 `\r\r\n`

**现象**：脚本能跑，但某些行行为诡异（比如 `set "FORCE=0"` 之后 `FORCE` 比较失效）。

**根因**：用脚本批量修补 `.bat` 时，读出的是已含 `\r\n` 的文本，
写回时又指定了 `newline='\r\n'`，于是每个 `\n` 被再转换一次，变成 `\r\r\n`。
`cmd` 容错性很强，多数行仍能执行，但**行尾多出的 `\r` 会混进变量值**。

**修复**：规范化时先 `replace('\r', '')` 去掉全部 CR，再统一转 CRLF：

```python
s = s.replace('\r', '')      # 归一化：去掉所有 CR，此时行尾只剩 \n
s = patch(s)
s = s.replace('\n', '\r\n')  # 统一 CRLF
```

并加了校验：断言文件内 `b'\r\r\n'` 计数为 0。

**教训**：批量改文本前先明确"当前换行是什么"，改完必须验证字节级结构。
`cmd` 的容错让这个 bug 变得隐蔽——它不会报错，只会偶尔出错。

---

## 8. `-t` 参数没有重新编译测试

**现象**：先 `run.bat -b` 编译主程序，再 `run.bat -t` 跑测试，却提示"目标程序不存在"。

**根因**：脚本的"是否需要编译"判断只拿**主程序 exe** 跟源码时间戳比。
主程序刚编完，判定"已是最新"，直接跳到运行阶段——而测试 exe 根本还没编译过。

**修复**：按本次请求的目标选择要比对的产物：

```bat
set "TARGET=%EXE%"
if /i "%MODE%"=="test"  set "TARGET=%TESTS%"
if /i "%MODE%"=="bench" set "TARGET=%BENCH%"
```

**教训**：增量构建的判断粒度必须与目标粒度一致。
只有一个 `TARGET` 变量的脚本，天然不支持多目标——这属于设计时没想清楚，不是笔误。

---

## 9. v1 源码转 UTF-8 后中文全乱

**现象**：把 v1 的 GBK 源码直接转成 UTF-8 再编译，控制台里所有中文变乱码。

**根因**：v1 程序内部写的是 `SetConsoleOutputCP(CP_ACP)`（GBK 控制台），
而 UTF-8 源码 + `/utf-8` 编译后，窄字符串字面量在二进制里是 **UTF-8 字节**。
UTF-8 字节输出到 936 代码页的控制台 → 乱码。

**修复**：转成 **UTF-8 with BOM**，且**不加 `/utf-8`**。
MSVC 见到 BOM 会按 UTF-8 读源码，但**执行字符集仍是系统 ANSI(GBK)**，
编译产物里的中文字面量自动转回 GBK，与原版行为一字不差：

```bat
:: 注意：这里不能加 /utf-8
cl /nologo /std:c++17 /EHsc /O2 /MD /W3 /DUNICODE /D_UNICODE "src\warehouse_v1.cpp" ...
```

验证方式：编译后跑一遍，检查 stdout 字节是否为 GBK 编码。

**教训**：MSVC 的"源字符集"和"执行字符集"是两个独立开关。
`/utf-8` 会同时设置两者，对只改其一的场景反而有害。归档旧项目时应优先保证**行为不变**。

---

## 10. 测试宏里写模板类型会报"宏参数太多"

**现象**：

```cpp
CHECK(table.loadFactor() <= HashTable<int, int>::kMaxLoadFactor + 1e-9);
// error C4002: 宏函数的实际参数太多
```

**根因**：预处理器不认识模板尖括号，把 `HashTable<int, int>` 中间的逗号当成宏参数分隔符。

**修复**：先用 `using` 起别名，或在宏调用处加一层额外括号：

```cpp
using IntTable = HashTable<int, int>;
CHECK(table.loadFactor() <= IntTable::kMaxLoadFactor + 1e-9);
```

**教训**：自研宏写断言时，模板类型、带逗号的初始化列表都会被误解析。
要么加括号保护，要么统一走 `using` 别名。这也是为什么工程上更推荐用函数模板而不是宏。

---

## 附：一句话总结

这十个问题里，**只有第 4 个是"算法写错"**，其余九个都是环境、编码、工具链、脚本层面的问题。
真实项目的难度分布基本就是这样——算法本身往往不是最难的部分，
**难的是让它在一个有中文、有 BOM、有宏污染、有编码转换的真实环境里正确运行**。

---

<sub>文档维护：Zero ｜ 最后更新：2026-11-02</sub>
