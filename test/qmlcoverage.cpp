// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/qmlcoverage.h>

#include <QCryptographicHash>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QSet>
#include <QThread>

#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace QmlCoverage {
constexpr qsizetype MAX_MANIFEST_BYTES{32 * 1024 * 1024};
constexpr qsizetype MAX_POINTS{200000};
constexpr qsizetype MAX_ENGINES{64};
constexpr qsizetype MAX_COUNTER_BYTES{128 * 1024 * 1024};
constexpr auto CONTRACT = "ast-entry-sites-and-explicit-boolean-decisions-v1";

enum class Kind { Line, Truth, Nullish, Switch, Case, Iteration, Caught };
struct EngineCounts {
    std::vector<std::array<quint64, 2>> counts;
    std::atomic<bool> invalid{false};
    std::atomic<bool> active{true};
};
struct State {
    QMutex mutex;
    QJsonObject manifest;
    std::vector<Kind> kinds;
    std::vector<std::shared_ptr<EngineCounts>> engines;
    QString rcc;
    QString output;
    bool prepared{false};
    bool registered{false};
    bool closed{false};
};
State& state()
{
    static State instance;
    return instance;
}
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
QByteArray sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
bool readFile(const QString& path, QByteArray* bytes, QString* error, qint64 limit = -1)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "Coverage cannot read " + path);
    if (limit >= 0 && file.size() > limit) return fail(error, "Coverage file exceeds limit: " + path);
    *bytes = file.readAll();
    if (file.error() != QFile::NoError) return fail(error, "Coverage read failed: " + path);
    return true;
}

/** No properties, change signals, file I/O, allocation, or locks in the hit path.
 * QJSValue is returned unchanged: truthiness is observed, never substituted.
 * Counters belong to one engine thread; the release/acquire lifecycle handshake
 * prevents teardown from reading counters while that thread can still write.
 */
class CoverageSink final : public QObject {
    Q_OBJECT
public:
    CoverageSink(QQmlEngine* engine, std::shared_ptr<EngineCounts> counts)
        : QObject(engine), m_counts(std::move(counts)) {}
    ~CoverageSink() override
    {
        if (m_depth != 0) invalid();
        m_counts->active.store(false, std::memory_order_release);
    }

    Q_INVOKABLE void line(int id) { hit(id, Kind::Line, 0); }
    Q_INVOKABLE QJSValue truth(int id, const QJSValue& value)
    {
        hit(id, Kind::Truth, value.toBool() ? 1 : 0);
        return value;
    }
    Q_INVOKABLE QJSValue nullish(int id, const QJSValue& value)
    {
        hit(id, Kind::Nullish, value.isNull() || value.isUndefined() ? 1 : 0);
        return value;
    }
    Q_INVOKABLE void enter(int id)
    {
        if (!contextValid(id) || m_depth == m_frames.size()) { invalid(); return; }
        const auto kind = state().kinds[id];
        if (kind != Kind::Switch && kind != Kind::Iteration && kind != Kind::Caught) { invalid(); return; }
        auto& frame = m_frames[m_depth++];
        frame.id = id;
        frame.flag = false;
        frame.value = QJSValue{};
        if (kind == Kind::Switch) hit(id, kind, 0);
    }
    Q_INVOKABLE QJSValue switchValue(int id, const QJSValue& value)
    {
        if (auto* frame = context(id, Kind::Switch)) frame->value = value;
        return value;
    }
    Q_INVOKABLE QJSValue caseValue(int id, int owner, const QJSValue& value)
    {
        if (auto* frame = context(owner, Kind::Switch)) hit(id, Kind::Case, value.strictlyEquals(frame->value) ? 1 : 0);
        return value;
    }
    Q_INVOKABLE void step(int id)
    {
        if (context(id, Kind::Iteration)) hit(id, Kind::Iteration, 1);
    }
    Q_INVOKABLE void broke(int id)
    {
        if (auto* frame = context(id, Kind::Iteration)) frame->flag = true;
    }
    Q_INVOKABLE void exhausted(int id)
    {
        if (auto* frame = context(id, Kind::Iteration); frame && !frame->flag) hit(id, Kind::Iteration, 0);
    }
    Q_INVOKABLE void caught(int id)
    {
        if (auto* frame = context(id, Kind::Caught)) frame->flag = true;
    }
    Q_INVOKABLE void leave(int id)
    {
        if (!contextValid(id) || m_depth == 0 || m_frames[m_depth - 1].id != id) { invalid(); return; }
        auto& frame = m_frames[--m_depth];
        if (state().kinds[id] == Kind::Caught) hit(id, Kind::Caught, frame.flag ? 1 : 0);
        frame.value = QJSValue{};
    }
private:
    struct Frame { int id{-1}; bool flag{false}; QJSValue value; };
    std::array<Frame, 256> m_frames;
    size_t m_depth{0};
    void invalid() { m_counts->invalid.store(true, std::memory_order_relaxed); }
    bool contextValid(int id)
    {
        if (QThread::currentThread() == thread() && id >= 0 && static_cast<size_t>(id) < state().kinds.size()) return true;
        invalid();
        return false;
    }
    Frame* context(int id, Kind kind)
    {
        if (!contextValid(id) || state().kinds[id] != kind) { invalid(); return nullptr; }
        for (size_t depth = m_depth; depth > 0; --depth) {
            if (m_frames[depth - 1].id == id) return &m_frames[depth - 1];
        }
        invalid();
        return nullptr;
    }
    void hit(int id, Kind kind, unsigned outcome)
    {
        const auto& kinds = state().kinds;
        if (QThread::currentThread() != thread() || id < 0 ||
            static_cast<size_t>(id) >= kinds.size() || kinds[id] != kind) {
            m_counts->invalid.store(true, std::memory_order_relaxed);
            return;
        }
        auto& count = m_counts->counts[id][outcome];
        if (count != std::numeric_limits<quint64>::max()) ++count;
    }
    std::shared_ptr<EngineCounts> m_counts;
};
} // namespace

