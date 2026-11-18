// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SETTINGSSESSION_H
#define BITCOIN_QML_SETTINGSSESSION_H
#include <qml/effectivesettings.h>
#include <qml/persistenteditor.h>
#include <array>
#include <bitset>
#include <optional>
namespace settingsedit {
inline constexpr size_t FIELDS{10};
struct Cell {
    effectivesettings::Value value{false};
    effectivesettings::Intent intent{effectivesettings::Intent::RemoveOverride};
    std::optional<effectivesettings::Value> remembered{};
    bool valid{true};
    QString invalid{};
    bool operator==(const Cell& other) const {
        if (intent != other.intent || valid != other.valid) return false;
        if (!valid) return invalid == other.invalid;
        return intent == effectivesettings::Intent::RemoveOverride || (value == other.value && remembered == other.remembered);
    }
};
using Reducer = persistentedit::Editor<Cell,FIELDS>;
struct Save {
    Reducer::Attempt attempt;
    effectivesettings::Snapshot original;
    effectivesettings::Batch batch;
    std::array<uint64_t,FIELDS> inputs;
};
struct SaveResult {
    uint64_t token{0};
    bool present{false}, write_completed{false}, persisted{false}, known{true};
    QString error{};
    QString actual_values{};
};
/** Settings consumer of the shared editor. Run on Domain. Native ports supply
 * actual settings/write facts; this class has no Core, file, clock or QObject. */
class Session {
public:
    Session(persistentedit::Identity identity,Reducer::Mask allowed);
    const Reducer& editor() const { return m_editor; }
    const effectivesettings::Snapshot& snapshot() const { return m_snapshot; }
    const SaveResult& result() const { return m_result; }
    const QString& error() const { return m_error; }
    QString fieldError(effectivesettings::Key key) const;
    QString literal(effectivesettings::Key key) const;
    uint64_t input(effectivesettings::Key key) const;
    bool fresh() const { return m_fresh; }
    bool valid() const;
    bool canSave() const;
    bool observe(effectivesettings::Snapshot snapshot);
    bool editBoolean(effectivesettings::Key key,bool value);
    bool editText(effectivesettings::Key key,QString text);
    bool useConfiguration(effectivesettings::Key key);
    bool useValue(effectivesettings::Key key,effectivesettings::Value value);
    std::optional<Save> check();
    bool validated(uint64_t token,bool valid,QString error={},std::map<effectivesettings::Key,QString> fields={});
    std::optional<Save> active() const { return m_active; }
    bool finished(const effectivesettings::Result& result,bool causal_observation);
    bool reconciled(const effectivesettings::Result& result);
    bool discard();
    bool requestClose();
    bool answerClose(uint64_t token,persistentedit::CloseChoice choice);
    bool resolve(effectivesettings::Key key,bool keep_edit);
    void retire();
    void clearFieldError(effectivesettings::Key key){m_fields.erase(key);m_error={};}
    bool needsRefresh() const { return m_needs_refresh; }
    void clearRefreshRequest() { m_needs_refresh=false; }
    static QString Format(const effectivesettings::Value& value);
private:
    bool edit(effectivesettings::Key key,Cell cell,QString literal);
    bool acceptedKey(effectivesettings::Key key) const;
    Reducer::Values Values(const effectivesettings::Snapshot& snapshot) const;
    effectivesettings::Batch Batch(const Reducer::Attempt& attempt) const;
    bool complete(const effectivesettings::Result& result,bool causal,bool reconcile);
    void projectUntouchedLiterals();
    Reducer m_editor;
    const Reducer::Mask m_allowed;
    effectivesettings::Snapshot m_snapshot;
    std::array<QString,FIELDS> m_literals;
    std::array<uint64_t,FIELDS> m_inputs{};
    uint64_t m_observation{0},m_next_input{0};
    bool m_fresh{false},m_needs_refresh{false};
    QString m_error;
    std::map<effectivesettings::Key,QString> m_fields;
    std::optional<Save> m_active;
    SaveResult m_result;
};
} // namespace settingsedit
#endif // BITCOIN_QML_SETTINGSSESSION_H
