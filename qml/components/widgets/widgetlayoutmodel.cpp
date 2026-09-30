// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/widgetlayoutmodel.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>
#include <memory>

namespace {
constexpr int MAX_ROW{1000};
const QList<QSize> ALLOWED_SIZES{{1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}};

std::unique_ptr<QSettings> Settings(const QString& file)
{
    return file.isEmpty() ? std::make_unique<QSettings>() : std::make_unique<QSettings>(file, QSettings::IniFormat);
}
} // namespace

WidgetLayoutModel::WidgetLayoutModel(QObject* parent, const QString& settings_file)
    : QAbstractListModel(parent), m_settings_file(settings_file)
{
}

int WidgetLayoutModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant WidgetLayoutModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto& entry = m_entries[index.row()];
    switch (role) {
    case WidgetIdRole: return entry.widget_id;
    case InstanceIdRole: return entry.id;
    case TitleRole: return definition(entry.widget_id).value("title");
    case SourceRole: return definition(entry.widget_id).value("source");
    case ColumnRole: return entry.rect.x();
    case RowRole: return entry.rect.y();
    case ColumnSpanRole: return entry.rect.width();
    case RowSpanRole: return entry.rect.height();
    case SizesRole: return definition(entry.widget_id).value("sizes");
    default: return {};
    }
}

QHash<int, QByteArray> WidgetLayoutModel::roleNames() const
{
    return {{WidgetIdRole, "widgetId"}, {TitleRole, "widgetTitle"}, {SourceRole, "widgetSource"},
            {ColumnRole, "gridColumn"}, {RowRole, "gridRow"}, {ColumnSpanRole, "columnSpan"},
            {RowSpanRole, "rowSpan"}, {SizesRole, "supportedSizes"}, {InstanceIdRole, "instanceId"}};
}

void WidgetLayoutModel::setCatalog(const QVariantList& catalog)
{
    if (catalog == m_catalog) return;
    cancelInteraction();
    m_catalog.clear();
    QSet<QString> seen;
    for (const auto& value : catalog) {
        auto item = value.toMap();
        const QString id = item.value("id").toString();
        if (id.isEmpty() || seen.contains(id)) continue;
        QVariantList valid_sizes;
        for (const auto& candidate : item.value("sizes").toList()) {
            const auto size = candidate.toMap();
            if (ALLOWED_SIZES.contains(QSize(size.value("columns").toInt(), size.value("rows").toInt())) && !valid_sizes.contains(candidate)) valid_sizes.append(candidate);
        }
        if (valid_sizes.isEmpty()) continue;
        seen.insert(id);
        item["sizes"] = valid_sizes;
        m_catalog.append(item);
        if (m_catalog.size() == 64) break;
    }
    Q_EMIT catalogChanged();
    if (!m_entries.isEmpty()) Q_EMIT dataChanged(index(0), index(m_entries.size() - 1));
}

void WidgetLayoutModel::setStorageKey(const QString& key)
{
    if (key == m_storage_key) return;
    m_storage_key = key;
    Q_EMIT storageKeyChanged();
}

QVariantMap WidgetLayoutModel::definition(const QString& id) const
{
    for (const auto& value : m_catalog) {
        const auto item = value.toMap();
        if (item.value("id").toString() == id) return item;
    }
    return {};
}

QList<QSize> WidgetLayoutModel::sizes(const QString& id) const
{
    QList<QSize> result;
    for (const auto& value : definition(id).value("sizes").toList()) {
        const auto size = value.toMap();
        result.append(QSize(size.value("columns").toInt(), size.value("rows").toInt()));
    }
    return result;
}

QSize WidgetLayoutModel::defaultSize(const QString& id) const
{
    const auto allowed = sizes(id);
    const int index = definition(id).value("defaultSize", 0).toInt();
    return allowed.isEmpty() ? QSize(1, 1) : allowed.value(index, allowed.first());
}

