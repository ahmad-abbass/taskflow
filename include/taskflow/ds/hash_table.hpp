#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace taskflow::ds {

struct StringHash {
    std::size_t operator()(const std::string& key) const noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned char ch : key) { hash ^= ch; hash *= 1099511628211ULL; }
        return static_cast<std::size_t>(hash);
    }
};

template<class Value, class Hash = StringHash>
class HashTable {
    struct Node {
        std::string key;
        Value value;
        std::unique_ptr<Node> next;
        Node(std::string k, Value v) : key(std::move(k)), value(std::move(v)) {}
    };
    std::vector<std::unique_ptr<Node>> buckets_;
    std::size_t size_ = 0;
    Hash hash_;

    void rehash(std::size_t count) {
        std::vector<std::unique_ptr<Node>> next(count);
        for (auto& bucket : buckets_) {
            while (bucket) {
                auto node = std::move(bucket);
                bucket = std::move(node->next);
                const auto index = hash_(node->key) % count;
                node->next = std::move(next[index]);
                next[index] = std::move(node);
            }
        }
        buckets_ = std::move(next);
    }

public:
    explicit HashTable(std::size_t buckets = 8) : buckets_(buckets ? buckets : 8) {}
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;
    HashTable(HashTable&& other) noexcept
        : buckets_(std::move(other.buckets_)), size_(std::exchange(other.size_, 0)), hash_(std::move(other.hash_)) {}
    HashTable& operator=(HashTable&& other) noexcept {
        if (this != &other) {
            clear();
            buckets_ = std::move(other.buckets_);
            size_ = std::exchange(other.size_, 0);
            hash_ = std::move(other.hash_);
        }
        return *this;
    }
    ~HashTable() { clear(); }
    Value* find(const std::string& key) {
        if (buckets_.empty()) return nullptr;
        for (auto* node = buckets_[hash_(key) % buckets_.size()].get(); node; node = node->next.get())
            if (node->key == key) return &node->value;
        return nullptr;
    }
    const Value* find(const std::string& key) const {
        if (buckets_.empty()) return nullptr;
        for (const auto* node = buckets_[hash_(key) % buckets_.size()].get(); node; node = node->next.get())
            if (node->key == key) return &node->value;
        return nullptr;
    }
    Value& insert(std::string key, Value value) {
        if (find(key)) throw std::invalid_argument("duplicate hash key");
        if (buckets_.empty()) rehash(8);
        if ((size_ + 1) > buckets_.size() * 3 / 4) rehash(buckets_.size() * 2);
        auto node = std::make_unique<Node>(std::move(key), std::move(value));
        const auto index = hash_(node->key) % buckets_.size();
        node->next = std::move(buckets_[index]);
        buckets_[index] = std::move(node);
        ++size_;
        return buckets_[index]->value;
    }
    void clear() noexcept {
        for (auto& bucket : buckets_) {
            while (bucket) {
                auto old = std::move(bucket);
                bucket = std::move(old->next);
            }
        }
        size_ = 0;
    }
    std::size_t size() const noexcept { return size_; }
    std::size_t bucketCount() const noexcept { return buckets_.size(); }
};

}
