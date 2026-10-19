#pragma once

#include "taskflow/task.hpp"
#include "taskflow/ds/hash_table.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace taskflow {

class TaskRegistry {
    ds::HashTable<std::unique_ptr<Task>> tasks_;
    std::vector<Task*> order_;
public:
    Task* find(const std::string& id) {
        const auto* stored = tasks_.find(id);
        return stored ? stored->get() : nullptr;
    }
    const Task* find(const std::string& id) const {
        const auto* stored = tasks_.find(id);
        return stored ? stored->get() : nullptr;
    }
    Task& insert(std::unique_ptr<Task> task) {
        if (find(task->id())) throw std::invalid_argument("task ID already exists");
        if (order_.size() == order_.capacity()) order_.reserve(order_.capacity() ? order_.capacity() * 2 : 8);
        const auto id = task->id();
        auto& stored = tasks_.insert(id, std::move(task));
        order_.push_back(stored.get());
        return *stored;
    }
    std::vector<const Task*> list() const {
        std::vector<const Task*> result;
        result.reserve(tasks_.size());
        for (const auto* task : order_) result.push_back(task);
        return result;
    }
    std::size_t size() const noexcept { return tasks_.size(); }
};

}
