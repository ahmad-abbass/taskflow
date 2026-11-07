#include "taskflow/scheduler.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace taskflow {

DependencyAnalysis Scheduler::analyze() const {
    DependencyAnalysis result;
    const auto tasks = tasks_.list();
    const auto count = tasks.size();
    ds::HashTable<std::size_t> indices;
    ds::LinkedQueue<std::size_t> ready;
    std::vector<std::size_t> degrees(count);
    std::vector<std::optional<std::size_t>> previous(count);
    result.tasks.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto* task = tasks[i];
        indices.insert(task->id_, i);
        degrees[i] = task->prerequisites_.size();
        if (!degrees[i]) ready.push(i);
        result.tasks.push_back({task->id_, task->state_, 0, 0, {}});
        for (const auto* prerequisite : task->prerequisites_) {
            if (prerequisite->state_ != TaskState::Succeeded)
                result.tasks.back().unfinishedPrerequisites.push_back({prerequisite->id_, prerequisite->state_});
        }
    }

    std::size_t visited = 0;
    while (!ready.empty()) {
        const auto index = ready.pop();
        ++visited;
        auto& timing = result.tasks[index];
        timing.earliestFinish = timing.earliestStart + tasks[index]->duration_;
        for (const auto* dependent : tasks[index]->dependents_) {
            const auto* found = indices.find(dependent->id_);
            if (!found || !degrees[*found]) throw std::logic_error("invalid dependency graph");
            const auto child = *found;
            auto& successor = result.tasks[child];
            if (timing.earliestFinish > successor.earliestStart ||
                (timing.earliestFinish == successor.earliestStart &&
                 (!previous[child] || index < *previous[child]))) {
                successor.earliestStart = timing.earliestFinish;
                previous[child] = index;
            }
            if (--degrees[child] == 0) ready.push(child);
        }
    }
    if (visited != count) throw std::logic_error("cannot analyze a cyclic dependency graph");

    std::optional<std::size_t> end;
    for (std::size_t i = 0; i < count; ++i) {
        if (result.tasks[i].earliestFinish > result.minimumTicks) {
            result.minimumTicks = result.tasks[i].earliestFinish;
            end = i;
        }
    }
    while (end) {
        result.criticalPath.push_back(tasks[*end]->id_);
        end = previous[*end];
    }
    std::reverse(result.criticalPath.begin(), result.criticalPath.end());
    return result;
}

}
