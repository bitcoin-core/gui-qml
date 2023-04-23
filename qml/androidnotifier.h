#ifndef BITCOIN_QML_ANDROIDNOTIFIER_H
#define BITCOIN_QML_ANDROIDNOTIFIER_H

#include <qml/models/nodemodel.h>

#include <QObject>
#include <jni.h>

class AndroidNotifier : public QObject
{
    Q_OBJECT

public:
    explicit AndroidNotifier(const NodeModel & node_model, QObject * parent = nullptr);

public Q_SLOTS:
    void onBlockTipHeightChanged();
    void onNumOutboundPeersChanged();
    void onVerificationProgressChanged();
    void onPausedChanged();

private:
    const NodeModel & m_node_model;
};

#endif // BITCOIN_QML_ANDROIDNOTIFIER_H
