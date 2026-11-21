// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_RECEIVETYPEREGISTRATION_H
#define BITCOIN_QML_RECEIVETYPEREGISTRATION_H
#include <qml/models/walletreceivemodel.h>
#include <QQmlEngine>

inline void RegisterReceiveTypes()
{
    constexpr auto uri = "org.bitcoincore.qt";
    qmlRegisterUncreatableType<WalletReceiveModel>(uri, 1, 0, "WalletReceiveModel", "Owned by a wallet opening");
    qmlRegisterUncreatableType<PaymentRequestEditorModel>(uri, 1, 0, "PaymentRequestEditorModel", "Owned by the receive editor factory");
    qmlRegisterUncreatableType<RequestReceiptModel>(uri, 1, 0, "RequestReceiptModel", "Owned by an original request editor");
    qmlRegisterUncreatableType<ReceiptReconciliationModel>(uri, 1, 0, "ReceiptReconciliationModel", "Owned by an opening");
    qmlRegisterUncreatableType<ReceiveReservationModel>(uri, 1, 0, "ReceiveReservationModel", "Owned by a receiving context");
    qmlRegisterUncreatableType<ReceiveAuthorizationPrompt>(uri, 1, 0, "ReceiveAuthorizationPrompt", "Owned by an original receiving intent");
    qmlRegisterUncreatableType<RequestSharingModel>(uri, 1, 0, "RequestSharingModel", "Owned by a request view");
    qmlRegisterUncreatableType<ShareOperationModel>(uri, 1, 0, "ShareOperationModel", "Owned by an original sharing action");
    qmlRegisterUncreatableType<RequestImageExportOperation>(uri, 1, 0, "RequestImageExportOperation", "Owned by an original export action");
    qmlRegisterUncreatableMetaObject(ReceiveError::staticMetaObject, uri, 1, 0, "ReceiveError", "Error value");
    qmlRegisterUncreatableMetaObject(ReceiveTypeStatus::staticMetaObject, uri, 1, 0, "ReceiveTypeStatus", "Capability value");
    qmlRegisterUncreatableMetaObject(SharingError::staticMetaObject, uri, 1, 0, "SharingError", "Error value");
    qmlRegisterUncreatableMetaObject(RequestImageExportError::staticMetaObject, uri, 1, 0, "RequestImageExportError", "Error value");
    qRegisterMetaType<RequestSaveResult>(); qRegisterMetaType<ShareResult>(); qRegisterMetaType<RequestImageExportResult>();
}
#endif // BITCOIN_QML_RECEIVETYPEREGISTRATION_H