namespace QmlCoverage {
bool requested()
{
    return qEnvironmentVariableIsSet("QML_COVERAGE_MANIFEST") ||
        qEnvironmentVariableIsSet("QML_COVERAGE_RCC") ||
        qEnvironmentVariableIsSet("QML_COVERAGE_OUTPUT");
}

bool prepare(QString* error)
{
    if (!requested()) return true;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    if (s.prepared || s.closed) return fail(error, "Coverage prepared more than once");
    const QString manifest_path = qEnvironmentVariable("QML_COVERAGE_MANIFEST");
    s.rcc = qEnvironmentVariable("QML_COVERAGE_RCC");
    s.output = qEnvironmentVariable("QML_COVERAGE_OUTPUT");
    if (manifest_path.isEmpty() || s.rcc.isEmpty() || s.output.isEmpty()) {
        return fail(error, "Coverage requires manifest, RCC and output paths together");
    }
    if (s.output == manifest_path || s.output == s.rcc) return fail(error, "Coverage output aliases its inputs");
    // Source URLs can have statically registered qmlcachegen units. Qt's
    // supported switch disables both AOT units and disk-cache reuse, so the
    // verified replacement bytes are actually parsed. Normal tests retain the
    // production execution mode; measurement runs must be reported separately.
    if (!qEnvironmentVariableIsEmpty("QML_FORCE_DISK_CACHE") && qEnvironmentVariable("QML_FORCE_DISK_CACHE") != "0") {
        return fail(error, "Forced QML cache conflicts with source instrumentation");
    }
    if (!qputenv("QML_DISABLE_DISK_CACHE", "1")) return fail(error, "Cannot select source instrumentation mode");
    QByteArray bytes;
    if (!readFile(manifest_path, &bytes, error, MAX_MANIFEST_BYTES)) return false;
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(error, "Invalid coverage manifest JSON");
    }
    s.manifest = document.object();
    if (s.manifest.value("schema").toInt() != 1 ||
        s.manifest.value("contract").toString() != CONTRACT ||
        !s.manifest.value("mapping_complete").toBool() ||
        !s.manifest.value("unsupported").isArray() ||
        !s.manifest.value("unsupported").toArray().isEmpty()) {
        return fail(error, "Unsupported or incomplete coverage mapping");
    }
    const auto points = s.manifest.value("points").toArray();
    if (points.isEmpty() || points.size() > MAX_POINTS) return fail(error, "Coverage point selection is empty or exceeds limit");
    QSet<QString> coordinate_ids;
    QMap<QString, qint64> source_lengths;
    const QRegularExpression hash_pattern(QStringLiteral("^[0-9a-f]{64}$"));
    for (const auto entry : s.manifest.value("sources").toArray()) {
        const auto source = entry.toObject();
        const auto path = source.value("path").toString();
        const auto length = source.value("length_utf16").toInteger(-1);
        if (path.isEmpty() || source_lengths.contains(path) || length <= 0 ||
            !hash_pattern.match(source.value("sha256").toString()).hasMatch() ||
            !hash_pattern.match(source.value("instrumented_sha256").toString()).hasMatch()) {
            return fail(error, "Invalid coverage source manifest");
        }
        source_lengths.insert(path, length);
    }
    if (source_lengths.isEmpty()) return fail(error, "Coverage source scope is empty");
    for (qsizetype i = 0; i < points.size(); ++i) {
        const auto point = points[i].toObject();
        const auto coordinate = point.value("coordinate_id").toString();
        if (point.value("id").toInt(-1) != i || coordinate.isEmpty() || coordinate_ids.contains(coordinate)) {
            return fail(error, "Duplicate or non-contiguous coverage point IDs");
        }
        coordinate_ids.insert(coordinate);
        const auto kind = point.value("kind").toString();
        const auto file = point.value("file").toString();
        const auto start = point.value("start_utf16").toInteger(-1);
        const auto end = point.value("end_utf16").toInteger(-1);
        const auto role = point.value("role").toString();
        if (!source_lengths.contains(file) || start < 0 || end <= start || end > source_lengths.value(file) ||
            point.value("line").toInt() <= 0 || point.value("column").toInt() <= 0 || role.isEmpty() ||
            coordinate != file + ":" + QString::number(start) + ":" + QString::number(end) + ":" + kind + ":" + role) {
            return fail(error, "Unmapped coverage source coordinates");
        }
        if (kind == "line") s.kinds.push_back(Kind::Line);
        else if (kind == "truth") s.kinds.push_back(Kind::Truth);
        else if (kind == "nullish") s.kinds.push_back(Kind::Nullish);
        else if (kind == "switch") s.kinds.push_back(Kind::Switch);
        else if (kind == "case") s.kinds.push_back(Kind::Case);
        else if (kind == "iteration") s.kinds.push_back(Kind::Iteration);
        else if (kind == "caught") s.kinds.push_back(Kind::Caught);
        else return fail(error, "Unknown coverage point kind");
    }
    if (s.manifest.value("aliases").toArray().isEmpty()) return fail(error, "Empty coverage resource mapping");
    if (!readFile(s.rcc, &bytes, error)) return false;
    if (sha256(bytes) != s.manifest.value("rcc_sha256").toString().toLatin1()) {
        return fail(error, "Coverage RCC digest mismatch");
    }
    s.prepared = true;
    return true;
}

