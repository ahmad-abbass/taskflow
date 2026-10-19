#pragma once

#include "taskflow/ds/linked_queue.hpp"
#include "taskflow/registry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace taskflow {

class Scheduler {
    TaskRegistry tasks_;
    ds::LinkedQueue<Task*> ready_;
public:
    const Task& add(std::string id, int priority, const std::string& kind,
                    const std::string& payload, std::string title = "");
    void cancel(const std::string& id);
    std::optional<std::string> runNext();
    std::size_t runUntilIdle(std::size_t limit = 100000);
    const Task* find(const std::string& id) const { return tasks_.find(id); }
    std::vector<const Task*> list() const { return tasks_.list(); }
    std::size_t size() const noexcept { return tasks_.size(); }
};

}
