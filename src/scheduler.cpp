#include "taskflow/scheduler.hpp"

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
    if (!pending(task->state_) || task->attempts_) throw std::logic_error("only unstarted tasks accept dependencies");
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
    task.retryAt_ = 0;
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
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
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
    if (!pending(task->state_) && task->state_ != TaskState::RetryWait) throw std::logic_error("only pending tasks can be cancelled");
    task->state_ = TaskState::Cancelled;
    blockDependents(*task);
}

Task* Scheduler::takeReady() {
    while (!ready_->empty()) {
        const auto item = ready_->pop();
        if (item.task->state_ == TaskState::Ready && item.version == item.task->readyVersion_) return item.task;
    }
    return nullptr;
}

void Scheduler::configure(const std::string& id, std::uint64_t duration, std::size_t retries) {
    auto* task = tasks_.find(id);
    if (!task) throw std::invalid_argument("task not found");
    if (!pending(task->state_) || task->attempts_) throw std::logic_error("configure requires an unstarted task");
    if (!duration || duration > 10000 || retries > 10) throw std::invalid_argument("duration must be 1..10000 and retries 0..10");
    task->duration_ = duration;
    task->maxRetries_ = retries;
}

void Scheduler::setWorkers(std::size_t count) {
    if (!count || count > 64) throw std::invalid_argument("worker count must be 1..64");
    for (const auto& slot : slots_) if (slot.task) throw std::logic_error("cannot resize running workers");
    slots_.assign(count, Slot{});
}

void Scheduler::finish(Task& task) {
    TaskResult result;
    try { result = task.action_->execute(task.attempts_); }
    catch (const std::exception& error) { result = {false, error.what()}; }
    task.result_ = std::move(result.output);
    if (!result.success && task.attempts_ <= task.maxRetries_) {
        task.state_ = TaskState::RetryWait;
        task.retryAt_ = clock_ + (std::uint64_t{1} << (task.attempts_ - 1));
        ++task.readyVersion_;
        task.readySequence_ = nextSequence_++;
        delayed_.push({&task, task.retryAt_, task.readySequence_, task.readyVersion_});
        return;
    }
    task.retryAt_ = 0;
    task.state_ = result.success ? TaskState::Succeeded : TaskState::Failed;
    completed_.push(task.id_);
    if (result.success) for (auto* dependent : task.dependents_) refresh(*dependent);
    else blockDependents(task);
}

TickReport Scheduler::advance() {
    TickReport report{clock_, false, {}, {}};
    while (!delayed_.empty() && delayed_.top().due <= clock_) {
        const auto item = delayed_.pop();
        if (item.task->state_ == TaskState::RetryWait && item.task->readyVersion_ == item.version) enqueue(*item.task);
    }
    bool active = false;
    for (auto& slot : slots_) {
        if (!slot.task) {
            slot.task = takeReady();
            if (slot.task) {
                slot.task->state_ = TaskState::Running;
                ++slot.task->attempts_;
                slot.deadline = clock_ + slot.task->duration_;
                report.started.push_back(slot.task->id_);
            }
        }
        active = active || slot.task;
    }
    while (!delayed_.empty() && delayed_.top().task->state_ != TaskState::RetryWait) delayed_.pop();
    if (!active && delayed_.empty()) return report;
    if (clock_ >= 1000000000000ULL) throw std::length_error("simulation clock limit reached");
    ++clock_;
    report.tick = clock_;
    report.progressed = true;
    for (auto& slot : slots_) {
        if (slot.task && slot.deadline <= clock_) {
            auto* task = slot.task;
            slot.task = nullptr;
            finish(*task);
        }
    }
    return report;
}

TickReport Scheduler::step() {
    auto report = advance();
    while (!completed_.empty()) report.completed.push_back(completed_.pop());
    return report;
}

std::optional<std::string> Scheduler::runNext(std::size_t tickBudget) {
    if (!completed_.empty()) return completed_.pop();
    for (std::size_t tick = 0; tick < tickBudget; ++tick) {
        if (!advance().progressed) return std::nullopt;
        if (!completed_.empty()) return completed_.pop();
    }
    throw std::length_error("next exhausted its tick budget; use step or run to continue");
}

std::size_t Scheduler::runUntilIdle(std::size_t limit, std::size_t tickBudget) {
    std::size_t count = 0;
    std::size_t ticks = 0;
    while (count < limit) {
        if (!completed_.empty()) { completed_.pop(); ++count; continue; }
        if (ticks == tickBudget || !advance().progressed) break;
        ++ticks;
    }
    return count;
}

Metrics Scheduler::metrics() const {
    Metrics result;
    result.submitted = size(); result.ticks = clock_;
    for (const auto* task : list()) {
        result.attempts += task->attempts_;
        switch (task->state_) {
            case TaskState::Waiting: ++result.waiting; break;
            case TaskState::Ready: ++result.ready; break;
            case TaskState::Running: ++result.running; break;
            case TaskState::RetryWait: ++result.retrying; break;
            case TaskState::Succeeded: ++result.succeeded; break;
            case TaskState::Failed: ++result.failed; break;
            case TaskState::Cancelled: ++result.cancelled; break;
            case TaskState::Blocked: ++result.blocked; break;
        }
    }
    return result;
}

}
