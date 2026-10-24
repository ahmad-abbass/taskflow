#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace taskflow::ds {

template<class T>
class LinkedStack {
    struct Node {
        T value;
        std::unique_ptr<Node> next;
        explicit Node(T item) : value(std::move(item)) {}
    };
    std::unique_ptr<Node> head_;
    std::size_t size_ = 0;
public:
    LinkedStack() = default;
    LinkedStack(const LinkedStack&) = delete;
    LinkedStack& operator=(const LinkedStack&) = delete;
    ~LinkedStack() {
        while (head_) { auto old = std::move(head_); head_ = std::move(old->next); }
    }
    void push(T value) {
        auto node = std::make_unique<Node>(std::move(value));
        node->next = std::move(head_); head_ = std::move(node); ++size_;
    }
    const T& top() const {
        if (!head_) throw std::out_of_range("empty stack");
        return head_->value;
    }
    T pop() {
        if (!head_) throw std::out_of_range("empty stack");
        auto node = std::move(head_); head_ = std::move(node->next); --size_;
        return std::move(node->value);
    }
    T discardOldest() {
        if (!head_) throw std::out_of_range("empty stack");
        auto* link = &head_;
        while ((*link)->next) link = &(*link)->next;
        auto node = std::move(*link); --size_;
        return std::move(node->value);
    }
    std::size_t size() const noexcept { return size_; }
};

}
