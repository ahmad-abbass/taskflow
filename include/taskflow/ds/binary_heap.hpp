#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace taskflow::ds {

template<class T, class Before>
class BinaryHeap {
    std::vector<T> values_;
    Before before_;
public:
    void push(T value) {
        values_.push_back(std::move(value));
        auto index = values_.size() - 1;
        while (index) {
            const auto parent = (index - 1) / 2;
            if (!before_(values_[index], values_[parent])) break;
            std::swap(values_[index], values_[parent]);
            index = parent;
        }
    }
    const T& top() const {
        if (empty()) throw std::out_of_range("empty heap");
        return values_.front();
    }
    T pop() {
        if (empty()) throw std::out_of_range("empty heap");
        T result = std::move(values_.front());
        if (values_.size() == 1) { values_.pop_back(); return result; }
        values_.front() = std::move(values_.back());
        values_.pop_back();
        std::size_t index = 0;
        while (index * 2 + 1 < values_.size()) {
            auto child = index * 2 + 1;
            if (child + 1 < values_.size() && before_(values_[child + 1], values_[child])) ++child;
            if (!before_(values_[child], values_[index])) break;
            std::swap(values_[index], values_[child]);
            index = child;
        }
        return result;
    }
    bool empty() const noexcept { return values_.empty(); }
    std::size_t size() const noexcept { return values_.size(); }
};

}
