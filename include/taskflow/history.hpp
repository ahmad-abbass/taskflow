#pragma once

#include "taskflow/ds/linked_stack.hpp"
#include "taskflow/scheduler.hpp"

namespace taskflow {

class UndoHistory {
    ds::LinkedStack<std::string> states_;
    std::size_t bytes_ = 0;
    static constexpr std::size_t maxBytes = 32 * 1024 * 1024;
public:
    void remember(std::string state) {
        if (state.size() > maxBytes) throw std::length_error("undo snapshot too large");
        const auto added = state.size();
        states_.push(std::move(state)); bytes_ += added;
        while (states_.size() > 20 || bytes_ > maxBytes) bytes_ -= states_.discardOldest().size();
    }
    void undo(Scheduler& scheduler) {
        scheduler.restore(states_.top());
        bytes_ -= states_.pop().size();
    }
    std::size_t size() const noexcept { return states_.size(); }
};

}
