// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/settingsnavigationcontroller.h>

#include <qml/models/settingseditormodel.h>

SettingsNavigationController::SettingsNavigationController(QObject* parent) : QObject(parent) {}

SettingsNavigationController::~SettingsNavigationController()
{
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
        m_editor->close();
    }
}

void SettingsNavigationController::setSource(SettingsEditorSource* source)
{
    if (m_source == source) return;
    m_source = source;
    const QPointer<SettingsNavigationController> guard{this};
    Q_EMIT sourceChanged();
    if (guard && !m_editor) open();
}

bool SettingsNavigationController::navigationAllowed() const
{
    if (m_pending) return false;
    if (!m_editor) return true;
    const auto phase = m_editor->phase();
    return phase != SettingsEditorModel::Checking && phase != SettingsEditorModel::Saving && phase != SettingsEditorModel::Retiring;
}

bool SettingsNavigationController::doneEnabled() const
{
    if (!navigationAllowed()) return false;
    if (!m_editor || !m_editor->busy()) return true;
    // An editor that only loads and holds no edits does not keep settings open.
    return m_editor->phase() == SettingsEditorModel::Loading && !m_editor->dirty() && !m_editor->inputPending();
}

void SettingsNavigationController::request(const QString& section, bool done)
{
    if (!navigationAllowed()) return;
    if (!m_editor || !m_editor->dirty()) {
        Q_EMIT navigate(section, done, false);
        return;
    }
    m_section = section;
    m_done = done;
    m_pending = true;
    m_question_seen = false;
    const QPointer<SettingsNavigationController> guard{this};
    Q_EMIT pendingChanged();
    if (!guard) return;
    Q_EMIT stateChanged();
    if (guard && m_editor) m_editor->requestClose();
}

void SettingsNavigationController::open()
{
    if (m_editor) disconnect(m_editor, nullptr, this, nullptr);
    m_editor = m_source ? m_source->createSettingsEditor() : nullptr;
    if (m_editor) {
        connect(m_editor, &SettingsEditorModel::closed, this, &SettingsNavigationController::editorClosed);
        connect(m_editor, &SettingsEditorModel::changed, this, &SettingsNavigationController::editorUpdated);
        connect(m_editor, &QObject::destroyed, this, &SettingsNavigationController::editorChanged);
    }
    const QPointer<SettingsNavigationController> guard{this};
    Q_EMIT editorChanged();
    if (guard) Q_EMIT stateChanged();
}

void SettingsNavigationController::editorClosed()
{
    if (!m_pending) {
        // The owner closed the editor and deletes it next. Let it go while its
        // entries still exist, so the rows showing them are removed first.
        disconnect(m_editor, nullptr, this, nullptr);
        m_editor = nullptr;
        const QPointer<SettingsNavigationController> guard{this};
        Q_EMIT editorChanged();
        if (guard) Q_EMIT stateChanged();
        return;
    }
    m_pending = false;
    const auto section = m_section;
    const bool done = m_done;
    const QPointer<SettingsNavigationController> guard{this};
    open();
    if (!guard) return;
    Q_EMIT pendingChanged();
    if (guard) Q_EMIT navigate(section, done, true);
}

void SettingsNavigationController::editorUpdated()
{
    const QPointer<SettingsNavigationController> guard{this};
    if (m_pending && m_editor) {
        if (m_editor->closeQuestion()) {
            m_question_seen = true;
        } else if (m_question_seen && !m_editor->busy() && m_editor->phase() != SettingsEditorModel::Closed) {
            // The user kept editing: the navigation is dropped.
            m_pending = false;
            Q_EMIT pendingChanged();
            if (!guard) return;
        }
    }
    Q_EMIT stateChanged();
}
