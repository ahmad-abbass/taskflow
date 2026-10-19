#pragma once

#include "taskflow/actions.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace taskflow {

enum class TaskState { Waiting, Ready, Running, Succeeded, Failed, Cancelled };

inline const char* stateName(TaskState state) {
    switch (state) {
        case TaskState::Waiting: return "WAITING";
        case TaskState::Ready: return "READY";
        case TaskState::Running: return "RUNNING";
        case TaskState::Succeeded: return "SUCCEEDED";
        case TaskState::Failed: return "FAILED";
        case TaskState::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

class Task {
    friend class Scheduler;
    std::string id_;
    std::string title_;
    int priority_;
    std::unique_ptr<TaskAction> action_;
    TaskState state_ = TaskState::Waiting;
    std::size_t attempts_ = 0;
    std::string result_;

public:
    Task(std::string id, std::string title, int priority, std::unique_ptr<TaskAction> action)
        : id_(std::move(id)), title_(std::move(title)), priority_(priority), action_(std::move(action)) {}
    const std::string& id() const noexcept { return id_; }
    const std::string& title() const noexcept { return title_; }
    int priority() const noexcept { return priority_; }
    const TaskAction& action() const noexcept { return *action_; }
    TaskState state() const noexcept { return state_; }
    std::size_t attempts() const noexcept { return attempts_; }
    const std::string& result() const noexcept { return result_; }
};

}
