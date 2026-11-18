// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETREQUESTERROR_H
#define BITCOIN_QML_WALLETREQUESTERROR_H
#include <QObject>
#include <QString>

class ReceiveError {
    Q_GADGET
public:
    enum class Code { None, Loading, Checking, Invalid, Missing, Changed, Paid, IdExhausted, NeedsUnlock,
                   Rejected, Closed, Persistence, LabelPersistence, Partial, Uncertain, Unsupported, AlreadySaved,
                   IncorrectPassphrase, AddressCreation, ReservationPersistence, PreferencePersistence, LockedAttemptFailed };
    Q_ENUM(Code)
};

class ReceiveTypeStatus {
    Q_GADGET
public:
    enum class Support { Unknown, Proven };
    Q_ENUM(Support)
    // Aggregate keypool availability is not a per-type support query. A
    // positive aggregate result leaves individual current availability unknown.
    enum class Availability { Unknown, Unavailable };
    Q_ENUM(Availability)
};
namespace walletrequests {
using Error = ReceiveError::Code;
QString ErrorText(Error error, bool request_saved = false);
}
#endif // BITCOIN_QML_WALLETREQUESTERROR_H
