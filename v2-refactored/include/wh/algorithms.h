// =============================================================================
//  algorithms.h —— 手写排序 / 查找 / 字符串匹配算法
//
//  内容：
//    1. 快速排序（三数取中 + 三路分区，处理大量重复键不退化）
//    2. 归并排序（稳定排序，用于需要保持原始相对顺序的场景）
//    3. 堆排序（原地 O(1) 额外空间，最坏仍 O(n log n)）
//    4. 插入排序（小数组快排的收尾优化）
//    5. 二分查找 / lower_bound（配合 AVL 中序结果做 O(log n) 定位）
//    6. KMP 字符串匹配（商品名模糊搜索，避免朴素算法的回溯）
//
//  说明：KMP 在 UTF-8 字节流上按字节匹配。UTF-8 具有自同步性，
//        合法模式的字节匹配必然落在字符边界上，因此对中文同样正确。
// =============================================================================
#pragma once

#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace wh {

// ---------------------------------------------------------------------------
//  插入排序：O(n^2) 最坏，但小规模（< 16）时因常数因子极小而快于递归排序
// ---------------------------------------------------------------------------
template <class It, class Comp>
void insertionSort(It first, It last, Comp comp) {
    if (first == last) return;
    for (It i = std::next(first); i != last; ++i) {
        auto key = std::move(*i);
        It j = i;
        while (j != first && comp(key, *std::prev(j))) {
            *j = std::move(*std::prev(j));
            --j;
        }
        *j = std::move(key);
    }
}

// 三数取中：返回 a/b/c 的中位数引用（调用方必须拷贝，因为分区会移动元素）
template <class T, class Comp>
const T& medianOfThree(const T& a, const T& b, const T& c, Comp comp) {
    if (comp(a, b)) {
        if (comp(b, c)) return b;
        return comp(a, c) ? c : a;
    }
    if (comp(a, c)) return a;
    return comp(b, c) ? c : b;
}

// ---------------------------------------------------------------------------
//  快速排序：平均 O(n log n)，最坏 O(n^2)（三数取中后概率极低）
//           三路分区让「大量相等键」退化为 O(n)
//           递归深度控制在 O(log n)：只递归较小的一侧，另一侧循环处理
// ---------------------------------------------------------------------------
template <class It, class Comp>
void quickSortRec(It first, It last, Comp comp) {
    while (std::distance(first, last) > 1) {
        const auto n = std::distance(first, last);
        if (n < 16) {
            insertionSort(first, last, comp);
            return;
        }

        auto pivot = medianOfThree(*first, *std::next(first, n / 2), *std::prev(last), comp);

        // 三路分区：[first, lt) 小于 pivot，[lt, gt] 等于 pivot，(gt, last) 大于 pivot
        It lt = first;
        It cur = first;
        It gt = std::prev(last);
        while (cur <= gt) {
            if (comp(*cur, pivot)) {
                std::iter_swap(lt, cur);
                ++lt;
                ++cur;
            } else if (comp(pivot, *cur)) {
                std::iter_swap(cur, gt);
                --gt;
            } else {
                ++cur;
            }
        }

        // 先递归较短的一侧，保证栈深度 O(log n)
        const auto leftLen = std::distance(first, lt);
        const auto rightLen = std::distance(gt, last) - 1;
        if (leftLen < rightLen) {
            quickSortRec(first, lt, comp);
            first = std::next(gt);
        } else {
            quickSortRec(std::next(gt), last, comp);
            last = lt;
        }
    }
}

template <class It, class Comp>
void quickSort(It first, It last, Comp comp) {
    quickSortRec(first, last, comp);
}

// ---------------------------------------------------------------------------
//  归并排序：稳定 O(n log n)，额外空间 O(n)
// ---------------------------------------------------------------------------
template <class T, class Comp>
void mergeSortRec(std::vector<T>& v, std::vector<T>& buf, std::size_t lo, std::size_t hi,
                  Comp& comp) {
    if (hi - lo < 2) return;
    const std::size_t mid = lo + (hi - lo) / 2;
    mergeSortRec(v, buf, lo, mid, comp);
    mergeSortRec(v, buf, mid, hi, comp);

    std::size_t i = lo;
    std::size_t j = mid;
    std::size_t k = lo;
    while (i < mid && j < hi) {
        // 用 <= 保证稳定性：相等时优先取左半部分
        if (comp(v[j], v[i])) {
            buf[k++] = v[j++];
        } else {
            buf[k++] = v[i++];
        }
    }
    while (i < mid) buf[k++] = v[i++];
    while (j < hi) buf[k++] = v[j++];
    for (std::size_t t = lo; t < hi; ++t) v[t] = std::move(buf[t]);
}

