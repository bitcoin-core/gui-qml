// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MONOSPACEOUTPUTMODEL_H
#define BITCOIN_QML_MODELS_MONOSPACEOUTPUTMODEL_H

#include <qml/execution.h>
#include <qml/presentationservices.h>
#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <functional>

/** Bounded presentation adapter. Model roles are captured in small GUI turns;
 * text conversion, line/chunk projection and literal search run on Utility.
 * Worker notices contain no GUI payload and never dereference a QObject. */
class MonospaceOutputModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged)
    Q_PROPERTY(QString contentRole READ contentRole WRITE setContentRole NOTIFY sourceModelChanged)
    Q_PROPERTY(QString leftRole READ leftRole WRITE setLeftRole NOTIFY sourceModelChanged)
    Q_PROPERTY(QString rightRole READ rightRole WRITE setRightRole NOTIFY sourceModelChanged)
    Q_PROPERTY(QString categoryRole READ categoryRole WRITE setCategoryRole NOTIFY sourceModelChanged)
    Q_PROPERTY(bool richText READ richText WRITE setRichText NOTIFY sourceModelChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchChanged)
    Q_PROPERTY(int searchResultCount READ searchResultCount NOTIFY searchChanged)
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchChanged)
    Q_PROPERTY(bool searchLimited READ searchLimited NOTIFY searchChanged)
    Q_PROPERTY(int currentSearchResultIndex READ currentSearchResultIndex NOTIFY currentMatchChanged)
    Q_PROPERTY(int currentSearchRow READ currentSearchRow NOTIFY currentMatchChanged)
    Q_PROPERTY(int currentSearchStart READ currentSearchStart NOTIFY currentMatchChanged)
    Q_PROPERTY(int currentSearchEnd READ currentSearchEnd NOTIFY currentMatchChanged)
    Q_PROPERTY(PresentationServices* services READ services WRITE setServices NOTIFY servicesChanged)
public:
    // retire is enqueue-only value cleanup; false keeps values for retry and
    // never deletes them on the GUI thread.
    using Services = PresentationServices::Ports;
    explicit MonospaceOutputModel(QObject* parent = nullptr);
    ~MonospaceOutputModel() override;
    PresentationServices* services() const { return m_presentation; }
    /** Background work uses the first services assigned; the model closes when
     * they close. Without services, output is reported as unavailable. */
    void setServices(PresentationServices* services);
    enum Roles { ContentRole = Qt::UserRole + 1, PlainContentRole, LeftRole, RightRole, CategoryRole };
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QAbstractItemModel* sourceModel() const { return m_source; }
    void setSourceModel(QAbstractItemModel* source);
    QString contentRole() const { return m_content_role; }
    QString leftRole() const { return m_left_role; }
    QString rightRole() const { return m_right_role; }
    QString categoryRole() const { return m_category_role; }
    void setContentRole(const QString& role);
    void setLeftRole(const QString& role);
    void setRightRole(const QString& role);
    void setCategoryRole(const QString& role);
    bool richText() const { return m_rich; }
    void setRichText(bool rich);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool busy() const { return m_busy; }
    bool truncated() const { return m_truncated; }
    QString error() const { return m_error; }
    QString searchText() const { return m_search_text; }
    void setSearchText(const QString& text);
    int searchResultCount() const;
    bool searchBusy() const { return m_search_busy; }
    bool searchLimited() const { return m_search_limited; }
    int currentSearchResultIndex() const { return m_match_index; }
    int currentSearchRow() const;
    int currentSearchStart() const;
    int currentSearchEnd() const;
    Q_INVOKABLE void copyAll();
    Q_INVOKABLE void nextSearchResult();
    Q_INVOKABLE void previousSearchResult();
    void close();
Q_SIGNALS:
    void sourceModelChanged();
    void activeChanged();
    void servicesChanged();
    void countChanged();
    void changed();
    void searchChanged();
    void currentMatchChanged();
    void copyFinished(bool copied);
private:
    struct Row { QString content, plain, left, right; int category{-1}; bool continuation{false}; };
    struct Match { int row, start, end; };
    using Rows = QVector<Row>;
    using Matches = QVector<Match>;
    struct Mailbox;
    struct Projection;
    struct Search;
    void refresh();
    void startCapture();
    void captureNext(const std::shared_ptr<Projection>& projection);
    void publish();
    void search(bool reset);
    void stepSearch(const std::shared_ptr<Search>& search);
    void retire(std::shared_ptr<const void> value);
    void retryCleanup();
    void setMatchIndex(int index);
    void usePorts(Services ports);
    QPointer<PresentationServices> m_presentation;
    Services m_services;
    std::shared_ptr<Mailbox> m_mailbox;
    QPointer<QAbstractItemModel> m_source;
    QString m_content_role{QStringLiteral("content")}, m_left_role, m_right_role, m_category_role;
    QTimer m_refresh_timer;
    quint64 m_generation{0}, m_source_revision{0}, m_search_revision{0};
    bool m_rich{false}, m_active{true}, m_closed{false}, m_busy{false}, m_truncated{false};
    bool m_search_busy{false}, m_search_limited{false};
    bool m_waiting_retirement{false}, m_retry_scheduled{false};
    QString m_error, m_search_text;
    int m_match_index{-1};
    std::shared_ptr<Projection> m_projection;
    std::shared_ptr<Search> m_search;
    std::shared_ptr<const Rows> m_rows;
    std::shared_ptr<const Matches> m_matches;
};

#endif // BITCOIN_QML_MODELS_MONOSPACEOUTPUTMODEL_H
