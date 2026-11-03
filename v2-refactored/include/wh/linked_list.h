// =============================================================================
//  linked_list.h —— 双向链表（操作日志的底层容器）
//
//  为什么日志用链表而不是 vector：
//    日志只做尾部追加 + 反向遍历，且规模会持续增长。
//    vector 扩容时整体搬迁 O(n)，而链表尾插是稳定的 O(1)（无搬迁）。
//    同时链表支持在 O(1) 时间内丢弃最旧记录（头部弹出），实现滚动窗口。
//
//  复杂度：尾插 O(1)、头删 O(1)、正/反向遍历 O(n)、随机访问 O(n)（不提供）
// =============================================================================
#pragma once

#include <cstddef>
#include <vector>

namespace wh {

template <class T>
class DoublyLinkedList {
public:
    DoublyLinkedList() = default;
    ~DoublyLinkedList() { clear(); }

    DoublyLinkedList(const DoublyLinkedList&) = delete;
    DoublyLinkedList& operator=(const DoublyLinkedList&) = delete;

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        Node* p = head_;
        while (p) {
            Node* next = p->next;
            delete p;
            p = next;
        }
        head_ = tail_ = nullptr;
        size_ = 0;
    }

    // 设置容量上限，超出后自动丢弃最旧记录（日志滚动窗口）
    void setCapacity(std::size_t cap) {
        capacity_ = cap;
        while (capacity_ > 0 && size_ > capacity_) popFront();
    }

    std::size_t capacity() const { return capacity_; }

    void pushBack(const T& value) {
        Node* fresh = new Node{value, tail_, nullptr};
        if (tail_) {
            tail_->next = fresh;
        } else {
            head_ = fresh;
        }
        tail_ = fresh;
        ++size_;
        if (capacity_ > 0 && size_ > capacity_) popFront();
    }

    void pushFront(const T& value) {
        Node* fresh = new Node{value, nullptr, head_};
        if (head_) {
            head_->prev = fresh;
        } else {
            tail_ = fresh;
        }
        head_ = fresh;
        ++size_;
    }

    bool popFront() {
        if (!head_) return false;
        Node* old = head_;
        head_ = old->next;
        if (head_) {
            head_->prev = nullptr;
        } else {
            tail_ = nullptr;
        }
        delete old;
        --size_;
        return true;
    }

    bool popBack() {
        if (!tail_) return false;
        Node* old = tail_;
        tail_ = old->prev;
        if (tail_) {
            tail_->next = nullptr;
        } else {
            head_ = nullptr;
        }
        delete old;
        --size_;
        return true;
    }

    // 正向遍历（从最旧到最新）
    template <class Fn>
    void forEach(Fn fn) const {
        for (const Node* p = head_; p; p = p->next) fn(p->value);
    }

    // 反向遍历（从最新到最旧）—— 日志展示用这个顺序
    template <class Fn>
    void forEachReverse(Fn fn) const {
        for (const Node* p = tail_; p; p = p->prev) fn(p->value);
    }

    // 取最近 n 条（最新在前）
    std::vector<T> takeRecent(std::size_t n) const {
        std::vector<T> out;
        if (n == 0) return out;
        out.reserve(n < size_ ? n : size_);
        for (const Node* p = tail_; p && out.size() < n; p = p->prev) {
            out.push_back(p->value);
        }
        return out;
    }

private:
    struct Node {
        T value;
        Node* prev;
        Node* next;
    };

    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;  // 0 表示不限制
};

}  // namespace wh
