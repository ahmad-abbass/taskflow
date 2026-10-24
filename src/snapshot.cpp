#include "taskflow/scheduler.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace taskflow {
namespace {
constexpr std::size_t maxSnapshotBytes = 16 * 1024 * 1024;

void expect(std::istream& input, const std::string& expected) {
    std::string word;
    if (!(input >> word) || word != expected) throw std::invalid_argument("invalid snapshot marker: " + expected);
}

std::string text(std::istream& input, std::size_t limit) {
    input >> std::ws;
    if (input.peek() != '"') throw std::invalid_argument("snapshot strings must be quoted");
    std::string value;
    if (!(input >> std::quoted(value)) || value.size() > limit) throw std::invalid_argument("invalid snapshot string");
    return value;
}

std::uint64_t number(std::istream& input, std::uint64_t limit) {
    std::string token;
    if (!(input >> token)) throw std::invalid_argument("missing snapshot number");
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value > limit)
        throw std::invalid_argument("invalid snapshot number");
    return value;
}

int priority(std::istream& input) {
    std::string token;
    if (!(input >> token)) throw std::invalid_argument("missing priority");
    const auto value = parseInteger(token);
    if (value < -1000 || value > 1000) throw std::invalid_argument("invalid priority");
    return static_cast<int>(value);
}

bool unsuccessful(TaskState state) {
    return state == TaskState::Failed || state == TaskState::Cancelled || state == TaskState::Blocked;
}
}

std::string Scheduler::snapshot() const {
    std::ostringstream output;
    output << "TASKFLOW 1\n" << clock_ << ' ' << slots_.size() << ' ' << std::quoted(policyName()) << ' '
           << nextSequence_ << ' ' << size() << ' ' << edgeCount_ << '\n';
    for (const auto* task : list()) {
        output << "TASK " << std::quoted(task->id_) << ' ' << std::quoted(task->title_) << ' '
               << task->priority_ << ' ' << std::quoted(task->action_->kind()) << ' '
               << std::quoted(task->action_->payload()) << ' ' << static_cast<int>(task->state_) << ' '
               << task->attempts_ << ' ' << task->duration_ << ' ' << task->maxRetries_ << ' '
               << task->retryAt_ << ' ' << task->readySequence_ << ' ' << std::quoted(task->result_) << ' '
               << task->prerequisites_.size();
        for (const auto* prerequisite : task->prerequisites_) output << ' ' << std::quoted(prerequisite->id_);
        output << '\n';
        if (output.tellp() > static_cast<std::streamoff>(maxSnapshotBytes)) throw std::length_error("snapshot exceeds 16 MiB");
    }
    std::size_t running = 0;
    for (const auto& slot : slots_) if (slot.task) ++running;
    output << "SLOTS " << running << '\n';
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].task) output << i << ' ' << std::quoted(slots_[i].task->id_) << ' ' << slots_[i].deadline << '\n';
    output << "END\n";
    auto result = output.str();
    if (result.size() > maxSnapshotBytes) throw std::length_error("snapshot exceeds 16 MiB");
    return result;
}

