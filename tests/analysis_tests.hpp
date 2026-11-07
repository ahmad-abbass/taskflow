#pragma once

#include "taskflow/scheduler.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace taskflow_tests {

inline const taskflow::TaskTiming& timing(const taskflow::DependencyAnalysis& analysis,
                                         const std::string& id) {
    for (const auto& task : analysis.tasks) if (task.id == id) return task;
    throw std::runtime_error("analysis task not found: " + id);
}

inline void addAnalysisTests(Suite& suite) {
    suite.add("analysis handles empty, single-task, and disconnected plans", [] {
        taskflow::Scheduler scheduler;
        const auto empty = scheduler.analyze();
        require(empty.minimumTicks == 0 && empty.tasks.empty() && empty.criticalPath.empty(),
                "empty analysis was not empty");
        scheduler.add("only", 0, "sum", "1"); scheduler.configure("only", 7, 2);
        const auto single = scheduler.analyze();
        require(single.minimumTicks == 7 && single.criticalPath == std::vector<std::string>{"only"},
                "single-task duration was incorrect");
        require(timing(single, "only").earliestStart == 0 && timing(single, "only").earliestFinish == 7,
                "single-task timing was incorrect");
        scheduler.add("other", 0, "sum", "1"); scheduler.configure("other", 4, 0);
        require(scheduler.analyze().minimumTicks == 7, "independent durations were added together");
        require(single.tasks.size() == 1, "previous analysis changed after adding a task");
    });
    suite.add("critical path follows task durations through a dependency chain", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("prepare", 0, "sum", "1"); scheduler.configure("prepare", 2, 0);
        scheduler.add("compile", 0, "sum", "1"); scheduler.configure("compile", 5, 0);
        scheduler.add("test", 0, "sum", "1"); scheduler.configure("test", 3, 0);
        scheduler.addDependency("compile", "prepare");
        scheduler.addDependency("test", "compile");
        const auto analysis = scheduler.analyze();
        require(analysis.minimumTicks == 10 &&
                analysis.criticalPath == std::vector<std::string>{"prepare", "compile", "test"},
                "chain critical path was incorrect");
        require(timing(analysis, "compile").earliestStart == 2 &&
                timing(analysis, "test").earliestStart == 7, "earliest starts ignored dependencies");
        require(timing(analysis, "prepare").unfinishedPrerequisites.empty(), "root had a blocker");
        const auto& unfinished = timing(analysis, "test").unfinishedPrerequisites;
        require(unfinished.size() == 1 && unfinished[0].id == "compile" &&
                unfinished[0].state == taskflow::TaskState::Waiting,
                "unfinished prerequisite did not retain its current state");
    });
    suite.add("diamond analysis waits for the longest incoming branch", [] {
        taskflow::Scheduler scheduler;
        for (const auto* id : {"root", "left", "right", "join"}) scheduler.add(id, 0, "sum", "1");
        scheduler.configure("root", 2, 0); scheduler.configure("left", 5, 0);
        scheduler.configure("right", 3, 0); scheduler.configure("join", 4, 0);
        scheduler.addDependency("left", "root"); scheduler.addDependency("right", "root");
        scheduler.addDependency("join", "right"); scheduler.addDependency("join", "left");
        const auto analysis = scheduler.analyze();
        require(analysis.minimumTicks == 11 && timing(analysis, "join").earliestStart == 7,
                "diamond added parallel durations or released the join early");
        require(analysis.criticalPath == std::vector<std::string>{"root", "left", "join"},
                "diamond critical path chose the shorter branch");
    });
    suite.add("critical path ties use insertion order independently of priority and edge order", [] {
        taskflow::Scheduler scheduler;
        for (const auto* id : {"finish", "zeta", "alpha"}) scheduler.add(id, 0, "sum", "1");
        scheduler.configure("zeta", 2, 0); scheduler.configure("alpha", 2, 0);
        scheduler.addDependency("finish", "alpha"); scheduler.addDependency("finish", "zeta");
        scheduler.setPolicy("priority"); scheduler.setPriority("alpha", 1000);
        require(scheduler.analyze().criticalPath == std::vector<std::string>{"zeta", "finish"},
                "equal paths changed with priority or prerequisite insertion order");
        taskflow::Scheduler restored; restored.restore(scheduler.snapshot());
        restored.setPolicy("fifo");
        require(restored.analyze().criticalPath == scheduler.analyze().criticalPath,
                "snapshot or policy switch changed a tied critical path");
    });
    suite.add("analysis leaves running workers and retry deadlines unchanged", [] {
        taskflow::Scheduler scheduler;
        scheduler.setWorkers(2);
        scheduler.add("root", 0, "sum", "1"); scheduler.configure("root", 4, 0);
        scheduler.add("retry", 0, "flaky", "1 recovered"); scheduler.configure("retry", 1, 1);
        scheduler.add("child", 0, "sum", "1"); scheduler.addDependency("child", "root");
        scheduler.step();
        const auto before = scheduler.snapshot();
        const auto& view = scheduler;
        const auto analysis = view.analyze();
        require(scheduler.snapshot() == before, "analysis mutated live scheduling state");
        require(timing(analysis, "root").state == taskflow::TaskState::Running &&
                timing(analysis, "retry").state == taskflow::TaskState::RetryWait,
                "analysis lost live task states");
        require(analysis.minimumTicks == 5, "full-plan analysis used remaining time or retries");
        require(timing(analysis, "child").unfinishedPrerequisites[0].state == taskflow::TaskState::Running,
                "running prerequisite was not reported");
        taskflow::Scheduler replay; replay.restore(before);
        scheduler.runUntilIdle(); replay.runUntilIdle();
        require(scheduler.snapshot() == replay.snapshot(), "analysis changed subsequent execution");
        require(timing(analysis, "root").state == taskflow::TaskState::Running,
                "analysis retained references to mutable task states");
        const auto completed = scheduler.analyze();
        require(timing(completed, "child").unfinishedPrerequisites.empty() && completed.minimumTicks == 5,
                "completed tasks changed the full-plan estimate or retained finished prerequisites");
    });
    suite.add("failed and cancelled prerequisites remain visible in nominal analysis", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("failure", 0, "flaky", "1");
        scheduler.add("blocked", 0, "sum", "1"); scheduler.addDependency("blocked", "failure");
        scheduler.runUntilIdle();
        scheduler.add("cancelled", 0, "sum", "1"); scheduler.cancel("cancelled");
        scheduler.add("also-blocked", 0, "sum", "1"); scheduler.addDependency("also-blocked", "cancelled");
        const auto before = scheduler.snapshot();
        const auto analysis = scheduler.analyze();
        const auto& failed = timing(analysis, "blocked").unfinishedPrerequisites;
        const auto& cancelled = timing(analysis, "also-blocked").unfinishedPrerequisites;
        require(failed.size() == 1 && failed[0].id == "failure" && failed[0].state == taskflow::TaskState::Failed,
                "terminal failure was hidden from dependency analysis");
        require(cancelled.size() == 1 && cancelled[0].state == taskflow::TaskState::Cancelled,
                "cancelled prerequisite was hidden from dependency analysis");
        require(analysis.minimumTicks == 2 && scheduler.snapshot() == before,
                "analysis resumed failed tasks or changed its nominal duration model");
    });
    suite.add("random DAG analysis agrees with an independent duration model", [] {
        constexpr std::size_t count = 70;
        for (unsigned seed = 0; seed < 12; ++seed) {
            std::mt19937 random(seed + 400);
            taskflow::Scheduler scheduler;
            std::vector<std::size_t> insertion(count);
            std::vector<std::uint64_t> durations(count), starts(count), finishes(count);
            std::vector<std::vector<std::size_t>> prerequisites(count);
            for (std::size_t i = 0; i < count; ++i) { insertion[i] = i; durations[i] = 1 + random() % 20; }
            std::shuffle(insertion.begin(), insertion.end(), random);
            for (const auto i : insertion) {
                const auto id = "node-" + std::to_string(i);
                scheduler.add(id, 0, "sum", "1"); scheduler.configure(id, durations[i], 0);
            }
            for (std::size_t i = 0; i < count; ++i) {
                for (unsigned edge = 0; i && edge < 4; ++edge) {
                    const std::size_t parent = random() % i;
                    if (std::find(prerequisites[i].begin(), prerequisites[i].end(), parent) != prerequisites[i].end()) continue;
                    prerequisites[i].push_back(parent);
                    scheduler.addDependency("node-" + std::to_string(i), "node-" + std::to_string(parent));
                }
                for (const auto parent : prerequisites[i]) starts[i] = std::max(starts[i], finishes[parent]);
                finishes[i] = starts[i] + durations[i];
            }
            const auto before = scheduler.snapshot();
            const auto analysis = scheduler.analyze();
            require(analysis.minimumTicks == *std::max_element(finishes.begin(), finishes.end()),
                    "random DAG duration differs from the reference");
            for (std::size_t i = 0; i < count; ++i) {
                const auto id = "node-" + std::to_string(i);
                const auto& task = timing(analysis, id);
                require(task.earliestStart == starts[i] && task.earliestFinish == finishes[i] &&
                        task.unfinishedPrerequisites.size() == prerequisites[i].size(),
                        "random DAG task timing differs from the reference");
                require(analysis.tasks[i].id == "node-" + std::to_string(insertion[i]), "report lost insertion order");
            }
            std::uint64_t pathDuration = 0;
            for (std::size_t i = 0; i < analysis.criticalPath.size(); ++i) {
                const auto node = std::stoul(analysis.criticalPath[i].substr(5));
                pathDuration += durations[node];
                if (!i) require(prerequisites[node].empty(), "critical path did not begin at a root");
                else {
                    const auto parent = std::stoul(analysis.criticalPath[i - 1].substr(5));
                    require(std::find(prerequisites[node].begin(), prerequisites[node].end(), parent) != prerequisites[node].end(),
                            "critical path contained a nonexistent dependency");
                }
            }
            require(pathDuration == analysis.minimumTicks && scheduler.snapshot() == before,
                    "critical path duration or read-only behavior was incorrect");
        }
    });
    suite.add("deep dependency analysis uses iterative traversal", [] {
        constexpr std::size_t count = 12000;
        taskflow::Scheduler scheduler;
        for (std::size_t i = 0; i < count; ++i) {
            const auto id = "deep-" + std::to_string(i);
            scheduler.add(id, 0, "sum", "1");
            if (i) scheduler.addDependency(id, "deep-" + std::to_string(i - 1));
        }
        const auto analysis = scheduler.analyze();
        require(analysis.minimumTicks == count && analysis.criticalPath.size() == count &&
                analysis.criticalPath.front() == "deep-0" &&
                analysis.criticalPath.back() == "deep-11999", "deep chain analysis was truncated");
    });
}

}