int WidgetLayoutModel::unplacedCount() const
{
    return std::count_if(m_saved.begin(), m_saved.end(), [&](const Entry& entry) { return indexOf(entry.id) < 0; });
}

int WidgetLayoutModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i) if (m_entries[i].id == id) return i;
    return -1;
}

bool WidgetLayoutModel::contains(const QString& id) const
{
    return std::any_of(m_saved.begin(), m_saved.end(), [&](const Entry& entry) { return entry.widget_id == id; });
}

QRect WidgetLayoutModel::nearestFree(QRect rect, const QList<Entry>& occupied) const
{
    rect.moveLeft(std::clamp(rect.x(), 0, m_columns - rect.width()));
    rect.moveTop(std::clamp(rect.y(), 0, rows() - rect.height()));
    auto free = [&](const QRect& candidate) {
        return std::none_of(occupied.begin(), occupied.end(), [&](const Entry& other) { return candidate.intersects(other.rect); });
    };
    if (free(rect)) return rect;
    QRect best;
    int best_distance = std::numeric_limits<int>::max();
    for (int row = 0; row <= rows() - rect.height(); ++row) {
        for (int column = 0; column <= m_columns - rect.width(); ++column) {
            const int distance = std::abs(row - rect.y()) + std::abs(column - rect.x());
            if (distance >= best_distance) continue;
            QRect candidate(QPoint(column, row), rect.size());
            if (free(candidate)) {
                best = candidate;
                best_distance = distance;
            }
        }
    }
    return best;
}

std::optional<QList<WidgetLayoutModel::Entry>> WidgetLayoutModel::arrange(const QList<Entry>& entries, const QString& pinned_id) const
{
    int area{0};
    for (const auto& entry : entries) area += entry.rect.width() * entry.rect.height();
    if (area > m_columns * rows()) return std::nullopt;

    // Try the inexpensive, minimally disruptive placement first.
    QList<Entry> occupied;
    QList<Entry> result = entries;
    if (!pinned_id.isEmpty()) {
        for (const auto& entry : entries) if (entry.id == pinned_id) occupied.append(entry);
    }
    for (const auto& entry : entries) {
        if (entry.id == pinned_id) continue;
        if (entry.rect.x() >= 0 && entry.rect.y() >= 0 && entry.rect.right() < m_columns && entry.rect.bottom() < rows() &&
            std::none_of(occupied.begin(), occupied.end(), [&](const Entry& other) { return entry.rect.intersects(other.rect); })) occupied.append(entry);
    }
    bool placed_all{true};
    for (auto& entry : result) {
        const auto found = std::find_if(occupied.begin(), occupied.end(), [&](const Entry& other) { return other.id == entry.id; });
        if (found != occupied.end()) continue;
        entry.rect = nearestFree(entry.rect, occupied);
        if (!entry.rect.isValid()) { placed_all = false; break; }
        occupied.append(entry);
    }
    if (placed_all) return result;

    // At most twenty cells permit an exact search when greedy placement fragments the
    // free space. Never reject a layout that can fit, or create overflow rows.
    struct Candidate { QRect rect; quint32 mask; };
    QList<QList<Candidate>> candidates;
    for (const auto& entry : entries) {
        QList<Candidate> options;
        for (int row = 0; row <= rows() - entry.rect.height(); ++row) {
            for (int column = 0; column <= m_columns - entry.rect.width(); ++column) {
                QRect rect(QPoint(column, row), entry.rect.size());
                if (entry.id == pinned_id && rect != entry.rect) continue;
                quint32 mask{0};
                for (int y = rect.top(); y <= rect.bottom(); ++y) {
                    for (int x = rect.left(); x <= rect.right(); ++x) mask |= quint32{1} << (y * m_columns + x);
                }
                options.append({rect, mask});
            }
        }
        std::stable_sort(options.begin(), options.end(), [&](const Candidate& left, const Candidate& right) {
            return (left.rect.topLeft() - entry.rect.topLeft()).manhattanLength() < (right.rect.topLeft() - entry.rect.topLeft()).manhattanLength();
        });
        candidates.append(options);
    }
    QList<int> order;
    for (int i = 0; i < entries.size(); ++i) order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        if ((entries[left].id == pinned_id) != (entries[right].id == pinned_id)) return entries[left].id == pinned_id;
        return entries[left].rect.width() * entries[left].rect.height() > entries[right].rect.width() * entries[right].rect.height();
    });
    QSet<quint64> failed;
    result = entries;
    std::function<bool(int, quint32)> search = [&](int depth, quint32 mask) {
        if (depth == order.size()) return true;
        const quint64 key = (quint64(depth) << 32) | mask;
        if (failed.contains(key)) return false;
        const int index = order[depth];
        for (const auto& candidate : candidates[index]) {
            if (mask & candidate.mask) continue;
            result[index].rect = candidate.rect;
            if (search(depth + 1, mask | candidate.mask)) return true;
        }
        failed.insert(key);
        return false;
    };
    return search(0, 0) ? std::optional{result} : std::nullopt;
}

