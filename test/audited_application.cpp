// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>
#include <qml/models/nodemodel.h>
#include <test/thread_audit.h>

#include <util/translation.h>

#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

const TranslateFn G_TRANSLATION_FUN{[](const char* text) {
    return QCoreApplication::translate("bitcoin-core", text).toStdString();
}};

// The normal argument/automation contract, with checked application handles.
// Useful for Python journeys and process-level sanitizer lifecycle checks.
int main(int argc, char* argv[])
{
    std::shared_ptr<qmlintegration::ThreadAudit> audit;
    QmlApplicationHooks hooks;
    hooks.window_created = [&](interfaces::Node&, QQmlApplicationEngine& engine) {
        audit->setPhase(qmlintegration::TestPhase::Running);
        auto* model = qobject_cast<NodeModel*>(engine.rootContext()->contextProperty("nodeModel").value<QObject*>());
        QObject::connect(model, &NodeModel::requestedShutdown, &engine, [audit] {
            audit->setPhase(qmlintegration::TestPhase::Shutdown);
        });
    };
    hooks.interfaces_created = [&](std::unique_ptr<interfaces::Node>& node, std::unique_ptr<interfaces::Chain>& chain) {
        audit = std::make_shared<qmlintegration::ThreadAudit>();
        node = qmlintegration::CheckNode(std::move(node), audit);
        chain = qmlintegration::CheckChain(std::move(chain), audit);
    };
    return QmlGuiMain(argc, argv, hooks);
}
