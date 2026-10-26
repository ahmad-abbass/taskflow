#include "taskflow/scheduler.hpp"
#include "taskflow/history.hpp"

#include <cctype>
#include <iomanip>
#include <optional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string readToken(std::istringstream& input) {
    std::string value;
    if (!(input >> std::quoted(value))) throw std::invalid_argument("missing argument or closing quote");
    return value;
}

void requireEnd(std::istringstream& input) {
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("unexpected extra arguments");
}

std::optional<taskflow::TaskState> readStateFilter(std::istringstream& input) {
    input >> std::ws;
    if (input.eof()) return std::nullopt;
    auto name = readToken(input);
    for (auto& ch : name)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    using taskflow::TaskState;
    const TaskState states[] = {
        TaskState::Waiting, TaskState::Ready, TaskState::Running, TaskState::Succeeded,
        TaskState::Failed, TaskState::Cancelled, TaskState::Blocked, TaskState::RetryWait
    };
    for (const auto state : states)
        if (name == taskflow::stateName(state)) return state;
    throw std::invalid_argument("unknown task state; choose waiting, ready, running, succeeded, failed, cancelled, blocked or retry_wait");
}

std::size_t readBounded(std::istringstream& input, std::int64_t minimum, std::int64_t maximum) {
    const auto value = taskflow::parseInteger(readToken(input));
    if (value < minimum || value > maximum) throw std::invalid_argument("argument outside allowed range");
    return static_cast<std::size_t>(value);
}

void printTask(const taskflow::Task& task) {
    std::cout << std::quoted(task.id()) << ' ' << taskflow::stateName(task.state()) << ' '
              << task.priority() << ' ' << task.attempts() << ' ' << std::quoted(task.result()) << '\n';
}

}