QList<WidgetLayoutModel::Entry> WidgetLayoutModel::reflow(const QList<Entry>& entries) const
{
    if (const auto layout = arrange(entries)) return *layout;
    // Preserve non-fitting saved widgets for another orientation or after space
    // is freed. The picker exposes them, and save() keeps their records intact.
    QList<Entry> result;
    for (const auto& entry : entries) {
        auto candidate = result;
        candidate.append(entry);
        if (const auto layout = arrange(candidate)) result = *layout;
    }
    return result;
}

void WidgetLayoutModel::publish(const QList<Entry>& entries)
{
    if (entries == m_entries) return;
    // Adding/removing one widget must not recreate the other widgets' content.
    if (entries.size() == m_entries.size() + 1 && std::equal(m_entries.begin(), m_entries.end(), entries.begin())) {
        beginInsertRows({}, m_entries.size(), m_entries.size());
        m_entries = entries;
        endInsertRows();
        Q_EMIT layoutChanged();
        return;
    }
    if (entries.size() + 1 == m_entries.size()) {
        int removed{0};
        while (removed < entries.size() && entries[removed] == m_entries[removed]) ++removed;
        if (std::equal(entries.begin() + removed, entries.end(), m_entries.begin() + removed + 1)) {
            beginRemoveRows({}, removed, removed);
            m_entries = entries;
            endRemoveRows();
            Q_EMIT layoutChanged();
            return;
        }
    }
    bool same_ids = entries.size() == m_entries.size();
    for (int i = 0; same_ids && i < entries.size(); ++i) same_ids = entries[i].id == m_entries[i].id;
    if (same_ids) {
        // Preserve delegates and their pointer grabs during previews.
        m_entries = entries;
        if (!m_entries.isEmpty()) Q_EMIT dataChanged(index(0), index(m_entries.size() - 1));
    } else {
        beginResetModel();
        m_entries = entries;
        endResetModel();
    }
    Q_EMIT layoutChanged();
}

void WidgetLayoutModel::setColumns(int columns)
{
    if (columns < 3 || columns > 6 || columns == m_columns) return;
    cancelInteraction();
    if (m_restored) save(false);
    m_columns = columns;
    applyArrangement();
    if (m_restored) save(false);
    Q_EMIT columnsChanged();
    Q_EMIT layoutChanged();
}

void WidgetLayoutModel::applyArrangement()
{
    // Membership and sizes are shared; only positions differ between boards.
    const auto positions = m_arrangements.value(m_columns);
    for (auto& entry : m_saved) {
        const auto found = std::find_if(positions.begin(), positions.end(), [&](const Entry& other) { return entry.id == other.id; });
        if (found != positions.end()) entry.rect.moveTopLeft(found->rect.topLeft());
    }
    publish(reflow(m_saved));
}

