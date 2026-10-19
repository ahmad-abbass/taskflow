#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace taskflow::ds {

template <typename T>
class LinkedQueue {
    struct Node {
        T value;
        std::unique_ptr<Node> next;
        explicit Node(T item) : value(std::move(item)) {}
    };

    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;

public:
    LinkedQueue() = default;
    LinkedQueue(const LinkedQueue&) = delete;
    LinkedQueue& operator=(const LinkedQueue&) = delete;

    LinkedQueue(LinkedQueue&& other) noexcept
        : head_(std::move(other.head_)), tail_(std::exchange(other.tail_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}

    LinkedQueue& operator=(LinkedQueue&& other) noexcept {
        if (this != &other) {
            clear();
            head_ = std::move(other.head_);
            tail_ = std::exchange(other.tail_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    ~LinkedQueue() { clear(); }

    void push(T value) {
        auto node = std::make_unique<Node>(std::move(value));
        Node* last = node.get();
        if (tail_) tail_->next = std::move(node);
        else head_ = std::move(node);
        tail_ = last;
        ++size_;
    }

    T pop() {
        if (empty()) throw std::out_of_range("queue is empty");
        T value = std::move(head_->value);
        head_ = std::move(head_->next);
        if (!head_) tail_ = nullptr;
        --size_;
        return value;
    }

    const T& front() const {
        if (empty()) throw std::out_of_range("queue is empty");
        return head_->value;
    }

    bool empty() const noexcept { return size_ == 0; }
    std::size_t size() const noexcept { return size_; }

    void clear() noexcept {
        while (head_) head_ = std::move(head_->next);
        tail_ = nullptr;
        size_ = 0;
    }
};

}
