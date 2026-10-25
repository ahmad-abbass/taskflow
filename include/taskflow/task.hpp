#pragma once

#include "taskflow/actions.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace taskflow {

enum class TaskState { Waiting, Ready, Running, Succeeded, Failed, Cancelled, Blocked, RetryWait };

inline const char* stateName(TaskState state) {
    switch (state) {
        case TaskState::Waiting: return "WAITING";
        case TaskState::Ready: return "READY";
        case TaskState::Running: return "RUNNING";
        case TaskState::Succeeded: return "SUCCEEDED";
        case TaskState::Failed: return "FAILED";
        case TaskState::Cancelled: return "CANCELLED";
        case TaskState::Blocked: return "BLOCKED";
        case TaskState::RetryWait: return "RETRY_WAIT";
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
    std::uint64_t readySequence_ = 0;
    std::uint64_t readyVersion_ = 0;
    std::vector<Task*> prerequisites_;
    std::vector<Task*> dependents_;
    std::uint64_t duration_ = 1;
    std::size_t maxRetries_ = 0;
    std::uint64_t retryAt_ = 0;

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
    std::uint64_t duration() const noexcept { return duration_; }
    std::size_t maxRetries() const noexcept { return maxRetries_; }
    std::uint64_t retryAt() const noexcept { return retryAt_; }
    std::vector<std::string> dependencies() const {
        std::vector<std::string> result;
        for (const auto* task : prerequisites_) result.push_back(task->id_);
        return result;
    }
};

}
