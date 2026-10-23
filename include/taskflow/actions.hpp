#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace taskflow {

struct TaskResult {
    bool success;
    std::string output;
};

class TaskAction {
public:
    virtual ~TaskAction() = default;
    virtual std::string kind() const = 0;
    virtual const std::string& payload() const = 0;
    virtual TaskResult execute(std::size_t attempt) const = 0;
};

class SumAction final : public TaskAction {
    std::string payload_;
    std::vector<std::int64_t> values_;
public:
    explicit SumAction(std::string payload);
    std::string kind() const override { return "sum"; }
    const std::string& payload() const override { return payload_; }
    TaskResult execute(std::size_t attempt) const override;
};

class WordCountAction final : public TaskAction {
    std::string payload_;
public:
    explicit WordCountAction(std::string payload) : payload_(std::move(payload)) {}
    std::string kind() const override { return "count"; }
    const std::string& payload() const override { return payload_; }
    TaskResult execute(std::size_t attempt) const override;
};

std::int64_t parseInteger(const std::string& text);

class FlakyAction final : public TaskAction {
    std::string payload_;
    std::size_t failures_;
    std::string message_;
public:
    explicit FlakyAction(std::string payload);
    std::string kind() const override { return "flaky"; }
    const std::string& payload() const override { return payload_; }
    TaskResult execute(std::size_t attempt) const override;
};

std::unique_ptr<TaskAction> makeAction(const std::string& kind, const std::string& payload);

}
