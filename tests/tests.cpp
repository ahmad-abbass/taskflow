#include "taskflow/scheduler.hpp"
#include "test_support.hpp"

#include <limits>
#include <memory>
#include <random>
#include <unordered_map>

int main() {
    Suite suite;
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
