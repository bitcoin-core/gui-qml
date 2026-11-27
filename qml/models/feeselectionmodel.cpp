// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/feeselectionmodel.h>
#include <qml/guipresentation.h>
#include <QLocale>
#include <QPointer>
#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
namespace {
constexpr std::array<unsigned int, 7> TARGETS{2, 3, 4, 6, 10, 25, 50};
int NearestTarget(unsigned int target)
{
    auto nearest = TARGETS.begin();
    for (auto it = TARGETS.begin() + 1; it != TARGETS.end(); ++it)
        if (std::abs(int(*it) - int(target)) < std::abs(int(*nearest) - int(target))) nearest = it;
    return static_cast<int>(nearest - TARGETS.begin());
}
}
namespace feepolicy {
std::optional<CAmount> ParseRate(const QString& original, QChar decimal)
{
    if (original.size() > 32) return {};
    const auto text = original.trimmed(); if (text.isEmpty()) return {};
    CAmount whole{0}, fraction{0}; int digits{0}; bool point{false}, integer{false};
    for (const QChar c : text) {
        if ((c == decimal || c == QLatin1Char{'.'}) && !point) { point = true; continue; }
        if (c < QLatin1Char{'0'} || c > QLatin1Char{'9'}) return {};
        const int digit = c.unicode() - ushort{'0'};
        if (point) { if (++digits > 3) return {}; fraction = fraction * 10 + digit; }
        else { integer = true; if (whole > (std::numeric_limits<CAmount>::max() - digit) / 10) return {}; whole = whole * 10 + digit; }
    }
    if (!integer && digits == 0) return {};
    while (digits++ < 3) fraction *= 10;
    if (whole > (std::numeric_limits<CAmount>::max() - fraction) / 1000) return {};
    return whole * 1000 + fraction;
}
QString FormatRate(CAmount rate)
{
    if (rate < 0) return {};
    return QString::number(rate / 1000) + QLatin1Char{'.'} + QString::number(rate % 1000).rightJustified(3, QLatin1Char{'0'});
}
}
struct FeeSelectionModel::Input {
    std::mutex mutex;
    feepolicy::Snapshot desired;
    CAmount minimum{DEFAULT_MIN_RELAY_TX_FEE};
    QHash<unsigned int, CAmount> rates;
    QChar decimal{QLatin1Char{'.'}};
};
FeeSelectionModel::FeeSelectionModel(QObject* parent) : QObject(parent), m_input(std::make_shared<Input>()), m_rate(new FeeRateEntry(this))
{
    connect(m_rate, &FeeRateEntry::edited, this, &FeeSelectionModel::editRate);
}
FeeSelectionModel::~FeeSelectionModel() { close(); }
void FeeSelectionModel::setServices(presentation::Post post, execution::SnapshotQuery<feepolicy::Snapshot>::Post submit)
{
    if (m_closed || m_query || !submit) return;
    if (!post) post = presentation::QueuedPost();
    const QPointer<FeeSelectionModel> observer{this}; const auto input = m_input;
    m_query = std::make_shared<execution::SnapshotQuery<feepolicy::Snapshot>>([input] {
        feepolicy::Snapshot value; CAmount minimum; QHash<unsigned int, CAmount> rates; QChar decimal;
        { std::lock_guard lock{input->mutex}; value = input->desired; minimum = input->minimum; rates = input->rates; decimal = input->decimal; }
        if (value.manual_mode && !value.manual_rate) {
            value.target = TARGETS[NearestTarget(value.target)];
            value.text = rates.value(value.target, 0) > 0 ? feepolicy::FormatRate(rates.value(value.target)) : QString{};
        }
        value.rate = feepolicy::ParseRate(value.text, decimal);
        if (!value.rate || *value.rate < minimum) {
            auto minimum_text = feepolicy::FormatRate(minimum);
            while (minimum_text.endsWith(QLatin1Char{'0'})) minimum_text.chop(1);
            if (minimum_text.endsWith(QLatin1Char{'.'})) minimum_text.chop(1);
            value.rate.reset(); value.error = QObject::tr("Enter a fee rate of at least %1 sat/vB.").arg(minimum_text);
        }
        if (value.text.size() > 32) value.error = QObject::tr("Fee input exceeds the 32-character limit.");
        if (value.manual_rate && value.rate && rates.size() > 1) {
            const auto first = rates.value(TARGETS.front(), 0);
            const bool distinct = std::any_of(TARGETS.begin() + 1, TARGETS.end(), [&](auto target) { return rates.value(target, 0) != first; });
            if (distinct) { value.target = TARGETS.back(); for (const auto target : TARGETS) if (rates.value(target, 0) > 0 && *value.rate >= rates.value(target)) { value.target = target; break; } }
        }
        value.preset = value.manual_mode ? 3 : value.target == 2 ? 0 : value.target == 10 ? 2 : 1;
        value.target_index = NearestTarget(value.target); return value;
    }, [post = std::move(post), observer] { post([observer] { if (observer) observer->publishPolicy(); }); },
        std::move(submit));
    request(false);
}
void FeeSelectionModel::refresh()
{
    if (m_closed) return;
    const QPointer<FeeSelectionModel> guard{this};
    request(false); // Explicit retry of this captured policy, never a blind loop.
    if (guard && !m_closed) Q_EMIT refreshRequested();
}
void FeeSelectionModel::reject(const QString& error) { if (!m_closed) Q_EMIT commandRejected(error); }
void FeeSelectionModel::choosePreset(int target)
{
    if (m_closed) return;
    if (target < 1 || target > 1008) { reject(tr("Choose a valid fee target.")); return; }
    if (!m_manual_mode && m_target == static_cast<unsigned int>(target)) return;
    m_manual_mode = false; m_target = static_cast<unsigned int>(target); request(true);
}
void FeeSelectionModel::chooseCustom()
{
    if (m_closed || m_manual_mode) return;
    m_manual_mode = true; request(true);
}
void FeeSelectionModel::chooseCustomTarget(int target)
{
    if (m_closed) return;
    if (target < 1 || target > 1008) { reject(tr("Choose a valid fee target.")); return; }
    if (m_manual_mode && m_target == static_cast<unsigned int>(target)) return;
    m_manual_mode = true; m_manual_rate = false; m_target = static_cast<unsigned int>(target); request(true);
}
void FeeSelectionModel::chooseTargetIndex(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= TARGETS.size()) { reject(tr("Choose a valid fee target.")); return; }
    chooseCustomTarget(static_cast<int>(TARGETS[index]));
}
void FeeSelectionModel::captureMode(bool manual) { if (manual) chooseCustom(); else choosePreset(static_cast<int>(m_target)); }
void FeeSelectionModel::captureTarget(unsigned int target)
{
    if (m_closed || target < 1 || target > 1008 || m_target == target) return;
    m_target = target; request(true);
}
void FeeSelectionModel::editRate(const QString& text)
{
    if (m_closed) return;
    const auto captured = text.left(33);
    if (m_manual_rate && m_manual_text == captured) return;
    m_manual_text = captured; m_manual_rate = true; request(true);
}
void FeeSelectionModel::observe(CAmount minimum, QHash<unsigned int, CAmount> rates)
{
    if (m_closed || (m_minimum == minimum && m_rates == rates)) return;
    m_minimum = minimum; m_rates = std::move(rates); ++m_observations; request(false);
}
void FeeSelectionModel::request(bool intent)
{
    if (m_closed) return;
    ++m_generation; if (intent) ++m_intent; m_pending = true; m_rate->m_pending = true;
    if (m_manual_rate) m_rate->m_text = m_manual_text;
    { std::lock_guard lock{m_input->mutex};
      m_input->desired = {m_generation, m_intent, m_observations, m_target, 1, 3, m_manual_mode, m_manual_rate, m_manual_text, {}, {}};
      m_input->minimum = m_minimum; m_input->rates = m_rates; m_input->decimal = QLocale().decimalPoint().at(0); }
    const QPointer<FeeSelectionModel> guard{this};
    if (intent) { Q_EMIT intentChanged(); if (!guard || m_closed) return; }
    Q_EMIT m_rate->changed(); if (!guard || m_closed) return;
    Q_EMIT policyChanged(); if (!guard || m_closed) return;
    if (m_query) m_query->request();
}
void FeeSelectionModel::publishPolicy()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (result->snapshot && result->snapshot->generation != m_generation) return;
    m_pending = false; m_rate->m_pending = false;
    const QPointer<FeeSelectionModel> guard{this};
    if (!result->snapshot) { m_snapshot.reset(); m_rate->m_valid = false; m_rate->m_error = tr("The fee policy could not be validated. Try again."); }
    else { m_snapshot = *result->snapshot; m_target = m_snapshot->target; m_rate->m_text = m_snapshot->text; m_rate->m_valid = m_snapshot->rate.has_value(); m_rate->m_error = m_snapshot->error; }
    Q_EMIT m_rate->changed(); if (guard && !m_closed) Q_EMIT policyChanged();
}
void FeeSelectionModel::retryPublications() { if (!m_closed) publishPolicy(); }
void FeeSelectionModel::close() { if (m_closed) return; m_closed = true; if (m_query) m_query->close(); m_pending = false; m_rate->m_pending = false; m_rate->m_valid = false; m_fresh = false; m_estimate_state = Stopping; }
void FeeSelectionModel::finishStop() { if (m_closed) m_estimate_state = Stopped; }
int FeeSelectionModel::presetIndex() const { return m_manual_mode ? 3 : m_target == 2 ? 0 : m_target == 10 ? 2 : 1; }
int FeeSelectionModel::targetIndex() const { return m_snapshot ? m_snapshot->target_index : 3; }
std::optional<CAmount> FeeSelectionModel::ratePerKvB() const { return !m_closed && !m_pending && m_snapshot && m_snapshot->generation == m_generation ? m_snapshot->rate : std::nullopt; }
bool FeeSelectionModel::estimatePending() const { return m_estimate_state == Scheduled || m_estimate_state == Reading || m_estimate_state == ReadingWithPendingWork || m_estimate_state == Applying; }
std::optional<SendFeePreview> FeeSelectionModel::retainedPreview() const
{
    if (m_manual_mode) return m_estimates.custom;
    const auto found = m_estimates.estimates.constFind(m_target); return found == m_estimates.estimates.cend() ? std::nullopt : std::make_optional(*found);
}
std::optional<SendFeePreview> FeeSelectionModel::selectedPreview() const { return m_fresh ? retainedPreview() : std::nullopt; }
std::optional<SendFeePreview> FeeSelectionModel::preview(unsigned int target) const { if (!m_fresh) return {}; const auto found = m_estimates.estimates.constFind(target); return found == m_estimates.estimates.cend() ? std::nullopt : std::make_optional(*found); }
qint64 FeeSelectionModel::estimatedFeeSatoshi() const { const auto p = retainedPreview(); return p ? p->fee : -1; }
QString FeeSelectionModel::estimatedRate() const { const auto p = retainedPreview(); return p ? feepolicy::FormatRate(p->rate_per_kvb) : QString{}; }
FeeSelectionModel::Provenance FeeSelectionModel::provenance() const { const auto p = selectedPreview(); return p ? static_cast<Provenance>(p->provenance) : Unknown; }
bool FeeSelectionModel::hasEstimatorTarget() const { const auto p = selectedPreview(); return p && p->provenance == feeestimate::Provenance::Estimated && p->observed_target > 0; }
void FeeSelectionModel::invalidateEstimate(EstimateState state) { if (m_closed) return; m_fresh = false; m_estimate_state = state; m_error = NoError; m_error_text.clear(); Q_EMIT estimateChanged(); }
void FeeSelectionModel::publishEstimate(feeestimate::Phase phase, FeeReadResult result, bool fresh, feeestimate::Dependencies dependencies, bool retain_previous)
{
    if (m_closed) return;
    const auto text = m_manual_mode ? result.custom_error : result.errors.value(m_target);
    const bool selected = m_manual_mode ? result.custom.has_value() : result.estimates.contains(m_target);
    if (fresh && !selected) {
        fresh = false; phase = feeestimate::Phase::Failed;
        if (result.error == feeestimate::Error::None) result.error = feeestimate::Error::Unavailable;
        if (result.message.isEmpty()) result.message = text.isEmpty() ? tr("Fee estimation is unavailable. Choose Custom to enter a rate.") : text;
    }
    m_estimate_state = static_cast<EstimateState>(phase); m_fresh = fresh; m_error = static_cast<Error>(result.error); m_error_text = result.message;
    // A successful other preset cannot replace the selected policy's last
    // valid data after a partial failure. Keep its exact previous provenance.
    if (fresh || retain_previous) { m_estimates = std::move(result); m_estimate_dependencies = dependencies; }
    if (m_error_text.isEmpty()) m_error_text = text;
    Q_EMIT estimateChanged();
}
