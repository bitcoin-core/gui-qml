// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDIMPORTMODEL_H
#define BITCOIN_QML_MODELS_SENDIMPORTMODEL_H

#include <qml/models/sendimportports.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

/** The Send page's payment request and PSBT imports: one current import,
 * its status, the payment request offered from the clipboard, and when an
 * imported PSBT review can open. The page binds to it and calls one action
 * per user event; a new import, a hidden page, another wallet or recipient,
 * or a user edit of the draft cancels the current import. */
class SendImportModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ImportTarget* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString clipboardText READ clipboardText WRITE setClipboardText NOTIFY clipboardTextChanged)
    Q_PROPERTY(ImportOperation* operation READ operation NOTIFY changed)
    Q_PROPERTY(ImportOperation* clipboardCheck READ clipboardCheck NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool canAccept READ canAccept NOTIFY changed)
    Q_PROPERTY(quint64 context READ context NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool clipboardOffered READ clipboardOffered NOTIFY changed)
    Q_PROPERTY(QString offeredText READ offeredText NOTIFY changed)

public:
    enum class Source { Clipboard, ManualEntry, File, DragAndDrop, Desktop };
    Q_ENUM(Source)

    explicit SendImportModel(QObject* parent = nullptr);

    ImportTarget* target() const { return m_target; }
    void setTarget(ImportTarget* target);
    bool active() const { return m_active; }
    void setActive(bool active);
    QString clipboardText() const { return m_clipboard_text; }
    void setClipboardText(const QString& text);

    ImportOperation* operation() const { return m_operation; }
    ImportOperation* clipboardCheck() const { return m_clipboard_check; }
    bool pending() const { return m_operation && m_operation->pending(); }
    bool confirmationRequired() const { return m_operation && m_operation->promptToken() != 0; }
    //! Whether an import made elsewhere can be accepted now.
    bool canAccept() const;
    //! Changes with the wallet, recipient, visibility or availability.
    quint64 context() const { return m_context; }
    QString status() const;
    bool statusIsError() const;
    QString message() const;
    bool clipboardOffered() const;
    QString offeredText() const { return clipboardOffered() ? m_offered_text : QString{}; }

    //! A payment URI pasted into a field of \p recipient (null: the current one).
    //! That field's own value is not a conflict when the request replaces it.
    Q_INVOKABLE void importPaste(int field, const QString& text, QObject* recipient = nullptr);
    Q_INVOKABLE void importText(const QString& text, SendImportModel::Source source);
    Q_INVOKABLE void importFile(const QString& path);
    Q_INVOKABLE void importDrop(int urlCount, const QUrl& firstUrl, const QString& text);
    //! Starts a PSBT import whose file the user chooses next. Returns false
    //! when imports are unavailable.
    Q_INVOKABLE bool beginFileChoice();
    //! The file chosen for \p choice, which must still be the current import.
    Q_INVOKABLE void chooseFile(ImportOperation* choice, const QUrl& file);
    //! Takes over an import inspected elsewhere, such as a desktop intent,
    //! if the context is still \p context. Otherwise cancels it.
    Q_INVOKABLE bool acceptImport(ImportOperation* operation, quint64 context);
    //! Answers the replacement prompt.
    Q_INVOKABLE void replace();
    Q_INVOKABLE void cancel();
    //! The user edited the draft: cancel an import that is not writing it.
    Q_INVOKABLE void draftChanged();
    Q_INVOKABLE void clearStatus();

Q_SIGNALS:
    void targetChanged();
    void activeChanged();
    void clipboardTextChanged();
    void changed();
    //! The imported PSBT review is ready to open, once per result.
    void reviewReady();

private:
    void start(ImportOperation* operation, Source source, bool use, bool report_unavailable);
    void cancelImport();
    void cancelAll();
    void checkClipboard();
    void presentReview();
    void watch(ImportOperation* operation);

    QPointer<ImportTarget> m_target;
    QPointer<ImportOperation> m_operation;
    QPointer<ImportOperation> m_clipboard_check;
    Source m_source{Source::Clipboard};
    QString m_admission_error;
    QString m_presented_result;
    QString m_clipboard_text;
    QString m_offered_text;
    quint64 m_context{0};
    bool m_active{false};
};

#endif // BITCOIN_QML_MODELS_SENDIMPORTMODEL_H
