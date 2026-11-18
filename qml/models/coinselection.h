// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_COINSELECTION_H
#define BITCOIN_QML_MODELS_COINSELECTION_H

#include <wallet/coincontrol.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace coinselection {

// Automatic and Manual are user intent. In particular, an empty Manual
// selection must remain empty instead of being interpreted as a request to
// let Core choose wallet inputs.
enum class InputMode { Automatic, Manual };

inline void ApplyInputPolicy(wallet::CCoinControl& control, const InputMode mode)
{
    control.m_allow_other_inputs = mode == InputMode::Automatic;

    // Core applies this filter only when the wallet has avoid_reuse enabled.
    // Setting it on every app-generated control therefore preserves ordinary
    // wallet behavior while honoring the wallet's opt-in policy.
    control.m_avoid_address_reuse = true;
}

// Deterministic intent reducer for the temporary coin-selection editor. It
// owns no wallet or GUI objects; the caller supplies the value-retention
// boundary used by an opening session.
class Session
{
public:
    using Selection = std::set<COutPoint>;
    using Snapshot = std::shared_ptr<const Selection>;
    using Retain = std::function<Snapshot(Selection)>;
    enum class Intent { Select, Unselect, Toggle, Replace, Begin, Apply, Cancel, ChooseAutomatic, ChooseManual };
    struct Transition { Snapshot selection; std::uint64_t revision{0}; InputMode mode{InputMode::Automatic}; bool failed{false}; bool changed{false}; };
    struct MarkChange { Snapshot marks; std::uint64_t revision{0}; bool failed{false}; };

    explicit Session(Retain retain) : m_retain{std::move(retain)}
    {
        m_empty = m_retain({});
        m_selection = m_marks = m_empty;
    }

    bool ready() const { return bool(m_empty); }
    Snapshot selection() const { return m_selection; }
    Snapshot marks() const { return m_marks; }
    std::uint64_t revision() const { return m_revision; }
    std::uint64_t marksRevision() const { return m_marks_revision; }
    std::uint64_t catalogVersion() const { return m_catalog_version; }
    InputMode inputMode() const { return m_mode; }
    const Selection& missing() const { return m_missing; }
    bool editing() const { return bool(m_baseline); }

    Transition apply(Intent intent, std::optional<COutPoint> outpoint = {}, Snapshot replacement = {})
    {
        if (!ready()) return result(true, false);
        if (intent == Intent::Begin) {
            if (!m_baseline) m_baseline = m_selection;
            return result(false, false);
        }
        if (intent == Intent::Apply) {
            m_baseline.reset();
            if (!m_catalog_known) return result(false, false);
            Selection retained;
            std::set_intersection(m_selection->begin(), m_selection->end(), m_catalog.begin(), m_catalog.end(), std::inserter(retained, retained.end()));
            return replaceSelection(std::move(retained));
        }
        if (intent == Intent::Cancel) {
            if (!m_baseline) return result(false, false);
            auto restored = m_baseline;
            m_baseline.reset();
            if (restored == m_selection) return result(false, false);
            m_selection = std::move(restored);
            updateMissing();
            ++m_revision;
            return result(false, true);
        }
        if (intent == Intent::ChooseAutomatic || intent == Intent::ChooseManual) {
            const auto mode = intent == Intent::ChooseManual ? InputMode::Manual : InputMode::Automatic;
            if (m_mode == mode && (mode == InputMode::Manual || m_selection->empty())) return result(false, false);
            if (mode == InputMode::Automatic && !m_selection->empty()) {
                auto empty = m_retain({});
                if (!empty) return result(true, false);
                m_selection = std::move(empty);
                updateMissing();
            }
            m_mode = mode;
            m_baseline.reset();
            ++m_revision;
            return result(false, true);
        }

        Selection next;
        if (intent == Intent::Replace) {
            if (replacement) next = *replacement;
        } else {
            next = *m_selection;
            if (outpoint) {
                if (intent == Intent::Unselect || (intent == Intent::Toggle && next.contains(*outpoint))) next.erase(*outpoint);
                else next.insert(*outpoint);
            }
        }
        if (next == *m_selection) return result(false, false);
        if (auto retained = m_retain(std::move(next))) {
            m_selection = std::move(retained);
            ++m_revision;
            return result(false, true);
        }
        return result(true, false);
    }