int main() {
    taskflow::Scheduler scheduler;
    taskflow::UndoHistory history;
    std::string line;
    bool failed = false;
    while (std::getline(std::cin, line)) {
        if (line.size() > 12000) { std::cout << "ERROR command exceeds 12,000 bytes\n"; failed = true; continue; }
        std::istringstream input(line);
        std::string command;
        if (!(input >> command)) continue;
        try {
            const bool mutates = command == "add" || command == "configure" || command == "workers" ||
                command == "depend" || command == "policy" || command == "cancel" || command == "run" ||
                command == "next" || command == "step" || command == "load";
            const auto before = mutates ? scheduler.snapshot() : std::string{};
            if (command == "help") {
                requireEnd(input);
                std::cout << "add ID PRIORITY KIND \"PAYLOAD\" [\"TITLE\"]\n"
                             "KIND: sum | count | flaky\n"
                             "list [STATE]\n"
                             "STATE: waiting | ready | running | succeeded | failed | cancelled | blocked | retry_wait\n"
                             "ordered | range LOW HIGH | rank ID | select INDEX | save PATH | load PATH | undo | workers COUNT | configure ID DURATION RETRIES | step | metrics | depend ID PREREQUISITE | policy fifo|priority | next | run | show ID | cancel ID | quit\n";
            } else if (command == "add") {
                const auto id = readToken(input);
                const auto priority = taskflow::parseInteger(readToken(input));
                const auto kind = readToken(input);
                const auto payload = readToken(input);
                input >> std::ws;
                const auto title = input.eof() ? std::string{} : readToken(input);
                requireEnd(input);
                if (priority < -1000 || priority > 1000) throw std::invalid_argument("priority out of range");
                scheduler.add(id, static_cast<int>(priority), kind, payload, title);
                std::cout << "OK added " << std::quoted(id) << '\n';
            } else if (command == "ordered") {
                requireEnd(input);
                for (const auto* task : scheduler.ordered()) printTask(*task);
                std::cout << "OK ordered " << scheduler.size() << '\n';
            } else if (command == "range") {
                const auto low = readToken(input);
                const auto high = readToken(input);
                requireEnd(input);
                const auto tasks = scheduler.range(low, high);
                for (const auto* task : tasks) printTask(*task);
                std::cout << "OK range " << tasks.size() << '\n';
            } else if (command == "rank") {
                const auto id = readToken(input);
                requireEnd(input);
                std::cout << "RANK " << scheduler.rank(id) << '\n';
            } else if (command == "select") {
                const auto index = readBounded(input, 0, 100000);
                requireEnd(input);
                const auto* task = scheduler.select(index);
                if (task) printTask(*task);
                else std::cout << "EMPTY\n";
            } else if (command == "save" || command == "load") {
                const auto path = readToken(input);
                requireEnd(input);
                if (command == "save") scheduler.save(path);
                else scheduler.load(path);
                std::cout << "OK " << command << '\n';
            } else if (command == "undo") {
                requireEnd(input);
                history.undo(scheduler);
                std::cout << "OK undo " << history.size() << '\n';
            } else if (command == "workers") {
                const auto count = readBounded(input, 1, 64);
                requireEnd(input);
                scheduler.setWorkers(count);
                std::cout << "OK workers " << count << '\n';
            } else if (command == "configure") {
                const auto id = readToken(input);
                const auto duration = readBounded(input, 1, 10000);
                const auto retries = readBounded(input, 0, 10);
                requireEnd(input);
                scheduler.configure(id, duration, retries);
                std::cout << "OK configured\n";
            } else if (command == "step") {
                requireEnd(input);
                const auto report = scheduler.step();
                std::cout << "TICK " << report.tick << '\n';
                for (const auto& id : report.started) std::cout << "START " << std::quoted(id) << '\n';
                for (const auto& id : report.completed) printTask(*scheduler.find(id));
                if (!report.progressed) std::cout << "IDLE\n";
            } else if (command == "metrics") {
                requireEnd(input);
                const auto metrics = scheduler.metrics();
                std::cout << "METRICS submitted=" << metrics.submitted << " waiting=" << metrics.waiting
                          << " ready=" << metrics.ready << " running=" << metrics.running << " retrying=" << metrics.retrying
                          << " succeeded=" << metrics.succeeded << " failed=" << metrics.failed
                          << " cancelled=" << metrics.cancelled << " blocked=" << metrics.blocked
                          << " attempts=" << metrics.attempts << " ticks=" << metrics.ticks << '\n';
            } else if (command == "depend") {
                const auto id = readToken(input);
                const auto prerequisite = readToken(input);
                requireEnd(input);
                scheduler.addDependency(id, prerequisite);
                std::cout << "OK dependency\n";
            } else if (command == "policy") {
                const auto name = readToken(input);
                requireEnd(input);
                scheduler.setPolicy(name);
                std::cout << "OK policy " << name << '\n';
            } else if (command == "run") {
                requireEnd(input);
                const auto count = scheduler.runUntilIdle();
                std::cout << "OK run " << count << '\n';
            } else if (command == "next") {
                requireEnd(input);
                const auto id = scheduler.runNext();
                if (id) printTask(*scheduler.find(*id));
                else std::cout << "EMPTY\n";
            } else if (command == "list") {
                const auto state = readStateFilter(input);
                requireEnd(input);
                const auto tasks = scheduler.list(state);
                for (const auto* task : tasks) printTask(*task);
                std::cout << "OK listed " << tasks.size() << '\n';
            } else if (command == "show") {
                const auto id = readToken(input);
                requireEnd(input);
                const auto* task = scheduler.find(id);
                if (!task) throw std::invalid_argument("task not found");
                printTask(*task);
                std::cout << "ACTION " << task->action().kind() << ' ' << std::quoted(task->action().payload())
                          << " TITLE " << std::quoted(task->title()) << '\n';
                for (const auto& dependency : task->dependencies()) std::cout << "DEPENDS " << std::quoted(dependency) << '\n';
            } else if (command == "cancel") {
                const auto id = readToken(input);
                requireEnd(input);
                scheduler.cancel(id);
                std::cout << "OK cancelled " << std::quoted(id) << '\n';
            } else if (command == "quit") {
                requireEnd(input);
                break;
            } else throw std::invalid_argument("unknown command; enter help");
            if (mutates && scheduler.snapshot() != before) history.remember(before);
        } catch (const std::exception& error) {
            failed = true;
            std::cout << "ERROR " << error.what() << '\n';
        }
    }
    if (std::cin.bad()) return 2;
    return failed ? 1 : 0;
}
