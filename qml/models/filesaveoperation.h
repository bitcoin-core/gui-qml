// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FILESAVEOPERATION_H
#define BITCOIN_QML_MODELS_FILESAVEOPERATION_H

#include <QObject>
#include <QString>

/** Saving one file the user asked for: a wallet backup, a PSBT, the Activity
 * CSV or a request's QR image. Each kind supplies its own content and
 * messages; pages bind only to this interface. A destination that exists or
 * was renamed waits for confirm() or cancel() before anything is written. */
class FileSaveOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool replacing READ replacing NOTIFY changed)
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(bool saved READ saved NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    //! The save has not finished; this includes waiting for confirmation.
    virtual bool busy() const = 0;
    //! The destination needs the user's confirmation before it is written.
    virtual bool confirmationRequired() const = 0;
    //! The confirmed destination replaces an existing file.
    virtual bool replacing() const = 0;
    //! The destination after the file name was completed.
    virtual QString finalPath() const = 0;
    //! The file was written at finalPath.
    virtual bool saved() const = 0;
    //! Why the save did not finish, or empty.
    virtual QString error() const = 0;
    //! Writes the file at the destination that waits for confirmation.
    Q_INVOKABLE virtual bool confirm() = 0;
    //! Ends the save without writing; no effect once writing has started.
    Q_INVOKABLE virtual bool cancel() = 0;
    //! Ends the save if its write has not been admitted, at any step before
    //! it, and returns true. False means the write runs or ran; its outcome
    //! stays. A page that is no longer shown calls this.
    virtual bool retireBeforeWrite() { return cancel(); }
Q_SIGNALS:
    void changed();
protected:
    using QObject::QObject;
};

#endif // BITCOIN_QML_MODELS_FILESAVEOPERATION_H
