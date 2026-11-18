// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PERSISTENTEDITOR_H
#define BITCOIN_QML_PERSISTENTEDITOR_H

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace persistentedit {
enum class Phase { Loading, Editing, Checking, Saving, Conflict, Uncertain, Retiring, Closed };
enum class Outcome { Saved, Rejected, Failed, Unknown, Partial };
enum class CloseChoice { Save, Discard, KeepEditing };
struct Identity {
    std::string editor, entity;
    bool operator==(const Identity&) const = default;
};

/** Pure per-editor intent/merge decisions. The consumer owns source identity,
 * validation, persistence and readback on its existing background lanes. This
 * reducer neither reads Core nor supplies Core/RPC conditional-write semantics.
 */
template <typename Value, size_t Fields>
class Editor
{
public:
    using Values = std::array<Value, Fields>;
    using Mask = std::bitset<Fields>;
    struct Attempt {
        Identity identity;
        uint64_t token, base_revision, edit_revision;
        Values base, values;
        Mask dirty;
        std::array<uint64_t, Fields> edits;
        bool operator==(const Attempt&) const = default;
    };

    struct Observation {
        Values values;
        uint64_t revision;
        // Only a physically post-write read may identify its original attempt.
        uint64_t after_attempt{0};
    };
    struct Receipt {
        Attempt attempt;
        Outcome outcome;
        // Confirmed writes, including partial writes before an exception.
        Mask persisted;
        Values values;
        std::optional<Observation> observed;
    };
    explicit Editor(Identity identity = {}) : m_identity(std::move(identity)) {}
    const Identity& identity() const { return m_identity; }
    Phase phase() const { return m_phase; }
    const Values& base() const { return m_base; }
    const Values& draft() const { return m_draft; }
    const Mask& conflicts() const { return m_conflicts; }
    const Mask& dirtyFields() const { return m_dirty; }
    bool dirty() const { return m_dirty.any(); }
    bool busy() const { return m_phase == Phase::Checking || m_phase == Phase::Saving || m_phase == Phase::Retiring; }
    bool closeQuestion() const { return m_close_question; }
    uint64_t closeToken() const { return m_close_token; }
    bool canSave() const { return m_phase == Phase::Editing && dirty() && m_conflicts.none() && !m_close_question && m_token != UINT64_MAX; }
    bool canDiscard() const { return !busy() && m_phase != Phase::Loading && m_phase != Phase::Closed && m_phase != Phase::Uncertain; }
    uint64_t editRevision() const { return m_edit_revision; }
    const std::optional<Receipt>& result() const { return m_result; }
    uint64_t baseRevision() const { return m_base_revision; }

    bool observe(Values values, uint64_t revision)
    {
        if (m_phase == Phase::Closed || m_phase == Phase::Retiring || revision <= m_base_revision) return false;
        const bool loading = m_phase == Phase::Loading;
        for (size_t i = 0; i < Fields; ++i) {
            const bool changed = values[i] != m_base[i];
            if (loading || !m_dirty[i]) m_draft[i] = values[i];
            else if (m_draft[i] == values[i]) m_conflicts[i] = false;
            else if (changed) m_conflicts[i] = true;
            m_base[i] = std::move(values[i]);
            m_dirty[i] = m_draft[i] != m_base[i];
            if (!m_dirty[i]) m_conflicts[i] = false;
        }
        m_base_revision = revision;
        if (loading || m_phase == Phase::Editing || m_phase == Phase::Conflict) settle();
        refreshPrompt();
        return true;
    }

    bool edit(size_t field, Value value)
    {
        if (field >= Fields || m_phase == Phase::Loading || m_phase == Phase::Closed || m_phase == Phase::Retiring || m_edit_revision == UINT64_MAX) return false;
        if (m_draft[field] == value) return true;
        m_draft[field] = std::move(value);
        m_edits[field] = ++m_edit_revision;
        m_dirty[field] = m_draft[field] != m_base[field];
        if (!m_dirty[field]) m_conflicts[field] = false;
        if (m_phase == Phase::Editing || m_phase == Phase::Conflict) settle();
        refreshPrompt();
        return true;
    }

    bool resolve(size_t field, bool keep_edit)
    {
        if (field >= Fields || !m_conflicts[field] || busy() || m_phase == Phase::Uncertain || m_phase == Phase::Closed || m_edit_revision == UINT64_MAX) return false;
        if (!keep_edit) m_draft[field] = m_base[field];
        m_conflicts[field] = false;
        m_dirty[field] = m_draft[field] != m_base[field];
        m_edits[field] = ++m_edit_revision;
        settle(); refreshPrompt();
        return true;
    }

    std::optional<Attempt> check()
    {
        if (!canSave()) return {};
        m_active = Attempt{m_identity, ++m_token, m_base_revision, m_edit_revision, m_base, m_draft, m_dirty, m_edits};
        m_phase = Phase::Checking;
        return m_active;
    }

    bool validated(uint64_t token, bool valid)
    {
        if (!m_active || token != m_active->token || m_phase != Phase::Checking) return false;
        if (!valid || m_active->edits != m_edits || m_conflicts.any()) {
            m_result = Receipt{*m_active, Outcome::Rejected, {}, {}, {}};
            m_active.reset(); m_close_after_save = false;
            settle();
            return false;
        }
        // Validation may observe a newer equal base without changing intent.
        m_active->base = m_base;
        m_active->base_revision = m_base_revision;
        m_phase = Phase::Saving;
        return true;
    }

    std::optional<Attempt> active() const { return m_active; }

    bool finished(Receipt receipt)
    {
        if (!m_active || receipt.attempt.identity != m_identity || receipt.attempt.token != m_active->token ||
            (m_phase != Phase::Saving && m_phase != Phase::Retiring && m_phase != Phase::Uncertain && m_phase != Phase::Closed)) return false;
        if (receipt.attempt != *m_active) return false;
        if (receipt.observed && receipt.observed->after_attempt != receipt.attempt.token) return false;
        const bool retiring = m_phase == Phase::Retiring || m_phase == Phase::Closed;
        const auto attempt = *m_active;
        // Read revision order is not write causality. Confirmed physical writes
        // supersede any pre-write read, even a numerically newer observation.
        for (size_t i{0}; i < Fields; ++i) if (receipt.persisted[i]) m_base[i] = receipt.values[i];
        if (receipt.observed) {
            m_base = receipt.observed->values;
            m_base_revision = std::max(m_base_revision, receipt.observed->revision);
        }
        for (size_t i{0}; i < Fields; ++i) {
            if (receipt.persisted[i] && m_edits[i] == attempt.edits[i]) m_draft[i] = m_base[i];
            m_dirty[i] = m_draft[i] != m_base[i];
            m_conflicts[i] = m_dirty[i] && m_base[i] != attempt.base[i] && (!receipt.persisted[i] || m_base[i] != receipt.values[i]);
        }
        m_result = std::move(receipt);
        if (m_result->outcome != Outcome::Unknown) m_active.reset();
        if (retiring) m_phase = Phase::Closed;
        else if (m_result->outcome == Outcome::Unknown) m_phase = Phase::Uncertain;
        else if (m_close_after_save && !dirty() && m_result->outcome == Outcome::Saved) m_phase = Phase::Closed;
        else { m_close_after_save = false; settle(); }
        return true;
    }
    // Matching readback cannot establish an unknown write's durability. The
    // adapter must query the retained actual outcome, never retry the mutation.
    bool reconciled(Receipt receipt)
    {
        if ((m_phase != Phase::Uncertain && m_phase != Phase::Closed) || !m_active) return false;
        return finished(std::move(receipt));
    }
    bool discard()
    {
        if (!canDiscard() || m_edit_revision > UINT64_MAX - Fields) return false;
        m_draft = m_base; m_dirty.reset(); m_conflicts.reset();
        for (auto& edit : m_edits) edit = ++m_edit_revision;
        m_close_question = false; m_close_after_save = false; settle();
        return true;
    }
    bool requestClose()
    {
        if (m_phase == Phase::Closed) return true;
        if (busy()) return false;
        if (m_phase == Phase::Loading || m_phase == Phase::Uncertain || !dirty()) { m_phase = Phase::Closed; return true; }
        if (!m_close_question) {
            if (m_close_token == UINT64_MAX) return false;
            m_close_question = true; refreshPrompt();
        }
        return false;
    }
    bool answerClose(uint64_t token, CloseChoice choice)
    {
        if (!m_close_question || token != m_close_token || busy() || m_phase == Phase::Closed ||
            m_prompt_base != m_base_revision || m_prompt_edit != m_edit_revision) return false;
        if (choice == CloseChoice::Discard) {
            if (!discard()) return false;
            m_phase = Phase::Closed;
        } else {
            m_close_question = false;
            if (choice == CloseChoice::Save) m_close_after_save = true;
        }
        return true;
    }
    void retire()
    {
        m_close_question = false; m_close_after_save = false;
        if (m_phase == Phase::Closed || m_phase == Phase::Retiring) return;
        if (m_phase == Phase::Saving) m_phase = Phase::Retiring;
        else {
            if (m_phase != Phase::Uncertain) m_active.reset();
            m_phase = Phase::Closed;
        }
    }

private:
    void refreshPrompt()
    {
        if (!m_close_question) return;
        if (m_close_token == UINT64_MAX) { m_close_question = false; return; }
        ++m_close_token; m_prompt_base = m_base_revision; m_prompt_edit = m_edit_revision;
    }
    void settle() { m_phase = m_conflicts.any() ? Phase::Conflict : Phase::Editing; }
    const Identity m_identity;
    Phase m_phase{Phase::Loading};
    Values m_base{}, m_draft{};
    Mask m_dirty, m_conflicts;
    std::array<uint64_t, Fields> m_edits{};
    uint64_t m_base_revision{0}, m_token{0}, m_edit_revision{0}, m_close_token{0}, m_prompt_base{0}, m_prompt_edit{0};
    bool m_close_question{false}, m_close_after_save{false};
    std::optional<Attempt> m_active;
    std::optional<Receipt> m_result;
};
} // namespace persistentedit
#endif // BITCOIN_QML_PERSISTENTEDITOR_H
