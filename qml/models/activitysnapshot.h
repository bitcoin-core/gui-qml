// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H
#define BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H

#include <qml/execution.h>
#include <qml/models/activitymap.h>
#include <qml/models/activityexportdata.h>
#include <qml/snapshotquery.h>
#include <QObject>

#include <QMap>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>

namespace walletopening { class Session; }

namespace activityview {
using Row = QMap<int, QVariant>;
using SharedRow = std::shared_ptr<const Row>;
struct Node;
using Tree = std::shared_ptr<const Node>;
/** Immutable order-statistics AVL index. Domain performs every allocation and
 * rebalance; GUI rank lookups visit logarithmically many cached row pointers.
 * Patches transfer an already-built root, avoiding a full GUI vector shift. */
struct Measure {
    int transactions{0}, requests{0};
    qint64 pending{0}, maximum{0};
};
inline qint64 Sum(qint64 left, qint64 right) {
    if ((right > 0 && left > std::numeric_limits<qint64>::max() - right) ||
        (right < 0 && left < std::numeric_limits<qint64>::min() - right)) throw std::overflow_error("Activity aggregate");
    return left + right;
}
struct Node {
    QString id;
    SharedRow row;
    Tree left, right;
    int height{1}, size{1};
    Measure own{}, total{};
};
inline int Size(const Tree& tree) { return tree ? tree->size : 0; }
inline int Height(const Tree& tree) { return tree ? tree->height : 0; }
inline Tree Make(const QString& id, SharedRow row, Tree left = {}, Tree right = {}, Measure own = {}) {
    auto node = std::make_shared<Node>();
    node->id = id; node->row = std::move(row); node->left = std::move(left); node->right = std::move(right);
    node->height = 1 + std::max(Height(node->left), Height(node->right));
    node->size = 1 + Size(node->left) + Size(node->right);
    node->own = node->total = own;
    for (const auto& child : {node->left, node->right}) if (child) {
        node->total.transactions += child->total.transactions; node->total.requests += child->total.requests;
        node->total.pending = Sum(node->total.pending, child->total.pending);
        node->total.maximum = std::max(node->total.maximum, child->total.maximum);
    }
    return node;
}
inline Tree RotateLeft(const Tree& tree) {
    const auto& child = tree->right;
    return Make(child->id, child->row, Make(tree->id, tree->row, tree->left, child->left, tree->own), child->right, child->own);
}
inline Tree RotateRight(const Tree& tree) {
    const auto& child = tree->left;
    return Make(child->id, child->row, child->left, Make(tree->id, tree->row, child->right, tree->right, tree->own), child->own);
}
inline Tree Balance(Tree tree) {
    const int delta = Height(tree->left) - Height(tree->right);
    if (delta > 1) {
        if (Height(tree->left->right) > Height(tree->left->left)) tree = Make(tree->id, tree->row, RotateLeft(tree->left), tree->right, tree->own);
        return RotateRight(tree);
    }
    if (delta < -1) {
        if (Height(tree->right->left) > Height(tree->right->right)) tree = Make(tree->id, tree->row, tree->left, RotateRight(tree->right), tree->own);
        return RotateLeft(tree);
    }
    return tree;
}
struct Ancestor { const Node* node; bool left; };
inline Tree RebuildPath(const std::vector<Ancestor>& path, Tree result) {
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto* node = it->node;
        result = Balance(it->left ? Make(node->id, node->row, std::move(result), node->right, node->own)
            : Make(node->id, node->row, node->left, std::move(result), node->own));
    }
    return result;
}
inline Tree Insert(const Tree& tree, const QString& id, SharedRow row, Measure own = {}) {
    std::vector<Ancestor> path;
    path.reserve(Height(tree));
    auto* node = tree.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = left ? node->left.get() : node->right.get();
    }
    auto result = node ? Make(id, std::move(row), node->left, node->right, own) : Make(id, std::move(row), {}, {}, own);
    return RebuildPath(path, std::move(result));
}
inline Tree Erase(const Tree& tree, const QString& id) {
    std::vector<Ancestor> path;
    path.reserve(Height(tree));
    auto* node = tree.get();
    while (node && node->id != id) {
        const bool left = id < node->id;
        path.push_back({node, left});
        node = left ? node->left.get() : node->right.get();
    }
    Tree result;
    if (node) {
        if (!node->left) result = node->right;
        else if (!node->right) result = node->left;
        else {
            std::vector<Ancestor> successor_path;
            successor_path.reserve(Height(node->right));
            auto* successor = node->right.get();
            while (successor->left) {
                successor_path.push_back({successor, true});
                successor = successor->left.get();
            }
            auto right = RebuildPath(successor_path, successor->right);
            result = Balance(Make(successor->id, successor->row, node->left, std::move(right), successor->own));
        }
    }
    return RebuildPath(path, std::move(result));
}
inline const Node* At(Tree tree, int rank) {
    if (rank < 0 || rank >= Size(tree)) return nullptr;
    while (tree) {
        const int left = Size(tree->left);
        if (rank == left) return tree.get();
        if (rank < left) tree = tree->left;
        else { rank -= left + 1; tree = tree->right; }
    }
    return nullptr;
}
inline int Rank(Tree tree, const QString& id) {
    int rank{0};
    while (tree) {
        if (id == tree->id) return rank + Size(tree->left);
        if (id < tree->id) tree = tree->left;
        else { rank += Size(tree->left) + 1; tree = tree->right; }
    }
    return rank;
}
struct Patch {
    enum class Kind { Insert, Remove, Change, Move } kind;
    int position;
    Tree root;
    QList<int> roles;
    int transaction_delta{0}, request_delta{0}, destination{0};
};
/** Full immutable rows are for Domain filtering/export. Presentation values may
 * use bounded previews; neither consumer reaches into a QObject or Core. */
