// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_INDEXEDSEQUENCE_H
#define BITCOIN_QML_INDEXEDSEQUENCE_H

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <exception>
#include <utility>

// GUI row bookkeeping with logarithmic single-row work. Native Qt indexes keep
// their normal begin/end row notifications; domain inventories live elsewhere.
template <typename Value>
class IndexedSequence
{
    struct Node {
        Value value;
        std::unique_ptr<Node> left, right;
        Node* parent{nullptr};
        int height{1}, count{1};
        explicit Node(Value item) : value(std::move(item)) {}
    };
    std::unique_ptr<Node> m_root;
    std::map<Value, Node*> m_index;
    static int size(const std::unique_ptr<Node>& node) { return node ? node->count : 0; }
    static int height(const std::unique_ptr<Node>& node) { return node ? node->height : 0; }
    static void update(Node* node) {
        node->height = 1 + std::max(height(node->left), height(node->right));
        node->count = 1 + size(node->left) + size(node->right);
        if (node->left) node->left->parent = node;
        if (node->right) node->right->parent = node;
    }
    static void left(std::unique_ptr<Node>& node) {
        auto parent = node->parent; auto next = std::move(node->right);
        node->right = std::move(next->left); update(node.get());
        next->left = std::move(node); update(next.get()); next->parent = parent; node = std::move(next);
    }
    static void right(std::unique_ptr<Node>& node) {
        auto parent = node->parent; auto next = std::move(node->left);
        node->left = std::move(next->right); update(node.get());
        next->right = std::move(node); update(next.get()); next->parent = parent; node = std::move(next);
    }
    static void balance(std::unique_ptr<Node>& node) {
        if (!node) return;
        update(node.get());
        if (height(node->left) - height(node->right) > 1) {
            if (height(node->left->right) > height(node->left->left)) left(node->left);
            right(node);
        } else if (height(node->right) - height(node->left) > 1) {
            if (height(node->right->left) > height(node->right->right)) right(node->right);
            left(node);
        }
    }
    // An int-sized AVL tree has height below 64. These fixed ancestor stacks
    // keep every GUI bookkeeping operation logarithmic without recursion.
    using Path = std::array<std::unique_ptr<Node>*, 64>;
    static void insert(std::unique_ptr<Node>& tree, int row, std::unique_ptr<Node> node) {
        Path path{};
        size_t depth{0};
        auto* link = &tree;
        while (*link) {
            path.at(depth++) = link;
            const int before = size((*link)->left);
            if (row <= before) link = &(*link)->left;
            else { row -= before + 1; link = &(*link)->right; }
        }
        *link = std::move(node);
        while (depth) balance(*path[--depth]);
    }
    static std::unique_ptr<Node> take(std::unique_ptr<Node>& tree, int row) {
        Path path{};
        size_t depth{0};
        auto* link = &tree;
        for (;;) {
            const int before = size((*link)->left);
            if (row == before) break;
            path.at(depth++) = link;
            if (row < before) link = &(*link)->left;
            else { row -= before + 1; link = &(*link)->right; }
        }
        auto result = std::move(*link);
        auto* parent = result->parent;
        if (!result->right) *link = std::move(result->left);
        else {
            Path successor_path{};
            size_t successor_depth{0};
            auto* successor_link = &result->right;
            while ((*successor_link)->left) {
                successor_path.at(successor_depth++) = successor_link;
                successor_link = &(*successor_link)->left;
            }
            auto successor = std::move(*successor_link);
            *successor_link = std::move(successor->right);
            if (*successor_link) (*successor_link)->parent = successor->parent;
            while (successor_depth) balance(*successor_path[--successor_depth]);
            successor->left = std::move(result->left);
            successor->right = std::move(result->right);
            *link = std::move(successor);
        }
        if (*link) (*link)->parent = parent;
        result->parent = nullptr;
        result->count = result->height = 1;
        balance(*link);
        while (depth) balance(*path[--depth]);
        return result;
    }

public:
    int size() const { return size(m_root); }
    bool empty() const { return !m_root; }
    Value& operator[](int row) const {
        auto* node = m_root.get();
        while (node) {
            const int before = size(node->left);
            if (row == before) return node->value;
            if (row < before) node = node->left.get();
            else { row -= before + 1; node = node->right.get(); }
        }
        std::terminate();
    }
    int indexOf(Value value) const {
        const auto found = m_index.find(value); if (found == m_index.end()) return -1;
        auto* node = found->second; int row = size(node->left);
        while (node->parent) {
            if (node == node->parent->right.get()) row += size(node->parent->left) + 1;
            node = node->parent;
        }
        return row;
    }
    void insertAt(int row, Value value) {
        auto node = std::make_unique<Node>(value); m_index.emplace(value, node.get());
        insert(m_root, row, std::move(node)); m_root->parent = nullptr;
    }
    void append(Value value) { insertAt(size(), std::move(value)); }
    Value takeAt(int row) {
        auto node = take(m_root, row); m_index.erase(node->value); if (m_root) m_root->parent = nullptr;
        return std::move(node->value);
    }
};
#endif // BITCOIN_QML_INDEXEDSEQUENCE_H
