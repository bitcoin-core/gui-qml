// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/walletrequesterror.h>
#include <QCoreApplication>

QString walletrequests::ErrorText(Error error, bool request_saved)
{
    switch (error) {
    case Error::None: return {};
    case Error::Loading: return QCoreApplication::translate("ReceiveRequestOperation", "Payment requests are still loading.");
    case Error::Checking: return QCoreApplication::translate("ReceiveRequestOperation", "Payments are still being checked. Try again when checking finishes.");
    case Error::Invalid: return QCoreApplication::translate("ReceiveRequestOperation", "Check the payment request fields.");
    case Error::Missing: return QCoreApplication::translate("ReceiveRequestOperation", "This payment request is no longer available.");
    case Error::Changed: return request_saved
        ? QCoreApplication::translate("ReceiveRequestOperation", "The request was saved, but its address note changed. Review the current address note before retrying.")
        : QCoreApplication::translate("ReceiveRequestOperation", "This payment request changed. Reopen it before saving.");
    case Error::Paid: return QCoreApplication::translate("ReceiveRequestOperation", "This address has received a payment. Its public request fields cannot be changed.");
    case Error::IdExhausted: return QCoreApplication::translate("ReceiveRequestOperation", "Another payment request cannot be created in this wallet.");
    case Error::NeedsUnlock: return QCoreApplication::translate("ReceiveRequestOperation", "Unlock the wallet to create a receiving address.");
    case Error::Rejected: return QCoreApplication::translate("ReceiveRequestOperation", "The operation could not be scheduled. Try again.");
    case Error::Closed: return QCoreApplication::translate("ReceiveRequestOperation", "This wallet opening has closed.");
    case Error::Persistence: return QCoreApplication::translate("ReceiveRequestOperation", "The payment request could not be saved. Try again.");
    case Error::LabelPersistence: return request_saved
        ? QCoreApplication::translate("ReceiveRequestOperation", "The request was saved, but its address note could not be saved.")
        : QCoreApplication::translate("ReceiveRequestOperation", "The address note could not be saved.");
    case Error::Partial: return QCoreApplication::translate("ReceiveRequestOperation", "Only part of the change was saved. Reload before retrying.");
    case Error::Uncertain: return QCoreApplication::translate("ReceiveRequestOperation", "The result could not be confirmed. Reload before retrying.");
    case Error::Unsupported: return QCoreApplication::translate("ReceiveRequestOperation", "This operation is not supported by the wallet interface.");
    case Error::AlreadySaved: return QCoreApplication::translate("ReceiveRequestOperation", "This receiving address was already used for a request. Create a new receiving address.");
    case Error::IncorrectPassphrase: return QCoreApplication::translate("ReceiveRequestOperation", "The wallet password you entered was incorrect.");
    case Error::AddressCreation: return QCoreApplication::translate("ReceiveRequestOperation", "A receiving address could not be generated. Please try again.");
    case Error::ReservationPersistence: return QCoreApplication::translate("ReceiveRequestOperation", "The receiving-address reservation could not be saved. Retry before creating a request.");
    case Error::PreferencePersistence: return QCoreApplication::translate("ReceiveRequestOperation", "The address is ready, but saving its receiving preference could not be confirmed.");
    case Error::TypeUnavailable: return QCoreApplication::translate("ReceiveRequestOperation", "This wallet cannot generate this address type. Choose another type.");
    case Error::LockedAttemptFailed: return QCoreApplication::translate("ReceiveRequestOperation", "This address attempt failed while the wallet was locked. It may need unlocking or may be unavailable for this address type. Unlock and retry the original attempt, or cancel.");
    }
    return {};
}
