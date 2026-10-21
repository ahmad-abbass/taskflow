#include "taskflow/scheduler.hpp"

#include <iomanip>
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

void printTask(const taskflow::Task& task) {
    std::cout << std::quoted(task.id()) << ' ' << taskflow::stateName(task.state()) << ' '
              << task.priority() << ' ' << task.attempts() << ' ' << std::quoted(task.result()) << '\n';
}

}

int main() {
    taskflow::Scheduler scheduler;
    std::string line;
    bool failed = false;
    while (std::getline(std::cin, line)) {
        if (line.size() > 12000) { std::cout << "ERROR command exceeds 12,000 bytes\n"; failed = true; continue; }
        std::istringstream input(line);
        std::string command;
        if (!(input >> command)) continue;
        try {
            if (command == "help") {
                requireEnd(input);
                std::cout << "add ID PRIORITY KIND \"PAYLOAD\" [\"TITLE\"]\npolicy fifo|priority | next | run | list | show ID | cancel ID | quit\n";
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
            } else if (command == "policy") {
                const auto name = readToken(input);
                requireEnd(input);
                scheduler.setPolicy(name);
                std::cout << "OK policy " << name << '\n';
            } else if (command == "run") {
                requireEnd(input);
                std::cout << "OK run " << scheduler.runUntilIdle() << '\n';
            } else if (command == "next") {
                requireEnd(input);
                const auto id = scheduler.runNext();
                if (id) printTask(*scheduler.find(*id));
                else std::cout << "EMPTY\n";
            } else if (command == "list") {
                requireEnd(input);
                for (const auto* task : scheduler.list()) printTask(*task);
                std::cout << "OK listed " << scheduler.size() << '\n';
            } else if (command == "show") {
                const auto id = readToken(input);
                requireEnd(input);
                const auto* task = scheduler.find(id);
                if (!task) throw std::invalid_argument("task not found");
                printTask(*task);
                std::cout << "ACTION " << task->action().kind() << ' ' << std::quoted(task->action().payload())
                          << " TITLE " << std::quoted(task->title()) << '\n';
            } else if (command == "cancel") {
                const auto id = readToken(input);
                requireEnd(input);
                scheduler.cancel(id);
                std::cout << "OK cancelled " << std::quoted(id) << '\n';
            } else if (command == "quit") {
                requireEnd(input);
                break;
            } else throw std::invalid_argument("unknown command; enter help");
        } catch (const std::exception& error) {
            failed = true;
            std::cout << "ERROR " << error.what() << '\n';
        }
    }
    if (std::cin.bad()) return 2;
    return failed ? 1 : 0;
}
