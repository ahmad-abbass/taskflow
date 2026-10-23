#include "taskflow/actions.hpp"

#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace taskflow {

std::int64_t parseInteger(const std::string& text) {
    std::int64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("expected a signed 64-bit integer");
    return value;
}

SumAction::SumAction(std::string payload) : payload_(std::move(payload)) {
    std::istringstream input(payload_);
    std::string token;
    while (input >> token) values_.push_back(parseInteger(token));
    if (values_.empty()) throw std::invalid_argument("sum requires at least one integer");
}

TaskResult SumAction::execute(std::size_t) const {
    std::int64_t result = 0;
    for (const auto value : values_) {
        if ((value > 0 && result > std::numeric_limits<std::int64_t>::max() - value) ||
            (value < 0 && result < std::numeric_limits<std::int64_t>::min() - value))
            return {false, "sum exceeds signed 64-bit range"};
        result += value;
    }
    return {true, std::to_string(result)};
}

TaskResult WordCountAction::execute(std::size_t) const {
    std::size_t words = 0;
    bool inWord = false;
    for (const unsigned char ch : payload_) {
        const bool space = std::isspace(ch) != 0;
        if (!space && !inWord) ++words;
        inWord = !space;
    }
    return {true, std::to_string(words)};
}

std::unique_ptr<TaskAction> makeAction(const std::string& kind, const std::string& payload) {
    if (payload.size() > 10000) throw std::invalid_argument("payload exceeds 10,000 bytes");
    if (kind == "sum") return std::make_unique<SumAction>(payload);
    if (kind == "count") return std::make_unique<WordCountAction>(payload);
    if (kind == "flaky") return std::make_unique<FlakyAction>(payload);
    throw std::invalid_argument("unknown action; choose sum or count");
}

FlakyAction::FlakyAction(std::string payload) : payload_(std::move(payload)) {
    std::istringstream input(payload_);
    std::string count;
    if (!(input >> count)) throw std::invalid_argument("flaky requires a failure count and optional message");
    const auto value = parseInteger(count);
    if (value < 0 || value > 10) throw std::invalid_argument("flaky failure count must be between 0 and 10");
    failures_ = static_cast<std::size_t>(value);
    std::getline(input >> std::ws, message_);
}

TaskResult FlakyAction::execute(std::size_t attempt) const {
    if (attempt <= failures_) return {false, "simulated transient failure"};
    return {true, message_};
}

}
