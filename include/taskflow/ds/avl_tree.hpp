#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace taskflow::ds {

template<class Value>
class AvlTree {
    struct Node {
        std::string key;
        Value value;
        std::unique_ptr<Node> left, right;
        int height = 1;
        std::size_t count = 1;
        Node(std::string k, Value v) : key(std::move(k)), value(std::move(v)) {}
    };
    std::unique_ptr<Node> root_;
    static int height(const std::unique_ptr<Node>& node) { return node ? node->height : 0; }
    static std::size_t count(const std::unique_ptr<Node>& node) { return node ? node->count : 0; }
    static void update(Node& node) {
        node.height = 1 + std::max(height(node.left), height(node.right));
        node.count = 1 + count(node.left) + count(node.right);
    }
    static int balance(const Node& node) { return height(node.left) - height(node.right); }
    static void rotateLeft(std::unique_ptr<Node>& root) {
        auto next = std::move(root->right);
        root->right = std::move(next->left);
        update(*root);
        next->left = std::move(root);
        update(*next);
        root = std::move(next);
    }
    static void rotateRight(std::unique_ptr<Node>& root) {
        auto next = std::move(root->left);
        root->left = std::move(next->right);
        update(*root);
        next->right = std::move(root);
        update(*next);
        root = std::move(next);
    }
    static void insertAt(std::unique_ptr<Node>& root, std::string key, Value value) {
        if (!root) { root = std::make_unique<Node>(std::move(key), std::move(value)); return; }
        if (key == root->key) throw std::invalid_argument("duplicate AVL key");
        if (key < root->key) insertAt(root->left, std::move(key), std::move(value));
        else insertAt(root->right, std::move(key), std::move(value));
        update(*root);
        if (balance(*root) > 1) {
            if (balance(*root->left) < 0) rotateLeft(root->left);
            rotateRight(root);
        } else if (balance(*root) < -1) {
            if (balance(*root->right) > 0) rotateRight(root->right);
            rotateLeft(root);
        }
    }
    static void collect(const Node* node, const std::string* low, const std::string* high, std::vector<Value>& result) {
        if (!node) return;
        if (!low || *low < node->key) collect(node->left.get(), low, high, result);
        if ((!low || *low <= node->key) && (!high || node->key <= *high)) result.push_back(node->value);
        if (!high || node->key < *high) collect(node->right.get(), low, high, result);
    }
    struct Audit { bool valid; int height; std::size_t count; };
    static Audit audit(const Node* node, const std::string* low, const std::string* high) {
        if (!node) return {true, 0, 0};
        const auto left = audit(node->left.get(), low, &node->key);
        const auto right = audit(node->right.get(), &node->key, high);
        const int h = 1 + std::max(left.height, right.height);
        const auto n = 1 + left.count + right.count;
        const int b = left.height - right.height;
        return {left.valid && right.valid && (!low || *low < node->key) && (!high || node->key < *high) &&
                b >= -1 && b <= 1 && node->height == h && node->count == n, h, n};
    }
public:
    void insert(std::string key, Value value) { insertAt(root_, std::move(key), std::move(value)); }
    std::size_t rank(const std::string& key) const {
        std::size_t result = 0;
        auto* node = root_.get();
        while (node) {
            if (key <= node->key) node = node->left.get();
            else { result += 1 + count(node->left); node = node->right.get(); }
        }
        return result;
    }
    const Value* select(std::size_t index) const {
        auto* node = root_.get();
        while (node) {
            const auto left = count(node->left);
            if (index == left) return &node->value;
            if (index < left) node = node->left.get();
            else { index -= left + 1; node = node->right.get(); }
        }
        return nullptr;
    }
    std::vector<Value> range(const std::string& low, const std::string& high) const {
        std::vector<Value> result;
        if (low <= high) collect(root_.get(), &low, &high, result);
        return result;
    }
    std::vector<Value> values() const {
        std::vector<Value> result;
        result.reserve(size()); collect(root_.get(), nullptr, nullptr, result);
        return result;
    }
    std::size_t size() const noexcept { return count(root_); }
    int height() const noexcept { return height(root_); }
    bool valid() const { return audit(root_.get(), nullptr, nullptr).valid; }
};

}