bool registerResources(QString* error)
{
    if (!requested()) return true;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    if (!s.prepared || s.registered || s.closed) return fail(error, "Invalid coverage resource lifecycle");
    if (!QResource::registerResource(s.rcc)) return fail(error, "Coverage RCC registration failed");
    QSet<QString> paths;
    for (const auto value : s.manifest.value("aliases").toArray()) {
        const auto alias = value.toObject();
        const auto path = alias.value("path").toString();
        if (!path.startsWith(":/") || paths.contains(path)) return fail(error, "Invalid or duplicate coverage resource alias");
        paths.insert(path);
        QByteArray bytes;
        if (!readFile(path, &bytes, error)) return false;
        if (sha256(bytes) != alias.value("sha256").toString().toLatin1()) {
            return fail(error, "Coverage resource is shadowed or changed: " + path);
        }
    }
    s.registered = true;
    return true;
}

bool install(QQmlEngine* engine, QString* error)
{
    if (!requested()) return true;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    if (!s.registered || s.closed || !engine || QThread::currentThread() != engine->thread()) {
        return fail(error, "Invalid coverage engine lifecycle");
    }
    if (engine->globalObject().hasProperty("__r10cov")) return fail(error, "Coverage engine hook already exists");
    if (s.engines.size() >= MAX_ENGINES ||
        (s.engines.size() + 1) * s.kinds.size() * sizeof(std::array<quint64, 2>) > MAX_COUNTER_BYTES) {
        return fail(error, "Coverage engine/counter storage limit exceeded");
    }
    auto counts = std::make_shared<EngineCounts>();
    counts->counts.resize(s.kinds.size());
    auto* sink = new CoverageSink(engine, counts);
    QJSEngine::setObjectOwnership(sink, QJSEngine::CppOwnership);
    const auto setter = engine->evaluate(QStringLiteral(
        "(function(value) { Object.defineProperty(this, '__r10cov', "
        "{value:value, writable:false, configurable:false}); })"));
    const auto result = setter.callWithInstance(engine->globalObject(), {engine->newQObject(sink)});
    if (result.isError()) {
        delete sink;
        return fail(error, "Coverage cannot install engine global: " + result.toString());
    }
    s.engines.push_back(std::move(counts));
    return true;
}

