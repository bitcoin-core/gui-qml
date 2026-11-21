// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/runtimepromptmodel.h>
#include <node/interface_ui.h>
RuntimePromptModel::RuntimePromptModel(std::shared_ptr<const runtimeprompt::Prompt> prompt,
    std::weak_ptr<runtimeprompt::Service> service,StartupDiagnosticsModel* diagnostics,QObject* parent)
    :QObject(parent),m_descriptor(prompt->descriptor),m_message(prompt->request->summary),m_service(std::move(service)),m_diagnostics(diagnostics){}
QString RuntimePromptModel::title()const
{
    const auto style=m_descriptor.style;
    return style&CClientUIInterface::ICON_ERROR?tr("Error"):style&CClientUIInterface::ICON_WARNING?tr("Warning"):tr("Information");
}
QString RuntimePromptModel::icon()const
{
    const auto style=m_descriptor.style;
    return style&CClientUIInterface::ICON_ERROR?QStringLiteral("image://images/error"):style&CClientUIInterface::ICON_WARNING?QStringLiteral("image://images/alert-filled"):QStringLiteral("image://images/info-filled");
}
bool RuntimePromptModel::question()const
{
    const auto mask=buttons();return m_descriptor.question||(mask&&(mask&(mask-1)));
}
bool RuntimePromptModel::presented(){const auto service=m_service.lock();return service&&service->visible(id());}
bool RuntimePromptModel::answer(unsigned int button){const auto service=m_service.lock();return service&&service->answer(id(),button);}
bool RuntimePromptModel::dismiss(){const auto service=m_service.lock();return service&&service->dismiss(id());}
bool RuntimePromptModel::unavailable(){const auto service=m_service.lock();return service&&service->unavailable(id());}
