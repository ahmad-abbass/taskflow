#pragma once

#include "taskflow/task.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace taskflow {

class TaskRegistry {
    std::vector<std::unique_ptr<Task>> tasks_;
public:
    Task* find(const std::string& id) {
        for (auto& task : tasks_) if (task->id() == id) return task.get();
        return nullptr;
    }
    const Task* find(const std::string& id) const {
        for (const auto& task : tasks_) if (task->id() == id) return task.get();
        return nullptr;
    }
    Task& insert(std::unique_ptr<Task> task) {
        if (find(task->id())) throw std::invalid_argument("task ID already exists");
        tasks_.push_back(std::move(task));
        return *tasks_.back();
    }
    std::vector<const Task*> list() const {
        std::vector<const Task*> result;
        result.reserve(tasks_.size());
        for (const auto& task : tasks_) result.push_back(task.get());
        return result;
    }
    std::size_t size() const noexcept { return tasks_.size(); }
};

}
