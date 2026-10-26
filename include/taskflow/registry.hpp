#pragma once

#include "taskflow/task.hpp"
#include "taskflow/ds/hash_table.hpp"
#include "taskflow/ds/avl_tree.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace taskflow {

class TaskRegistry {
    ds::HashTable<std::unique_ptr<Task>> tasks_;
    std::vector<Task*> order_;
    ds::AvlTree<const Task*> ordered_;
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
        ordered_.insert(id, stored.get());
        order_.push_back(stored.get());
        return *stored;
    }
    std::vector<const Task*> list(std::optional<TaskState> state = std::nullopt) const {
        std::vector<const Task*> result;
        result.reserve(tasks_.size());
        for (const auto* task : order_)
            if (!state || task->state() == *state) result.push_back(task);
        return result;
    }
    std::size_t size() const noexcept { return tasks_.size(); }
    std::vector<const Task*> ordered() const { return ordered_.values(); }
    std::vector<const Task*> range(const std::string& low, const std::string& high) const { return ordered_.range(low, high); }
    std::size_t rank(const std::string& id) const { return ordered_.rank(id); }
    const Task* select(std::size_t index) const { const auto* value = ordered_.select(index); return value ? *value : nullptr; }
};

}
