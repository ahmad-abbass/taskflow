#include "taskflow/scheduler.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
template<class Function>
void measure(const std::string& name, std::size_t items, std::size_t operations, Function function) {
    const auto start = std::chrono::steady_clock::now();
    const auto checksum = function();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << name << ',' << items << ',' << operations << ',' << std::fixed << std::setprecision(3) << elapsed << ',' << checksum << '\n';
}
}

int main() {
    constexpr std::size_t count = 20000;
    taskflow::Scheduler scheduler;
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < count; ++i) keys.push_back("task-" + std::to_string(i));
    std::cout << "operation,items,operations,milliseconds,checksum\n";
    measure("submit_with_hash_and_avl", count, count, [&] {
        for (const auto& key : keys) scheduler.add(key, 0, "sum", "2 3");
        return scheduler.size();
    });
    std::mt19937 random(17);
    std::vector<std::string> queries;
    for (std::size_t i = 0; i < 5000; ++i) queries.push_back(keys[random() % count]);
    const auto insertionOrder = scheduler.list();
    measure("hash_lookup", count, queries.size(), [&] {
        std::size_t checksum = 0;
        for (const auto& key : queries) checksum += scheduler.find(key)->id().size();
        return checksum;
    });
    measure("linear_lookup_reference", count, queries.size(), [&] {
        std::size_t checksum = 0;
        for (const auto& key : queries) for (const auto* task : insertionOrder)
            if (task->id() == key) { checksum += task->id().size(); break; }
        return checksum;
    });
    measure("avl_rank_select", count, queries.size(), [&] {
        std::size_t checksum = 0;
        for (const auto& key : queries) checksum += scheduler.select(scheduler.rank(key))->id().size();
        return checksum;
    });
    scheduler.setWorkers(4); scheduler.setPolicy("priority");
    measure("four_slot_simulation", count, count, [&] { return scheduler.runUntilIdle(); });
    return scheduler.metrics().succeeded == count ? 0 : 1;
}