struct Snapshot {
    quint64 revision{0};
    int display_unit{0};
    std::shared_ptr<const void> detail_source; // Original immutable Native domain frame; never exposed to QML.
    std::shared_ptr<const void> opening;
    std::shared_ptr<const void> request_context; // Original immutable request association inputs.
    std::shared_ptr<const void> presentation_context; // Original Domain filter policy, never exposed to QML.
    std::function<QString(const QString&)> canonical_address; // Bounded Native address codec, evaluated on Domain
    std::function<QString(const QString&)> raw; // Native-only immutable document codec
    Map<SharedRow> rows;
    std::map<QString, int> source_positions;
    Map<QString> order_keys;
    Tree presentation;
    Map<std::shared_ptr<const QVariantMap>> details;
    std::map<QString, QString> request_uris;
    QString selected_txid;
    std::shared_ptr<const QVariantMap> selected_details;
    QString selected_raw;
    std::shared_ptr<const QVariantList> detail_inputs, detail_outputs, detail_actions;
    std::shared_ptr<const QVariantList> full_inputs, full_outputs, full_actions, entry_document;
    int transactions{0}, requests{0};
    // Exact immutable predecessor, not a retained history chain. Every full-row
    // change/removal is listed, including fields outside the GUI preview.
    std::weak_ptr<const Snapshot> delta_base;
    std::vector<QString> changed_ids;
    bool statistics_valid{false};
    size_t transformed_rows{0}; // Native work accounting, not a GUI role.
};
/** Cleanup was reserved before the underlying value was admitted. Drop GUI
 * references before its lease so native cleanup owns every large last ref. */
struct Hold {
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<void> lease;
    ~Hold() { snapshot.reset(); lease.reset(); }
};
class Source {
public:
    virtual ~Source() = default;
    virtual std::shared_ptr<const Hold> preparedSnapshot() const = 0;
    virtual std::shared_ptr<walletopening::Session> opening() const = 0;
    virtual activityexport::Files activityFiles() const { return {}; }
    virtual bool activityLoading() const = 0;
    virtual QString activityError() const = 0;
    virtual int activityRowForId(const QString& id) const = 0;
    virtual QList<QMetaObject::Connection> observeActivity(QObject* receiver, std::function<void()> changed) = 0;
};
} // namespace activityview
Q_DECLARE_INTERFACE(activityview::Source, "org.bitcoincore.qt.activitysnapshot/1.0")

#endif // BITCOIN_QML_MODELS_ACTIVITYSNAPSHOT_H
