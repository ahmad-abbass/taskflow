#include "taskflow/scheduler.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace taskflow {

namespace {
bool pending(TaskState state) { return state == TaskState::Ready || state == TaskState::Waiting; }
bool unsuccessful(TaskState state) {
    return state == TaskState::Failed || state == TaskState::Cancelled || state == TaskState::Blocked;
}
}

bool Scheduler::reaches(Task& from, Task& target) const {
    ds::LinkedQueue<Task*> queue;
    ds::HashTable<bool> seen;
    queue.push(&from);
    seen.insert(from.id_, true);
    while (!queue.empty()) {
        const auto* task = queue.pop();
        if (task == &target) return true;
        for (auto* dependent : task->dependents_) {
            if (!seen.find(dependent->id_)) { seen.insert(dependent->id_, true); queue.push(dependent); }
        }
    }
    return false;
}

void Scheduler::blockDependents(Task& task) {
    ds::LinkedQueue<Task*> queue;
    queue.push(&task);
    while (!queue.empty()) {
        auto* source = queue.pop();
        for (auto* child : source->dependents_) {
            if (!pending(child->state_)) continue;
            child->state_ = TaskState::Blocked;
            child->result_ = "prerequisite " + source->id_ + " did not succeed";
            ++child->readyVersion_;
            queue.push(child);
        }
    }
}

void Scheduler::refresh(Task& task) {
    if (!pending(task.state_)) return;
    bool ready = true;
    for (const auto* prerequisite : task.prerequisites_) {
        if (unsuccessful(prerequisite->state_)) {
            task.state_ = TaskState::Blocked;
            task.result_ = "prerequisite " + prerequisite->id_ + " did not succeed";
            ++task.readyVersion_;
            blockDependents(task);
            return;
        }
        if (prerequisite->state_ != TaskState::Succeeded) ready = false;
    }
    if (ready && task.state_ != TaskState::Ready) enqueue(task);
    else if (!ready && task.state_ == TaskState::Ready) {
        task.state_ = TaskState::Waiting;
        ++task.readyVersion_;
    }
}

void Scheduler::addDependency(const std::string& id, const std::string& prerequisiteId) {
    auto* task = tasks_.find(id);
    auto* prerequisite = tasks_.find(prerequisiteId);
    if (!task || !prerequisite) throw std::invalid_argument("dependency task not found");
    if (!pending(task->state_)) throw std::logic_error("only pending tasks accept dependencies");
    for (const auto* existing : task->prerequisites_) if (existing == prerequisite) return;
    if (reaches(*task, *prerequisite)) throw std::invalid_argument("dependency would create a cycle");
    if (edgeCount_ >= 1000000) throw std::length_error("dependency limit reached");
    if (task->prerequisites_.size() == task->prerequisites_.capacity())
        task->prerequisites_.reserve(task->prerequisites_.capacity() ? task->prerequisites_.capacity() * 2 : 4);
    if (prerequisite->dependents_.size() == prerequisite->dependents_.capacity())
        prerequisite->dependents_.reserve(prerequisite->dependents_.capacity() ? prerequisite->dependents_.capacity() * 2 : 4);
    task->prerequisites_.push_back(prerequisite);
    prerequisite->dependents_.push_back(task);
    ++edgeCount_;
    refresh(*task);
}

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
    if (!pending(task->state_)) throw std::logic_error("only pending tasks can be cancelled");
    task->state_ = TaskState::Cancelled;
    blockDependents(*task);
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
        if (result.success) for (auto* dependent : task->dependents_) refresh(*dependent);
        else blockDependents(*task);
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