void WidgetLayoutModel::restore()
{
    cancelInteraction();
    const auto settings = Settings(m_settings_file);
    const auto object = m_storage_key.isEmpty() ? QJsonObject{} : QJsonDocument::fromJson(settings->value(m_storage_key).toByteArray()).object();
    m_saved.clear();
    m_arrangements.clear();
    const int version = object.value("version").toInt();
    const auto read_entries = [&](const QJsonArray& widgets) {
        QList<Entry> entries;
        QSet<QString> seen;
        for (const auto value : widgets) {
            const auto item = value.toObject();
            const QString id = item.value("id").toString();
            const QString widget_id = version >= 3 ? item.value("widgetId").toString() : id;
            if (id.isEmpty() || seen.contains(id) || definition(widget_id).isEmpty()) continue;
            seen.insert(id);
            QSize size(item.value("columns").toInt(), item.value("rows").toInt());
            if (!sizes(widget_id).contains(size)) size = defaultSize(widget_id);
            entries.append({id, QRect(QPoint(std::clamp(item.value("column").toInt(), 0, 11), std::clamp(item.value("row").toInt(), 0, MAX_ROW)), size), widget_id});
        }
        return entries;
    };
    m_persist_reflow = (version >= 1 && version <= 3) && object.value("widgets").isArray();
    if ((version >= 1 && version <= 3) && object.value("widgets").isArray()) {
        m_saved = read_entries(object.value("widgets").toArray());
        if (version >= 2) {
            const auto layouts = object.value("layouts").toObject();
            for (int columns = 3; columns <= 6; ++columns) {
                if (layouts.value(QString::number(columns)).isArray()) {
                    m_arrangements.insert(columns, read_entries(layouts.value(QString::number(columns)).toArray()));
                }
            }
        } else {
            // Version 1 had one board and no shape metadata. Rows below the
            // third identify a portrait arrangement; otherwise preserve 6x3.
            const bool portrait = std::any_of(m_saved.begin(), m_saved.end(), [](const Entry& entry) { return entry.rect.bottom() >= 3; });
            m_arrangements.insert(portrait ? 3 : 6, m_saved);
        }
    } else if (!m_catalog.isEmpty()) {
        const QString id = m_catalog.first().toMap().value("id").toString();
        m_saved.append({id, QRect(QPoint(0, 0), defaultSize(id)), id});
    }
    applyArrangement();
    m_restored = true;
}

void WidgetLayoutModel::save(bool user_edit)
{
    for (const auto& entry : m_entries) {
        const auto saved = std::find_if(m_saved.begin(), m_saved.end(), [&](const Entry& other) { return other.id == entry.id; });
        if (saved == m_saved.end()) m_saved.append(entry);
        else *saved = entry;
    }
    m_arrangements.insert(m_columns, m_saved);
    Q_EMIT layoutChanged();
    if (user_edit) m_persist_reflow = true;
    if (m_storage_key.isEmpty() || !m_persist_reflow) return;
    const auto write_entries = [](const QList<Entry>& entries) {
        QJsonArray widgets;
        for (const auto& entry : entries) {
            widgets.append(QJsonObject{{"id", entry.id}, {"widgetId", entry.widget_id}, {"column", entry.rect.x()}, {"row", entry.rect.y()},
                                       {"columns", entry.rect.width()}, {"rows", entry.rect.height()}});
        }
        return widgets;
    };
    QJsonObject layouts;
    for (auto it = m_arrangements.cbegin(); it != m_arrangements.cend(); ++it) {
        layouts.insert(QString::number(it.key()), write_entries(it.value()));
    }
    const auto settings = Settings(m_settings_file);
    settings->setValue(m_storage_key, QJsonDocument(QJsonObject{{"version", 3}, {"widgets", write_entries(m_saved)}, {"layouts", layouts}}).toJson(QJsonDocument::Compact));
    settings->sync();
    //: Error shown on the widget dashboard when saving the arrangement fails.
    const QString error = settings->status() == QSettings::NoError ? QString{} : tr("Your widget arrangement could not be saved.");
    if (error != m_persistence_error) {
        m_persistence_error = error;
        Q_EMIT persistenceErrorChanged();
    }
}

