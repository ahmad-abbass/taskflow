#include "taskflow/scheduler.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace taskflow {

void Scheduler::enqueue(Task& task) {
    task.readySequence_ = nextSequence_++;
    ++task.readyVersion_;
    ready_->push({&task, task.priority_, task.readySequence_, task.readyVersion_});
    task.state_ = TaskState::Ready;
}

void Scheduler::setPolicy(const std::string& name) {
    auto replacement = makePolicy(name);
    ds::BinaryHeap<ReadyItem, OlderFirst> ordered;
    for (const auto* task : tasks_.list()) {
        if (task->state_ == TaskState::Ready)
            ordered.push({tasks_.find(task->id_), task->priority_, task->readySequence_, task->readyVersion_});
    }
    while (!ordered.empty()) replacement->push(ordered.pop());
    ready_ = std::move(replacement);
}

const Task& Scheduler::add(std::string id, int priority, const std::string& kind,
                           const std::string& payload, std::string title) {
    if (id.empty() || id.size() > 64) throw std::invalid_argument("ID must contain 1 to 64 characters");
    for (const unsigned char ch : id)
        if (!std::isalnum(ch) && ch != '-' && ch != '_')
            throw std::invalid_argument("ID accepts letters, digits, dash and underscore");
    if (priority < -1000 || priority > 1000) throw std::invalid_argument("priority must be between -1000 and 1000");
    if (title.size() > 160) throw std::invalid_argument("title exceeds 160 bytes");
    if (tasks_.size() >= 100000) throw std::length_error("task limit reached");
    if (tasks_.find(id)) throw std::invalid_argument("task ID already exists");
    auto action = makeAction(kind, payload);
    if (title.empty()) title = id;
    Task& task = tasks_.insert(std::make_unique<Task>(std::move(id), std::move(title), priority, std::move(action)));
    enqueue(task);
    return task;
}

void Scheduler::cancel(const std::string& id) {
    Task* task = tasks_.find(id);
    if (!task) throw std::invalid_argument("task not found");
    if (task->state_ == TaskState::Cancelled) return;
    if (task->state_ != TaskState::Ready) throw std::logic_error("only pending tasks can be cancelled");
    task->state_ = TaskState::Cancelled;
}

std::optional<std::string> Scheduler::runNext() {
    while (!ready_->empty()) {
        const auto item = ready_->pop();
        Task* task = item.task;
        if (task->state_ != TaskState::Ready || item.version != task->readyVersion_) continue;
        task->state_ = TaskState::Running;
        ++task->attempts_;
        TaskResult result;
        try { result = task->action_->execute(task->attempts_); }
        catch (const std::exception& error) { result = {false, error.what()}; }
        task->result_ = std::move(result.output);
        task->state_ = result.success ? TaskState::Succeeded : TaskState::Failed;
        return task->id_;
    }
    return std::nullopt;
}

std::size_t Scheduler::runUntilIdle(std::size_t limit) {
    std::size_t count = 0;
    while (count < limit && runNext()) ++count;
    return count;
}

}
