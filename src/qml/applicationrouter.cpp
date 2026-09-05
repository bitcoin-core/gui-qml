// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applicationrouter.h>

#include <QCoreApplication>

#include <algorithm>
#include <utility>

ApplicationRouter::ApplicationRouter(QObject* parent) : QObject(parent) {}

const ApplicationRouter::Destination* ApplicationRouter::find(const QString& id) const
{
    for (const auto& destination : m_destinations) {
        if (destination.id == id) return &destination;
    }
    return nullptr;
}

bool ApplicationRouter::registerDestination(Destination destination)
{
    if (destination.id.isEmpty() || destination.source.isEmpty() || find(destination.id)) return false;
    m_destinations.push_back(std::move(destination));
    Q_EMIT destinationsChanged();
    return true;
}

void ApplicationRouter::setDestinationEnabled(const QString& id, bool enabled)
{
    for (auto& destination : m_destinations) {
        if (destination.id != id || destination.enabled == enabled) continue;
        destination.enabled = enabled;
        if (!enabled && !m_shutting_down) {
            m_history.erase(std::remove_if(m_history.begin(), m_history.end(), [&id](const Entry& entry) {
                return entry.id == id;
            }), m_history.end());
        }
        Q_EMIT destinationsChanged();
        Q_EMIT currentChanged();
        Q_EMIT routeChanged();
        return;
    }
}

QString ApplicationRouter::currentRoute() const { return m_history.isEmpty() ? QString{} : m_history.back().id; }
QVariantMap ApplicationRouter::currentParameters() const { return m_history.isEmpty() ? QVariantMap{} : m_history.back().parameters; }
QUrl ApplicationRouter::currentSource() const
{
    const auto* destination = find(currentRoute());
    return destination ? destination->source : QUrl{};
}

QString ApplicationRouter::title(const Destination& destination)
{
    return QCoreApplication::translate("ApplicationRouter", destination.title.constData());
}

QString ApplicationRouter::currentTitle() const
{
    const auto* destination = find(currentRoute());
    return destination ? title(*destination) : QString{};
}

QString ApplicationRouter::currentSelection() const
{
    const auto* destination = find(currentRoute());
    return destination && !destination->selection.isEmpty() ? destination->selection : currentRoute();
}

bool ApplicationRouter::navigate(const QString& id, const QVariantMap& parameters)
{
    const auto* destination = find(id);
    if (m_shutting_down || !destination || !destination->enabled || id == QStringLiteral("shutdown")) return false;
    if (currentRoute() == id && currentParameters() == parameters) return true;
    m_history.push_back({id, parameters});
    Q_EMIT currentChanged();
    Q_EMIT routeChanged();
    return true;
}

void ApplicationRouter::back()
{
    if (!canGoBack()) return;
    m_history.pop_back();
    Q_EMIT currentChanged();
    Q_EMIT routeChanged();
}

void ApplicationRouter::beginShutdown()
{
    if (m_shutting_down) return;
    m_shutting_down = true;
    m_history.clear();
    if (find(QStringLiteral("shutdown"))) m_history.push_back({QStringLiteral("shutdown"), {}});
    Q_EMIT currentChanged();
    Q_EMIT routeChanged();
}

void ApplicationRouter::retranslate()
{
    Q_EMIT destinationsChanged();
    Q_EMIT currentChanged();
}

NavigationModel::NavigationModel(ApplicationRouter& router, QObject* parent)
    : QAbstractListModel(parent), m_router(router)
{
    connect(&router, &ApplicationRouter::destinationsChanged, this, &NavigationModel::rebuild);
    connect(&router, &ApplicationRouter::currentChanged, this, [this] {
        if (!m_rows.isEmpty()) Q_EMIT dataChanged(index(0), index(m_rows.size() - 1), {SelectedRole, EnabledRole});
    });
    rebuild();
}

void NavigationModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    for (int i = 0; i < m_router.destinations().size(); ++i) {
        if (m_router.destinations()[i].in_menu && m_router.destinations()[i].enabled) m_rows.push_back(i);
    }
    endResetModel();
}

int NavigationModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant NavigationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const auto& destination = m_router.destinations()[m_rows[index.row()]];
    switch (role) {
    case RouteRole: return destination.id;
    case LabelRole: return ApplicationRouter::title(destination);
    case SelectedRole: return m_router.currentSelection() == destination.id;
    case EnabledRole: return !m_router.shuttingDown();
    default: return {};
    }
}

QHash<int, QByteArray> NavigationModel::roleNames() const
{
    return {{RouteRole, "routeId"}, {LabelRole, "label"}, {SelectedRole, "selected"}, {EnabledRole, "available"}};
}
