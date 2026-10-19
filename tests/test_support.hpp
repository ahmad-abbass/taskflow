#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Exception, typename Function>
void throws(Function operation) {
    bool caught = false;
    try { operation(); } catch (const Exception&) { caught = true; }
    require(caught, "expected exception was not thrown");
}

class Suite {
    std::vector<std::pair<std::string, std::function<void()>>> cases_;
public:
    void add(std::string name, std::function<void()> operation) {
        cases_.emplace_back(std::move(name), std::move(operation));
    }
    int run() const {
        std::size_t passed = 0;
        for (const auto& [name, operation] : cases_) {
            try { operation(); ++passed; std::cout << "PASS " << name << '\n'; }
            catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        }
        std::cout << passed << '/' << cases_.size() << " checks passed\n";
        return passed == cases_.size() ? 0 : 1;
    }
};