void Scheduler::restore(const std::string& data) {
    if (data.size() > maxSnapshotBytes) throw std::invalid_argument("snapshot exceeds 16 MiB");
    std::istringstream input(data);
    expect(input, "TASKFLOW");
    if (number(input, 1) != 1) throw std::invalid_argument("unsupported snapshot version");
    const auto clock = number(input, 1000000000000ULL);
    const auto workers = number(input, 64);
    if (!workers) throw std::invalid_argument("snapshot has no workers");
    const auto policy = text(input, 16);
    const auto sequence = number(input, 1000000000000000ULL);
    const auto count = number(input, 100000);
    const auto edges = number(input, 1000000);
    Scheduler candidate;
    candidate.setWorkers(workers);
    candidate.ready_ = makePolicy(policy);
    std::vector<std::vector<std::string>> dependencies;
    dependencies.reserve(count);
    ds::HashTable<bool> sequences;
    std::size_t foundEdges = 0;
    for (std::size_t i = 0; i < count; ++i) {
        expect(input, "TASK");
        const auto id = text(input, 64);
        const auto title = text(input, 160);
        const auto p = priority(input);
        const auto kind = text(input, 16);
        const auto payload = text(input, 10000);
        candidate.add(id, p, kind, payload, title);
        auto* task = candidate.tasks_.find(id);
        task->title_ = title;
        task->state_ = static_cast<TaskState>(number(input, 7));
        task->attempts_ = number(input, 11);
        task->duration_ = number(input, 10000);
        task->maxRetries_ = number(input, 10);
        task->retryAt_ = number(input, 1000000001024ULL);
        task->readySequence_ = number(input, 1000000000000000ULL);
        task->result_ = text(input, 10000);
        const auto degree = number(input, count);
        if (degree > 1000000 - foundEdges) throw std::invalid_argument("too many snapshot edges");
        foundEdges += degree;
        if (!task->duration_ || task->attempts_ > task->maxRetries_ + 1 || task->readySequence_ >= sequence)
            throw std::invalid_argument("inconsistent task configuration");
        sequences.insert(std::to_string(task->readySequence_), true);
        std::vector<std::string> prerequisites;
        prerequisites.reserve(degree);
        for (std::size_t j = 0; j < degree; ++j) prerequisites.push_back(text(input, 64));
        dependencies.push_back(std::move(prerequisites));
    }
    if (foundEdges != edges) throw std::invalid_argument("snapshot edge count mismatch");
    auto tasks = candidate.list();
    ds::HashTable<std::size_t> degrees;
    ds::LinkedQueue<Task*> roots;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto* task = candidate.tasks_.find(tasks[i]->id_);
        ds::HashTable<bool> seen;
        for (const auto& id : dependencies[i]) {
            auto* prerequisite = candidate.tasks_.find(id);
            if (!prerequisite || prerequisite == task || seen.find(id)) throw std::invalid_argument("invalid snapshot dependency");
            seen.insert(id, true);
            task->prerequisites_.push_back(prerequisite);
            prerequisite->dependents_.push_back(task);
        }
        degrees.insert(task->id_, task->prerequisites_.size());
        if (task->prerequisites_.empty()) roots.push(task);
    }
    std::size_t visited = 0;
    while (!roots.empty()) {
        auto* task = roots.pop(); ++visited;
        for (auto* child : task->dependents_) if (--*degrees.find(child->id_) == 0) roots.push(child);
    }
    if (visited != count) throw std::invalid_argument("snapshot dependency graph is cyclic");
    candidate.ready_ = makePolicy(policy);
    ds::BinaryHeap<ReadyItem, OlderFirst> ready;
    for (const auto* entry : tasks) {
        auto* task = candidate.tasks_.find(entry->id_);
        bool allSucceeded = true, badPrerequisite = false;
        for (const auto* prerequisite : task->prerequisites_) {
            allSucceeded = allSucceeded && prerequisite->state_ == TaskState::Succeeded;
            badPrerequisite = badPrerequisite || unsuccessful(prerequisite->state_);
        }
        const auto state = task->state_;
        if ((state == TaskState::Waiting && (allSucceeded || badPrerequisite || task->attempts_)) ||
            (state == TaskState::Blocked && (!badPrerequisite || task->attempts_)) ||
            ((state == TaskState::Ready || state == TaskState::Running || state == TaskState::RetryWait ||
              state == TaskState::Succeeded || state == TaskState::Failed) && !allSucceeded) ||
            ((state == TaskState::Running || state == TaskState::Succeeded || state == TaskState::Failed ||
              state == TaskState::RetryWait) && !task->attempts_) ||
            ((state == TaskState::Ready || state == TaskState::RetryWait || state == TaskState::Cancelled) && task->attempts_ > task->maxRetries_) ||
            (state == TaskState::Failed && task->attempts_ != task->maxRetries_ + 1) ||
            (state == TaskState::RetryWait ? (task->retryAt_ < clock || task->retryAt_ > clock + (std::uint64_t{1} << (task->attempts_ - 1))) : task->retryAt_ != 0))
            throw std::invalid_argument("snapshot task state contradicts its dependencies or attempts");
        task->readyVersion_ = 1;
        if (state == TaskState::Ready) ready.push({task, task->priority_, task->readySequence_, task->readyVersion_});
        if (state == TaskState::RetryWait) candidate.delayed_.push({task, task->retryAt_, task->readySequence_, task->readyVersion_});
    }
    while (!ready.empty()) candidate.ready_->push(ready.pop());
    expect(input, "SLOTS");
    const auto running = number(input, workers);
    ds::HashTable<bool> assigned;
    for (std::size_t i = 0; i < running; ++i) {
        const auto slot = number(input, workers - 1);
        const auto id = text(input, 64);
        const auto deadline = number(input, 1000000010000ULL);
        auto* task = candidate.tasks_.find(id);
        if (!task || task->state_ != TaskState::Running || candidate.slots_[slot].task || assigned.find(id) ||
            deadline <= clock || deadline > clock + task->duration_)
            throw std::invalid_argument("invalid snapshot worker assignment");
        assigned.insert(id, true);
        candidate.slots_[slot] = {task, deadline};
    }
    for (const auto* task : tasks)
        if (task->state_ == TaskState::Running && !assigned.find(task->id_)) throw std::invalid_argument("running task has no worker");
    expect(input, "END");
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("trailing snapshot data");
    candidate.clock_ = clock;
    candidate.nextSequence_ = sequence;
    candidate.edgeCount_ = edges;
    *this = std::move(candidate);
}

void Scheduler::save(const std::string& path) const {
    namespace fs = std::filesystem;
    const auto data = snapshot();
    const fs::path staging = path + ".staging";
    if (!fs::create_directory(staging)) throw std::runtime_error("snapshot staging directory already exists");
    const auto temporary = staging / "snapshot";
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) throw std::runtime_error("snapshot write failed");
        output.close();
        if (!output) throw std::runtime_error("snapshot close failed");
        fs::rename(temporary, fs::path(path));
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored); fs::remove(staging, ignored);
        throw;
    }
    std::error_code ignored;
    fs::remove(staging, ignored);
}

void Scheduler::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("could not open snapshot");
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(maxSnapshotBytes)) throw std::invalid_argument("invalid snapshot size");
    std::string data(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(data.data(), static_cast<std::streamsize>(data.size()))) throw std::runtime_error("snapshot read failed");
    restore(data);
}

}