bool WidgetLayoutModel::addWidget(const QString& id, int size_index)
{
    if (definition(id).isEmpty()) return false;
    cancelInteraction();
    const auto allowed = sizes(id);
    const QSize size = size_index < 0 ? defaultSize(id) : allowed.value(size_index);
    if (!size.isValid()) return false;
    auto entries = m_entries;
    // Preserve the original ID for a first instance, including legacy layouts.
    const bool used = std::any_of(m_saved.begin(), m_saved.end(), [&](const Entry& entry) { return entry.id == id; });
    const QString instance_id = used ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    entries.append({instance_id, QRect(QPoint(0, 0), size), id});
    const auto layout = arrange(entries);
    if (!layout) return false;
    publish(*layout);
    save();
    return true;
}

bool WidgetLayoutModel::removeWidget(const QString& id)
{
    if (std::none_of(m_saved.begin(), m_saved.end(), [&](const Entry& entry) { return entry.id == id; })) return false;
    cancelInteraction();
    m_saved.removeIf([&](const Entry& entry) { return entry.id == id; });
    for (auto& entries : m_arrangements) entries.removeIf([&](const Entry& entry) { return entry.id == id; });
    publish(reflow(m_saved));
    save();
    return true;
}

bool WidgetLayoutModel::beginInteraction(const QString& id)
{
    if (indexOf(id) < 0) return false;
    cancelInteraction();
    m_before = m_entries;
    m_active_id = id;
    Q_EMIT activeIdChanged();
    return true;
}

void WidgetLayoutModel::preview(QRect rect)
{
    if (m_active_id.isEmpty()) return;
    rect.moveLeft(std::clamp(rect.x(), 0, m_columns - rect.width()));
    rect.moveTop(std::clamp(rect.y(), 0, rows() - rect.height()));
    auto result = m_before;
    for (auto& entry : result) if (entry.id == m_active_id) entry.rect = rect;
    if (const auto layout = arrange(result, m_active_id)) publish(*layout);
}

void WidgetLayoutModel::previewMove(int column, int row)
{
    if (m_active_id.isEmpty()) return;
    const int i = indexOf(m_active_id);
    preview(QRect(QPoint(column, row), m_before[i].rect.size()));
}

void WidgetLayoutModel::previewResize(double columns, double rows)
{
    if (m_active_id.isEmpty() || !std::isfinite(columns) || !std::isfinite(rows)) return;
    const int i = indexOf(m_active_id);
    const QPoint origin = m_before[i].rect.topLeft();
    const auto distance = [&](QSize size) { return std::pow(columns - size.width(), 2) + std::pow(rows - size.height(), 2); };
    QSize best = m_entries[i].rect.size();
    for (const auto& candidate : sizes(m_before[i].widget_id)) {
        if (origin.x() + candidate.width() > m_columns || origin.y() + candidate.height() > this->rows()) continue;
        // Hysteresis prevents jitter near an equally close pair of sizes.
        if (distance(candidate) + 0.15 < distance(best)) best = candidate;
    }
    preview(QRect(origin, best));
}

void WidgetLayoutModel::commitInteraction()
{
    if (m_active_id.isEmpty()) return;
    const bool changed = m_before != m_entries;
    m_active_id.clear();
    m_before.clear();
    Q_EMIT activeIdChanged();
    if (changed) {
        save();
        if (unplacedCount() > 0) publish(reflow(m_saved));
    }
}

void WidgetLayoutModel::cancelInteraction()
{
    if (m_active_id.isEmpty()) return;
    const auto before = m_before;
    m_active_id.clear();
    m_before.clear();
    publish(before);
    Q_EMIT activeIdChanged();
}

QVariantMap WidgetLayoutModel::geometry(const QString& id) const
{
    const int i = indexOf(id);
    if (i < 0) return {};
    const auto& rect = m_entries[i].rect;
    return {{"column", rect.x()}, {"row", rect.y()}, {"columns", rect.width()}, {"rows", rect.height()}};
}