template <class T, class Comp>
void mergeSort(std::vector<T>& v, Comp comp) {
    if (v.size() < 2) return;
    std::vector<T> buf(v.size());
    mergeSortRec(v, buf, 0, v.size(), comp);
}

// ---------------------------------------------------------------------------
//  堆排序：O(n log n) 最坏保证，原地 O(1) 额外空间，不稳定
// ---------------------------------------------------------------------------
template <class T, class Comp>
void siftDown(std::vector<T>& v, std::size_t start, std::size_t end, Comp& comp) {
    std::size_t root = start;
    while (true) {
        std::size_t child = 2 * root + 1;
        if (child >= end) break;
        // 选较大的孩子（comp 为 less 时构建大顶堆）
        if (child + 1 < end && comp(v[child], v[child + 1])) ++child;
        if (comp(v[root], v[child])) {
            std::swap(v[root], v[child]);
            root = child;
        } else {
            break;
        }
    }
}

template <class T, class Comp>
void heapSort(std::vector<T>& v, Comp comp) {
    const std::size_t n = v.size();
    if (n < 2) return;
    // 自底向上建堆：O(n)
    for (std::size_t i = n / 2; i-- > 0;) {
        siftDown(v, i, n, comp);
    }
    // 逐个摘出堆顶放到末尾
    for (std::size_t end = n - 1; end > 0; --end) {
        std::swap(v[0], v[end]);
        siftDown(v, 0, end, comp);
    }
}

// ---------------------------------------------------------------------------
//  二分查找
// ---------------------------------------------------------------------------
// 返回第一个不小于 value 的位置
template <class It, class T, class Comp>
It lowerBound(It first, It last, const T& value, Comp comp) {
    auto len = std::distance(first, last);
    while (len > 0) {
        const auto half = len / 2;
        It mid = std::next(first, half);
        if (comp(*mid, value)) {
            first = std::next(mid);
            len -= half + 1;
        } else {
            len = half;
        }
    }
    return first;
}

// 命中返回指向该元素的迭代器，否则返回 last
template <class It, class T, class Comp>
It binarySearch(It first, It last, const T& value, Comp comp) {
    It hit = lowerBound(first, last, value, comp);
    if (hit != last && !comp(value, *hit)) return hit;
    return last;
}

// ---------------------------------------------------------------------------
//  KMP 字符串匹配
// ---------------------------------------------------------------------------
// 构造 next / lps 数组：lps[i] = pattern[0..i] 的最长相等前后缀长度
inline std::vector<std::size_t> buildLps(const std::string& pattern) {
    std::vector<std::size_t> lps(pattern.size(), 0);
    std::size_t len = 0;
    for (std::size_t i = 1; i < pattern.size();) {
        if (pattern[i] == pattern[len]) {
            lps[i] = ++len;
            ++i;
        } else if (len > 0) {
            len = lps[len - 1];  // 回退到上一个可能的前缀
        } else {
            lps[i] = 0;
            ++i;
        }
    }
    return lps;
}

// 返回首次出现位置，未找到返回 std::string::npos。时间 O(n + m)
inline std::size_t kmpFind(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) return 0;
    if (text.size() < pattern.size()) return std::string::npos;

    const std::vector<std::size_t> lps = buildLps(pattern);
    std::size_t i = 0;  // text 指针
    std::size_t j = 0;  // pattern 指针
    while (i < text.size()) {
        if (text[i] == pattern[j]) {
            ++i;
            ++j;
            if (j == pattern.size()) return i - j;
        } else if (j > 0) {
            j = lps[j - 1];  // 关键：文本指针不回退
        } else {
            ++i;
        }
    }
    return std::string::npos;
}

