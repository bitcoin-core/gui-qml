// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_ACTIVITYMAP_H
#define BITCOIN_QML_MODELS_ACTIVITYMAP_H

#include <QString>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace activityview {
/** Immutable ordered Activity values. Snapshot copies share one root; Domain
 * edits allocate only the search/rebalance paths. GUI may perform bounded
 * lookups while the snapshot's existing Native cleanup lease owns the root. */
template <typename Value>
class Map
{
public:
    using value_type = std::pair<const QString, Value>;
    struct Node;
    using Tree = std::shared_ptr<const Node>;
    struct Node {
        const value_type entry;
        const Tree left, right;
        const int height;
        const size_t size;
        Node(QString key, Value value, Tree a, Tree b)
            : entry(std::move(key), std::move(value)), left(std::move(a)), right(std::move(b)),
              height(1 + std::max(left ? left->height : 0, right ? right->height : 0)),
              size(1 + (left ? left->size : 0) + (right ? right->size : 0)) {}
    };
    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Map::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;
        const_iterator() = default;
        reference operator*() const { return m_node->entry; }
        pointer operator->() const { return &m_node->entry; }
        const_iterator& operator++() {
            if (!m_node) return *this;
            if (m_node->right) {
                m_path.push_back(m_node); m_node = m_node->right.get();
                while (m_node->left) { m_path.push_back(m_node); m_node = m_node->left.get(); }
            } else {
                const auto* previous = m_node; m_node = nullptr;
                while (!m_path.empty()) {
                    const auto* parent = m_path.back(); m_path.pop_back();
                    if (parent->left.get() == previous) { m_node = parent; break; }
                    previous = parent;
                }
            }
            return *this;
        }
        const_iterator operator++(int) { auto previous = *this; ++*this; return previous; }
        bool operator==(const const_iterator& other) const { return m_node == other.m_node; }
    private:
        friend class Map;
        Tree m_root;
        const Node* m_node{nullptr};
        std::vector<const Node*> m_path;
    };

    size_t size() const { return m_root ? m_root->size : 0; }
    bool empty() const { return !m_root; }
    const Tree& storage() const { return m_root; }
    const_iterator begin() const {
        const_iterator result; result.m_root = m_root; result.m_node = m_root.get();
        while (result.m_node && result.m_node->left) { result.m_path.push_back(result.m_node); result.m_node = result.m_node->left.get(); }
        return result;
    }
    const_iterator end() const { return {}; }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    const_iterator find(const QString& key) const {
        const_iterator result; result.m_root = m_root; result.m_node = m_root.get();
        while (result.m_node && result.m_node->entry.first != key) {
            result.m_path.push_back(result.m_node);
            result.m_node = (key < result.m_node->entry.first ? result.m_node->left : result.m_node->right).get();
        }
        return result;
    }
    bool contains(const QString& key) const { return find(key) != end(); }
    const Value& at(const QString& key) const {
        const auto found = find(key);
        if (found == end()) throw std::out_of_range("Activity map key");
        return found->second;
    }
    std::pair<const_iterator, bool> emplace(QString key, Value value) {
        const auto found = find(key); if (found != end()) return {found, false};
        m_root = assign(m_root, key, std::move(value)); return {find(key), true};
    }
    std::pair<const_iterator, bool> insert_or_assign(QString key, Value value) {
        const auto found = find(key);
        const bool inserted = found == end();
        if (!inserted && found->second == value) return {found, false};
        m_root = assign(m_root, key, std::move(value)); return {find(key), inserted};
    }
    size_t erase(const QString& key) {
        if (!contains(key)) return 0;
        m_root = remove(m_root, key); return 1;
    }
    void clear() { m_root.reset(); }

private:
    static int height(const Tree& tree) { return tree ? tree->height : 0; }
    static Tree make(const QString& key, Value value, Tree left = {}, Tree right = {}) {
        return std::make_shared<const Node>(key, std::move(value), std::move(left), std::move(right));
    }
    static Tree left(const Tree& tree) {
        const auto& child = tree->right;
        return make(child->entry.first, child->entry.second, make(tree->entry.first, tree->entry.second, tree->left, child->left), child->right);
    }
    static Tree right(const Tree& tree) {
        const auto& child = tree->left;
        return make(child->entry.first, child->entry.second, child->left, make(tree->entry.first, tree->entry.second, child->right, tree->right));
    }
    static Tree balance(Tree tree) {
        const auto difference = height(tree->left) - height(tree->right);
        if (difference > 1) {
            if (height(tree->left->right) > height(tree->left->left)) tree = make(tree->entry.first, tree->entry.second, left(tree->left), tree->right);
            return right(tree);
        }
        if (difference < -1) {
            if (height(tree->right->left) > height(tree->right->right)) tree = make(tree->entry.first, tree->entry.second, tree->left, right(tree->right));
            return left(tree);
        }
        return tree;
    }
    struct Ancestor { const Node* node; bool left; };
    static Tree rebuild(const std::vector<Ancestor>& path, Tree result) {
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const auto* node = it->node;
            result = balance(it->left ? make(node->entry.first, node->entry.second, std::move(result), node->right)
                : make(node->entry.first, node->entry.second, node->left, std::move(result)));
        }
        return result;
    }
    static Tree assign(const Tree& tree, const QString& key, Value value) {
        std::vector<Ancestor> path;
        path.reserve(height(tree));
        auto* node = tree.get();
        while (node && node->entry.first != key) {
            const bool take_left = key < node->entry.first;
            path.push_back({node, take_left});
            node = (take_left ? node->left : node->right).get();
        }
        auto result = node ? make(key, std::move(value), node->left, node->right) : make(key, std::move(value));
        return rebuild(path, std::move(result));
    }
    static Tree remove(const Tree& tree, const QString& key) {
        std::vector<Ancestor> path;
        path.reserve(height(tree));
        auto* node = tree.get();
        while (node && node->entry.first != key) {
            const bool take_left = key < node->entry.first;
            path.push_back({node, take_left});
            node = (take_left ? node->left : node->right).get();
        }
        if (!node) return tree;
        Tree result;
        if (!node->left) result = node->right;
        else if (!node->right) result = node->left;
        else {
            std::vector<Ancestor> successor_path;
            successor_path.reserve(height(node->right));
            auto* successor = node->right.get();
            while (successor->left) {
                successor_path.push_back({successor, true}); successor = successor->left.get();
            }
            auto remaining = rebuild(successor_path, successor->right);
            result = balance(make(successor->entry.first, successor->entry.second, node->left, std::move(remaining)));
        }
        return rebuild(path, std::move(result));
    }
    Tree m_root;
};
} // namespace activityview
#endif // BITCOIN_QML_MODELS_ACTIVITYMAP_H