bool finish(QString* error)
{
    if (!requested()) return true;
    auto& s = state();
    QMutexLocker lock(&s.mutex);
    if (!s.registered || s.closed || s.engines.empty()) return fail(error, "Coverage has no completed engine measurements");
    s.closed = true;
    bool valid = true;
    for (const auto& counts : s.engines) {
        if (counts->active.load(std::memory_order_acquire)) return fail(error, "Coverage engine is still alive at export");
        valid &= !counts->invalid.load(std::memory_order_relaxed);
    }
    QJsonArray points;
    const auto definitions = s.manifest.value("points").toArray();
    quint64 covered_points{0};
    quint64 total_outcomes{0};
    quint64 covered_outcomes{0};
    QMap<QString, QMap<int, bool>> lines;
    for (size_t i = 0; i < s.kinds.size(); ++i) {
        std::array<quint64, 2> aggregate{};
        for (const auto& engine : s.engines) {
            for (unsigned outcome = 0; outcome < 2; ++outcome) {
                const auto value = engine->counts[i][outcome];
                aggregate[outcome] += qMin(value, std::numeric_limits<quint64>::max() - aggregate[outcome]);
            }
        }
        auto point = definitions[i].toObject();
        // Decimal strings retain uint64 precision in JSON consumers.
        point.insert("counts", QJsonArray{QString::number(aggregate[0]), QString::number(aggregate[1])});
        const bool covered = aggregate[0] > 0 || aggregate[1] > 0;
        auto& file_lines = lines[point.value("file").toString()];
        const int line = point.value("line").toInt();
        file_lines[line] = file_lines.value(line, true) && covered;
        covered_points += covered;
        if (s.kinds[i] != Kind::Line && s.kinds[i] != Kind::Switch) {
            total_outcomes += 2;
            covered_outcomes += (aggregate[0] > 0) + (aggregate[1] > 0);
        }
        points.append(point);
    }
    QJsonArray line_counts;
    qint64 total_lines{0};
    qint64 covered_lines{0};
    for (auto file = lines.cbegin(); file != lines.cend(); ++file) {
        for (auto line = file->cbegin(); line != file->cend(); ++line) {
            ++total_lines;
            covered_lines += line.value();
            line_counts.append(QJsonObject{{"file", file.key()}, {"line", line.key()}, {"all_sites_visited", line.value()}});
        }
    }
    QJsonObject report{
        {"schema", 1}, {"contract", CONTRACT}, {"valid_measurement", valid},
        {"tool_qualified", false}, {"engine_count", static_cast<int>(s.engines.size())},
        {"execution_mode", "Instrumented source; AOT and disk cache disabled only for measurement"},
        {"point_count", static_cast<int>(s.kinds.size())},
        {"line_metric", "Original starting lines of executable AST sites; every mapped site on a line must execute"},
        {"line_count", total_lines}, {"covered_lines", covered_lines}, {"lines", line_counts},
        {"covered_points", static_cast<qint64>(covered_points)},
        {"branch_outcomes", static_cast<qint64>(total_outcomes)},
        {"covered_branch_outcomes", static_cast<qint64>(covered_outcomes)},
        {"manifest", s.manifest}, {"points", points}};
    QSaveFile output(s.output);
    if (!output.open(QIODevice::WriteOnly)) return fail(error, "Coverage output cannot be opened");
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) return fail(error, "Coverage output write failed");
    if (!valid) return fail(error, "Coverage observed unknown points, kinds or engine threads");
    return true;
}
} // namespace QmlCoverage

#include <qmlcoverage.moc>
