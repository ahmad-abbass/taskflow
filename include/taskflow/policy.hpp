#pragma once

#include "taskflow/ds/binary_heap.hpp"
#include "taskflow/ds/linked_queue.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace taskflow {

class Task;

struct ReadyItem {
    Task* task;
    int priority;
    std::uint64_t sequence;
    std::uint64_t version;
};

struct OlderFirst {
    bool operator()(const ReadyItem& a, const ReadyItem& b) const noexcept { return a.sequence < b.sequence; }
};

struct HigherPriority {
    bool operator()(const ReadyItem& a, const ReadyItem& b) const noexcept {
        return a.priority != b.priority ? a.priority > b.priority : a.sequence < b.sequence;
    }
};

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    virtual void push(ReadyItem item) = 0;
    virtual ReadyItem pop() = 0;
    virtual bool empty() const noexcept = 0;
    virtual std::string name() const = 0;
};

class FifoPolicy final : public SchedulingPolicy {
    ds::LinkedQueue<ReadyItem> items_;
public:
    void push(ReadyItem item) override { items_.push(item); }
    ReadyItem pop() override { return items_.pop(); }
    bool empty() const noexcept override { return items_.empty(); }
    std::string name() const override { return "fifo"; }
};

class PriorityPolicy final : public SchedulingPolicy {
    ds::BinaryHeap<ReadyItem, HigherPriority> items_;
public:
    void push(ReadyItem item) override { items_.push(item); }
    ReadyItem pop() override { return items_.pop(); }
    bool empty() const noexcept override { return items_.empty(); }
    std::string name() const override { return "priority"; }
};

inline std::unique_ptr<SchedulingPolicy> makePolicy(const std::string& name) {
    if (name == "fifo") return std::make_unique<FifoPolicy>();
    if (name == "priority") return std::make_unique<PriorityPolicy>();
    throw std::invalid_argument("policy must be fifo or priority");
}

}