// ---------------------------------------------------------------------------
//  TextMatcher —— 预编译的 KMP 模式
//
//  为什么需要它：一次搜索要在 N 条商品名上做匹配。若每条都重新构造 lps 数组，
//  就会产生 N 次堆分配，常数开销完全盖过算法本身的优势（实测反而比朴素算法慢 3 倍）。
//  把模式预编译一次、在 N 次匹配间复用，才是 KMP 的正确用法。
//
//  同时把 ASCII 大小写折叠下推到字符比较里，避免为每段文本额外拷贝一份小写副本。
// ---------------------------------------------------------------------------
class TextMatcher {
public:
    explicit TextMatcher(std::string pattern) : pattern_(std::move(pattern)) {
        asciiFold_ = !pattern_.empty();
        for (char c : pattern_) {
            if (static_cast<unsigned char>(c) >= 0x80) {
                asciiFold_ = false;  // 含多字节字符时禁止折叠，避免破坏 UTF-8
                break;
            }
        }
        if (asciiFold_) {
            for (char& c : pattern_) c = foldAscii(c);
        }
        lps_ = buildLps(pattern_);
    }

    bool empty() const { return pattern_.empty(); }
    const std::string& pattern() const { return pattern_; }
    bool usesKmp() const { return pattern_.size() >= kKmpThreshold; }

    std::size_t find(const std::string& text) const {
        if (pattern_.empty()) return 0;
        if (text.size() < pattern_.size()) return std::string::npos;
        // 实测结论：模式很短时，KMP 的 lps 间接寻址开销大于它省下的回溯，
        // 朴素匹配反而更快。所以按模式长度自适应选择算法，而不是无脑上 KMP。
        if (pattern_.size() < kKmpThreshold) return findNaive(text);
        return findKmp(text);
    }

    bool contains(const std::string& text) const { return find(text) != std::string::npos; }

private:
    // 模式短于该字节数时改用朴素匹配（阈值由 bench 实测得出）
    static constexpr std::size_t kKmpThreshold = 8;

    static char foldAscii(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::size_t findNaive(const std::string& text) const {
        return asciiFold_ ? findNaiveImpl<true>(text) : findNaiveImpl<false>(text);
    }

    std::size_t findKmp(const std::string& text) const {
        return asciiFold_ ? findKmpImpl<true>(text) : findKmpImpl<false>(text);
    }

    // Fold 作为编译期常量：避免在逐字节比较的热路径上反复判断
    template <bool Fold>
    std::size_t findNaiveImpl(const std::string& text) const {
        const std::size_t m = pattern_.size();
        const std::size_t limit = text.size() - m;
        for (std::size_t i = 0; i <= limit; ++i) {
            std::size_t j = 0;
            while (j < m && foldChar<Fold>(text[i + j]) == pattern_[j]) ++j;
            if (j == m) return i;
        }
        return std::string::npos;
    }

    template <bool Fold>
    std::size_t findKmpImpl(const std::string& text) const {
        const std::size_t m = pattern_.size();
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < text.size()) {
            if (foldChar<Fold>(text[i]) == pattern_[j]) {
                ++i;
                ++j;
                if (j == m) return i - j;
            } else if (j > 0) {
                j = lps_[j - 1];  // 文本指针不回退，这是 KMP 的核心
            } else {
                ++i;
            }
        }
        return std::string::npos;
    }

    template <bool Fold>
    static char foldChar(char c) {
        if constexpr (Fold) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        } else {
            return c;
        }
    }

    std::string pattern_;
    std::vector<std::size_t> lps_;
    bool asciiFold_ = true;
};

inline bool kmpContains(const std::string& text, const std::string& pattern) {
    return kmpFind(text, pattern) != std::string::npos;
}

// 朴素匹配：仅用于性能对比基准，证明 KMP 的常数与最坏情况优势
inline std::size_t naiveFind(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) return 0;
    if (text.size() < pattern.size()) return std::string::npos;
    for (std::size_t i = 0; i + pattern.size() <= text.size(); ++i) {
        std::size_t j = 0;
        while (j < pattern.size() && text[i + j] == pattern[j]) ++j;
        if (j == pattern.size()) return i;
    }
    return std::string::npos;
}

// ASCII 大小写无关的包含判断（中文按字节原样比较）
inline bool containsIgnoreCaseAscii(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) return true;
    return TextMatcher(pattern).contains(text);
}

}  // namespace wh
