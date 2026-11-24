// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SETTINGSNAVIGATIONCONTROLLER_H
#define BITCOIN_QML_MODELS_SETTINGSNAVIGATIONCONTROLLER_H

#include <qml/models/settingseditorfactoryinterface.h>

#include <QObject>
#include <QPointer>
#include <QString>

class SettingsEditorModel;

/** The settings view's editor and its navigation guard. Leaving a section with
 * unsaved edits asks the editor's close question first; once the editor
 * closes, a new editor opens and the view moves on. Declared by SettingsView. */
class SettingsNavigationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SettingsEditorFactoryInterface* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(SettingsEditorModel* editor READ editor NOTIFY editorChanged)
    //! A navigation waits for the editor's close question.
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    //! The section a pending navigation goes to.
    Q_PROPERTY(QString pendingSection READ pendingSection NOTIFY pendingChanged)
    Q_PROPERTY(bool navigationAllowed READ navigationAllowed NOTIFY stateChanged)
    Q_PROPERTY(bool doneEnabled READ doneEnabled NOTIFY stateChanged)
public:
    explicit SettingsNavigationController(QObject* parent = nullptr);
    ~SettingsNavigationController() override;

    SettingsEditorFactoryInterface* source() const { return m_source; }
    void setSource(SettingsEditorFactoryInterface* source);
    SettingsEditorModel* editor() const { return m_editor; }
    bool pending() const { return m_pending; }
    QString pendingSection() const { return m_pending ? m_section : QString{}; }
    bool navigationAllowed() const;
    bool doneEnabled() const;

    //! Goes to \p section, or leaves settings when \p done, after the editor's
    //! close question if it has unsaved edits.
    Q_INVOKABLE void request(const QString& section, bool done);

Q_SIGNALS:
    void sourceChanged();
    void editorChanged();
    void pendingChanged();
    void stateChanged();
    //! The view goes to \p section, or leaves settings when \p done. \p reload
    //! follows a closed editor: the section shows the new editor.
    void navigate(const QString& section, bool done, bool reload);

private:
    void open();
    void editorClosed();
    void editorUpdated();

    QPointer<SettingsEditorFactoryInterface> m_source;
    QPointer<SettingsEditorModel> m_editor;
    QString m_section;
    bool m_done{false};
    bool m_pending{false};
    bool m_question_seen{false};
};

#endif // BITCOIN_QML_MODELS_SETTINGSNAVIGATIONCONTROLLER_H