    MarkChange toggleMark(std::optional<COutPoint> outpoint = {})
    {
        if (!ready()) return {m_marks, m_marks_revision, true};
        Selection next = outpoint ? *m_marks : Selection{};
        if (outpoint) {
            if (next.contains(*outpoint)) next.erase(*outpoint);
            else next.insert(*outpoint);
        }
        if (next == *m_marks) return {m_marks, m_marks_revision, false};
        auto retained = m_retain(std::move(next));
        if (!retained) return {m_marks, m_marks_revision, true};
        m_marks = std::move(retained);
        ++m_marks_revision;
        return {m_marks, m_marks_revision, false};
    }

    bool reconcileCatalog(const Selection& catalog)
    {
        if (m_catalog_known && catalog == m_catalog) return false;
        m_catalog = catalog;
        m_catalog_known = true;
        ++m_catalog_version;
        updateMissing();
        return true;
    }

    // Bulk marks name outputs of the current catalog only. A spent output
    // must not keep a stale count or become a later Lock/Unlock target.
    bool retainMarks(const Selection& catalog)
    {
        Selection next;
        std::set_intersection(m_marks->begin(), m_marks->end(), catalog.begin(), catalog.end(), std::inserter(next, next.end()));
        if (next == *m_marks) return true;
        auto retained = m_retain(std::move(next));
        if (!retained) return false;
        m_marks = std::move(retained);
        ++m_marks_revision;
        return true;
    }

    bool applyLockResults(const Selection& successfully_locked, const Selection& restore_marks)
    {
        const auto remove_locked = [&](const Snapshot& source) -> Snapshot {
            if (!source || successfully_locked.empty()) return source;
            Selection next = *source;
            for (const auto& output : successfully_locked) next.erase(output);
            return next == *source ? source : m_retain(std::move(next));
        };
        auto selected = remove_locked(m_selection);
        auto baseline = m_baseline == m_selection ? selected : remove_locked(m_baseline);
        if (!selected || (m_baseline && !baseline)) return false;
        auto marks = m_marks;
        if (!restore_marks.empty()) {
            Selection next = *m_marks;
            next.insert(restore_marks.begin(), restore_marks.end());
            if (next != *m_marks) {
                marks = m_retain(std::move(next));
                if (!marks) return false;
            }
        }
        // Reserve every changed value before altering the editor. A later
        // Cancel restores the committed choice minus known successful locks;
        // it cannot resurrect an input that this operation already locked.
        if (selected != m_selection) {
            m_selection = std::move(selected); updateMissing(); ++m_revision;
        }
        m_baseline = std::move(baseline);
        if (marks != m_marks) { m_marks = std::move(marks); ++m_marks_revision; }
        // Success means that the actual results were reconciled, including a
        // no-op for an input that was never selected or marked here.
        return true;
    }

private:
    Transition result(bool failed, bool changed) const { return {m_selection, m_revision, m_mode, failed, changed}; }
    Transition replaceSelection(Selection next)
    {
        if (next == *m_selection) return result(false, false);
        auto retained = m_retain(std::move(next));
        if (!retained) return result(true, false);
        m_selection = std::move(retained);
        updateMissing();
        ++m_revision;
        return result(false, true);
    }
    bool updateMissing()
    {
        Selection missing;
        if (m_catalog_known) std::set_difference(m_selection->begin(), m_selection->end(), m_catalog.begin(), m_catalog.end(), std::inserter(missing, missing.end()));
        const bool changed = missing != m_missing;
        m_missing = std::move(missing);
        return changed;
    }

    Retain m_retain;
    Snapshot m_empty, m_selection, m_baseline, m_marks;
    Selection m_catalog, m_missing;
    std::uint64_t m_revision{0}, m_marks_revision{0}, m_catalog_version{0};
    InputMode m_mode{InputMode::Automatic};
    bool m_catalog_known{false};
};

} // namespace coinselection

#endif // BITCOIN_QML_MODELS_COINSELECTION_H
