// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_WIDGETLAYOUTMODEL_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_WIDGETLAYOUTMODEL_H

#include <QAbstractListModel>
#include <QRect>
#include <QMap>
#include <QVariantList>

#include <optional>

/** Persistent cell coordinates, independent of pixel geometry and widget content. */
class WidgetLayoutModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantList catalog READ catalog WRITE setCatalog NOTIFY catalogChanged)
    Q_PROPERTY(QString storageKey READ storageKey WRITE setStorageKey NOTIFY storageKeyChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(int rows READ rows NOTIFY columnsChanged)
    Q_PROPERTY(int unplacedCount READ unplacedCount NOTIFY layoutChanged)
    Q_PROPERTY(int count READ count NOTIFY layoutChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeIdChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY persistenceErrorChanged)

public:
    enum Role { WidgetIdRole = Qt::UserRole + 1, TitleRole, SourceRole, ColumnRole, RowRole, ColumnSpanRole, RowSpanRole, SizesRole, InstanceIdRole };
    explicit WidgetLayoutModel(QObject* parent = nullptr, const QString& settings_file = {});
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList catalog() const { return m_catalog; }
    void setCatalog(const QVariantList& catalog);
    QString storageKey() const { return m_storage_key; }
    void setStorageKey(const QString& key);
    int columns() const { return m_columns; }
    void setColumns(int columns);
    int rows() const { return 9 - m_columns; }
    int unplacedCount() const;
    int count() const { return m_entries.size(); }
    QString activeId() const { return m_active_id; }
    QString persistenceError() const { return m_persistence_error; }

    Q_INVOKABLE void restore();
    // Catalog IDs identify widget types; editing methods below use instance IDs.
    Q_INVOKABLE bool contains(const QString& id) const;
    Q_INVOKABLE bool addWidget(const QString& id, int size_index = -1);
    Q_INVOKABLE bool removeWidget(const QString& id);
    Q_INVOKABLE bool beginInteraction(const QString& id);
    Q_INVOKABLE void previewMove(int column, int row);
    Q_INVOKABLE void previewResize(double columns, double rows);
    Q_INVOKABLE void commitInteraction();
    Q_INVOKABLE void cancelInteraction();
    Q_INVOKABLE QVariantMap geometry(const QString& id) const;

Q_SIGNALS:
    void catalogChanged();
    void storageKeyChanged();
    void columnsChanged();
    void layoutChanged();
    void activeIdChanged();
    void persistenceErrorChanged();

private:
    struct Entry {
        QString id;
        QRect rect;
        QString widget_id;
        bool operator==(const Entry&) const = default;
    };
    QVariantMap definition(const QString& id) const;
    QList<QSize> sizes(const QString& id) const;
    QSize defaultSize(const QString& id) const;
    QRect nearestFree(QRect rect, const QList<Entry>& occupied) const;
    std::optional<QList<Entry>> arrange(const QList<Entry>& entries, const QString& pinned_id = {}) const;
    QList<Entry> reflow(const QList<Entry>& entries) const;
    void preview(QRect rect);
    void publish(const QList<Entry>& entries);
    void save(bool user_edit = true);
    void applyArrangement();
    int indexOf(const QString& id) const;

    QVariantList m_catalog;
    QString m_storage_key{"dashboard/layout"};
    QString m_settings_file;
    int m_columns{6};
    QList<Entry> m_entries;
    QList<Entry> m_saved;
    QMap<int, QList<Entry>> m_arrangements;
    bool m_restored{false};
    bool m_persist_reflow{false};
    QList<Entry> m_before;
    QString m_active_id;
    QString m_persistence_error;
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_WIDGETLAYOUTMODEL_H
