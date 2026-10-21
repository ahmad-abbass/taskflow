#include "taskflow/scheduler.hpp"
#include "test_support.hpp"

#include <limits>
#include <memory>
#include <random>
#include <unordered_map>
#include <queue>

int main() {
    Suite suite;
    suite.add("diamond dependencies run only after every prerequisite succeeds", [] {
        taskflow::Scheduler scheduler;
        for (const auto* id : {"finish", "left", "right", "root"}) scheduler.add(id, 1, "sum", "1");
        scheduler.addDependency("finish", "left");
        scheduler.addDependency("finish", "right");
        scheduler.addDependency("left", "root");
        scheduler.addDependency("right", "root");
        scheduler.setPolicy("priority");
        require(scheduler.runNext() == "root", "waiting task was dispatched");
        require(scheduler.runNext() == "left", "wrong released order");
        require(scheduler.find("finish")->state() == taskflow::TaskState::Waiting, "join released too early");
        require(scheduler.runNext() == "right" && scheduler.runNext() == "finish", "DAG did not drain");
        require(!scheduler.runNext(), "stale ready entries executed twice");
    });
    suite.add("cycle rejection is atomic and duplicate edges are idempotent", [] {
        taskflow::Scheduler scheduler;
        for (const auto* id : {"a", "b", "c"}) scheduler.add(id, 1, "sum", "1");
        scheduler.addDependency("b", "a"); scheduler.addDependency("c", "b");
        scheduler.addDependency("b", "a");
        throws<std::invalid_argument>([&] { scheduler.addDependency("a", "c"); });
        throws<std::invalid_argument>([&] { scheduler.addDependency("a", "a"); });
        require(scheduler.find("a")->dependencies().empty() && scheduler.find("b")->dependencies().size() == 1, "invalid edge changed graph");
        require(scheduler.runUntilIdle() == 3, "graph remained cyclic");
    });
    suite.add("failure and cancellation block descendants without running them", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("root", 0, "sum", "9223372036854775807 1");
        for (const auto* id : {"child", "leaf", "other"}) scheduler.add(id, 0, "sum", "1");
        scheduler.addDependency("child", "root"); scheduler.addDependency("leaf", "child");
        require(scheduler.runUntilIdle() == 2, "blocked task ran");
        require(scheduler.find("leaf")->state() == taskflow::TaskState::Blocked && scheduler.find("leaf")->attempts() == 0, "failure did not propagate");
        taskflow::Scheduler cancelled;
        for (const auto* id : {"a", "b", "c"}) cancelled.add(id, 0, "sum", "1");
        cancelled.addDependency("b", "a"); cancelled.cancel("a"); cancelled.addDependency("c", "a");
        require(cancelled.runUntilIdle() == 0 && cancelled.find("c")->state() == taskflow::TaskState::Blocked, "cancelled prerequisite released work");
    });
    suite.add("binary heap agrees with a reference under mixed operations", [] {
        taskflow::ds::BinaryHeap<int, std::less<int>> heap;
        std::priority_queue<int, std::vector<int>, std::greater<int>> reference;
        std::mt19937 random(42);
        for (int i = 0; i < 30000; ++i) {
            if (reference.empty() || random() % 3) {
                const int value = static_cast<int>(random() % 1000);
                heap.push(value); reference.push(value);
            } else {
                require(heap.top() == reference.top() && heap.pop() == reference.top(), "heap mismatch");
                reference.pop();
            }
        }
        while (!reference.empty()) { require(heap.pop() == reference.top(), "drain mismatch"); reference.pop(); }
        throws<std::out_of_range>([&] { heap.pop(); });
    });
    suite.add("priority scheduling is stable and policies can change safely", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("low", -1, "sum", "1");
        scheduler.add("high-first", 9, "sum", "2");
        scheduler.add("high-second", 9, "sum", "3");
        scheduler.add("cancelled", 100, "sum", "4");
        scheduler.cancel("cancelled");
        scheduler.setPolicy("priority");
        require(scheduler.runNext() == "high-first", "wrong priority or tie break");
        scheduler.setPolicy("fifo");
        require(scheduler.runNext() == "low", "FIFO rebuild lost original ready order");
        scheduler.setPolicy("priority");
        throws<std::invalid_argument>([&] { scheduler.setPolicy("unknown"); });
        require(scheduler.runNext() == "high-second" && !scheduler.runNext(), "switch lost a task");
    });
    suite.add("hash table resolves collisions and retains references through growth", [] {
        struct Collision { std::size_t operator()(const std::string&) const { return 0; } };
        taskflow::ds::HashTable<std::unique_ptr<int>, Collision> table;
        auto* address = &table.insert("first", std::make_unique<int>(7));
        for (int i = 0; i < 3000; ++i) table.insert(std::to_string(i), std::make_unique<int>(i));
        require(table.find("first") == address && **address == 7, "rehash invalidated value");
        for (int i = 0; i < 3000; ++i) require(**table.find(std::to_string(i)) == i, "collision lost key");
        throws<std::invalid_argument>([&] { table.insert("first", std::make_unique<int>(9)); });
        auto moved = std::move(table);
        table.insert("reuse", std::make_unique<int>(1));
        require(moved.size() == 3001 && table.size() == 1, "move lost table ownership");
        moved.clear();
        require(!moved.find("first"), "clear retained key");
    });
    suite.add("hash lookup agrees with a standard reference map", [] {
        taskflow::ds::HashTable<int> actual;
        std::unordered_map<std::string, int> expected;
        std::mt19937 random(17);
        for (int i = 0; i < 10000; ++i) {
            const auto key = std::to_string(random() % 2000);
            if (expected.emplace(key, i).second) actual.insert(key, i);
            require(actual.find(key) && *actual.find(key) == expected.at(key), "lookup mismatch");
        }
        require(actual.size() == expected.size() && !actual.find("missing"), "wrong map size");
    });
    suite.add("queue preserves FIFO and can be reused after draining", [] {
        taskflow::ds::LinkedQueue<int> queue;
        for (int i = 0; i < 1000; ++i) queue.push(i);
        for (int i = 0; i < 1000; ++i) require(queue.pop() == i, "FIFO order changed");
        require(queue.empty(), "queue did not drain");
        queue.push(42);
        require(queue.front() == 42 && queue.pop() == 42, "queue could not be reused");
        throws<std::out_of_range>([&] { queue.pop(); });
    });
    suite.add("queue transfers move-only values and leaves moved-from queues reusable", [] {
        taskflow::ds::LinkedQueue<std::unique_ptr<int>> first;
        first.push(std::make_unique<int>(8));
        auto second = std::move(first);
        first.push(std::make_unique<int>(3));
        require(*second.pop() == 8 && *first.pop() == 3, "move corrupted ownership");
        second = std::move(first);
        require(second.empty(), "move assignment retained old elements");
    });
    suite.add("long linked queue clears iteratively", [] {
        taskflow::ds::LinkedQueue<int> queue;
        for (int i = 0; i < 100000; ++i) queue.push(i);
        queue.clear();
        require(queue.size() == 0, "clear retained nodes");
    });
    suite.add("actions use polymorphism and validate integer payloads", [] {
        auto sum = taskflow::makeAction("sum", "-4 10 2");
        auto count = taskflow::makeAction("count", "  learn\tbuild  test\n");
        require(sum->execute(1).output == "8", "wrong sum");
        require(count->execute(1).output == "3", "wrong word count");
        throws<std::invalid_argument>([] { taskflow::makeAction("sum", "2 9999999999999999999999999"); });
        throws<std::invalid_argument>([] { taskflow::makeAction("sum", "2x"); });
        throws<std::invalid_argument>([] { taskflow::makeAction("unknown", "x"); });
    });
    suite.add("overflow fails a task without undefined signed arithmetic", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("overflow", 1, "sum", "9223372036854775807 1");
        scheduler.add("minimum", 1, "sum", "-9223372036854775808 -1");
        scheduler.runUntilIdle();
        require(scheduler.find("overflow")->state() == taskflow::TaskState::Failed, "positive overflow succeeded");
        require(scheduler.find("minimum")->state() == taskflow::TaskState::Failed, "negative overflow succeeded");
    });
    suite.add("FIFO scheduler dispatches in submission order", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("first", 1, "sum", "1 2");
        scheduler.add("second", 100, "count", "two words");
        require(scheduler.runNext() == "first", "FIFO unexpectedly used priority");
        require(scheduler.runNext() == "second", "second task missing");
        require(!scheduler.runNext(), "empty scheduler returned a task");
        require(scheduler.find("first")->result() == "3", "wrong result");
    });
    suite.add("cancellation is idempotent and cancelled jobs do not run", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("cancelled", 1, "count", "hello");
        scheduler.add("active", 1, "sum", "9");
        scheduler.cancel("cancelled");
        scheduler.cancel("cancelled");
        require(scheduler.runUntilIdle() == 1, "cancelled task ran");
        require(scheduler.find("cancelled")->attempts() == 0, "cancelled task was executed");
        throws<std::logic_error>([&] { scheduler.cancel("active"); });
    });
    suite.add("invalid submissions preserve the registry", [] {
        taskflow::Scheduler scheduler;
        scheduler.add("one", 0, "sum", "1");
        throws<std::invalid_argument>([&] { scheduler.add("one", 0, "count", "duplicate"); });
        throws<std::invalid_argument>([&] { scheduler.add("bad id", 0, "sum", "1"); });
        throws<std::invalid_argument>([&] { scheduler.add("two", 1001, "sum", "1"); });
        throws<std::invalid_argument>([&] { scheduler.add("two", 0, "sum", ""); });
        require(scheduler.size() == 1 && scheduler.find("one"), "invalid add changed registry");
    });
    return suite.run();
}
