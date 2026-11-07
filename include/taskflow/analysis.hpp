#pragma once

#include "taskflow/task.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace taskflow {

struct PrerequisiteStatus {
    std::string id;
    TaskState state;
};

struct TaskTiming {
    std::string id;
    TaskState state;
    std::uint64_t earliestStart = 0;
    std::uint64_t earliestFinish = 0;
    std::vector<PrerequisiteStatus> unfinishedPrerequisites;
};

struct DependencyAnalysis {
    std::uint64_t minimumTicks = 0;
    std::vector<std::string> criticalPath;
    std::vector<TaskTiming> tasks;
};

}
