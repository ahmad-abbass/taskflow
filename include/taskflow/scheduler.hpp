#pragma once

#include "taskflow/ds/linked_queue.hpp"
#include "taskflow/registry.hpp"
#include "taskflow/policy.hpp"

#include <optional>
#include <string>
#include <vector>

namespace taskflow {

struct TickReport {
    std::uint64_t tick;
    bool progressed;
    std::vector<std::string> started;
    std::vector<std::string> completed;
};

struct Metrics {
    std::size_t submitted = 0, waiting = 0, ready = 0, running = 0, retrying = 0;
    std::size_t succeeded = 0, failed = 0, cancelled = 0, blocked = 0, attempts = 0;
    std::uint64_t ticks = 0;
};

class Scheduler {
    struct Slot { Task* task = nullptr; std::uint64_t deadline = 0; };
    struct Delay { Task* task; std::uint64_t due, sequence, version; };
    struct Earlier {
        bool operator()(const Delay& a, const Delay& b) const noexcept {
            return a.due != b.due ? a.due < b.due : a.sequence < b.sequence;
        }
    };
    TaskRegistry tasks_;
    std::unique_ptr<SchedulingPolicy> ready_ = makePolicy("fifo");
    std::uint64_t nextSequence_ = 0;
    std::size_t edgeCount_ = 0;
    std::vector<Slot> slots_{1};
    ds::BinaryHeap<Delay, Earlier> delayed_;
    ds::LinkedQueue<std::string> completed_;
    std::uint64_t clock_ = 0;
    void enqueue(Task& task);
    void refresh(Task& task);
    void blockDependents(Task& task);
    bool reaches(Task& from, Task& target) const;
    Task* takeReady();
    TickReport advance();
    void finish(Task& task);
public:
    std::string snapshot() const;
    void restore(const std::string& data);
    void save(const std::string& path) const;
    void load(const std::string& path);
    void configure(const std::string& id, std::uint64_t duration, std::size_t retries);
    void setWorkers(std::size_t count);
    std::size_t workerCount() const noexcept { return slots_.size(); }
    std::uint64_t clock() const noexcept { return clock_; }
    TickReport step();
    Metrics metrics() const;
    void addDependency(const std::string& id, const std::string& prerequisite);
    void setPolicy(const std::string& name);
    std::string policyName() const { return ready_->name(); }
    const Task& add(std::string id, int priority, const std::string& kind,
                    const std::string& payload, std::string title = "");
    void cancel(const std::string& id);
    std::optional<std::string> runNext(std::size_t tickBudget = 1000000);
    std::size_t runUntilIdle(std::size_t limit = 100000, std::size_t tickBudget = 1000000);
    const Task* find(const std::string& id) const { return tasks_.find(id); }
    std::vector<const Task*> list() const { return tasks_.list(); }
    std::size_t size() const noexcept { return tasks_.size(); }
};

}
