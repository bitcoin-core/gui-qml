// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/monospaceoutputmodel.h>
#include <qml/guipresentation.h>

#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>

namespace {
constexpr int CAPTURE_ROWS{32};
constexpr qsizetype CAPTURE_CHARS{16 * 1024};
constexpr qsizetype SOURCE_ROW_CHARS{8192};
constexpr qsizetype SOURCE_CHARS{4 * 1024 * 1024};
constexpr qsizetype PROJECTION_BYTES{8 * 1024 * 1024};
constexpr int OUTPUT_ROWS{50'000};
constexpr int ROW_CHARS{1024};
constexpr int SEARCH_ROWS{64};
constexpr int SEARCH_MATCHES{10'000};

QString FragmentHtml(QTextDocument& document, int start, int end)
{
    QTextCursor cursor{&document};
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString html = cursor.selection().toHtml();
    const auto begin = html.indexOf(QStringLiteral("<!--StartFragment-->"));
    const auto finish = html.indexOf(QStringLiteral("<!--EndFragment-->"));
    if (begin >= 0 && finish >= begin) return html.mid(begin + 20, finish - begin - 20);
    const auto body = html.indexOf(QLatin1Char('>'), html.indexOf(QStringLiteral("<body")));
    const auto close = html.lastIndexOf(QStringLiteral("</body>"));
    return body >= 0 && close > body ? html.mid(body + 1, close - body - 1) : html;
}
} // namespace

struct MonospaceOutputModel::Mailbox : PresentationServices::Retirement, std::enable_shared_from_this<Mailbox> {
    struct Publication { quint64 generation; std::shared_ptr<const Rows> rows; bool truncated; QString error; };
    struct CopyPublication { quint64 generation; std::shared_ptr<const QString> text; };
    struct SearchPublication { quint64 revision; std::shared_ptr<const Matches> matches; bool limited; bool reset; };
    Services services;
    std::function<void()> notify;
    // Retries every retirement tracked by the services; runs on the GUI thread.
    std::function<void()> retry_all;
    std::mutex mutex;
    bool closed{false}, notice{false}, retirement_scheduled{false};
    std::optional<Publication> publication;
    std::optional<CopyPublication> copy;
    std::optional<SearchPublication> search;
    QList<std::shared_ptr<const void>> retired;
    void wake()
    {
        bool send;
        { std::lock_guard lock{mutex}; send = !closed && !std::exchange(notice, true); }
        if (send) try { notify(); } catch (...) { std::lock_guard lock{mutex}; notice = false; }
    }
    void publish(Publication value)
    {
        Publication old{};
        {
            std::lock_guard lock{mutex};
            if (closed) return;
            if (publication) old = std::move(*publication);
            publication = std::move(value);
        }
        // Replaced unpublished values die on the background caller. Keep the
        // retired value initialized independently of the optional slot state.
        old = {}; wake();
    }
    void publishSearch(SearchPublication value)
    {
        std::optional<SearchPublication> old;
        { std::lock_guard lock{mutex}; if (closed) return; old = std::exchange(search, std::move(value)); }
        old.reset(); wake();
    }
    void publishCopy(CopyPublication value) {
        std::optional<CopyPublication> old;
        { std::lock_guard lock{mutex}; if (closed) return; old = std::exchange(copy, std::move(value)); }
        old.reset(); wake();
    }
    bool retire(std::shared_ptr<const void> value)
    {
        if (!value) return true;
        { std::lock_guard lock{mutex}; retired.append(std::move(value)); }
        return retry();
    }
    size_t pending() override { std::lock_guard lock{mutex}; return retired.size() + size_t(retirement_scheduled); }
    bool retry() override
    {
        { std::lock_guard lock{mutex}; if (retirement_scheduled || retired.empty()) return true; retirement_scheduled = true; }
        const auto self = shared_from_this();
        try {
        if (services.retire && services.retire([self] {
            QList<std::shared_ptr<const void>> values;
            { std::lock_guard lock{self->mutex}; values.swap(self->retired); }
            values.clear();
            { std::lock_guard lock{self->mutex}; self->retirement_scheduled = false; }
            self->wake();
            if (self->services.post) try { self->services.post([retry_all = self->retry_all] { if (retry_all) retry_all(); }); } catch (...) {}
        })) return true;
        } catch (...) {}
        std::lock_guard lock{mutex}; retirement_scheduled = false; return false;
    }
};

struct MonospaceOutputModel::Projection {
    struct Source { QString text, left, right; int category; bool continuation{false}; };
    std::atomic_bool cancelled{false};
    quint64 generation{0}, source_revision{0};
    int next_row{-1}, first_row{0};
    qsizetype source_chars{0}, bytes{0};
    bool rich{false}, truncated{false};
    Rows rows;
};

struct MonospaceOutputModel::Search {
    std::atomic_bool cancelled{false};
    quint64 revision;
    QString text;
    std::shared_ptr<const Rows> rows;
    std::shared_ptr<Matches> matches{std::make_shared<Matches>()};
    int row{0};
    bool reset{false}, limited{false};
};

MonospaceOutputModel::MonospaceOutputModel(QObject* parent) : QAbstractListModel(parent)
{
    m_refresh_timer.setSingleShot(true);
    m_refresh_timer.setInterval(0);
    connect(&m_refresh_timer, &QTimer::timeout, this, &MonospaceOutputModel::startCapture);
    usePorts({});
}

MonospaceOutputModel::~MonospaceOutputModel()
{
    close();
    // The services keep tracking a mailbox with pending values after this model is gone.
    if (m_mailbox) m_mailbox->retry();
}

void MonospaceOutputModel::setServices(PresentationServices* services)
{
    if (!services || m_presentation || m_closed) return;
    m_presentation = services;
    connect(services, &PresentationServices::closed, this, &MonospaceOutputModel::close);
    Q_EMIT servicesChanged();
    if (services->isClosed()) close();
    else usePorts(services->ports());
}

void MonospaceOutputModel::usePorts(Services services)
{
    if (m_mailbox) {
        if (m_projection) { m_projection->cancelled = true; retire(std::exchange(m_projection, {})); }
        if (m_search) { m_search->cancelled = true; retire(std::exchange(m_search, {})); }
        retire(std::exchange(m_rows, {})); retire(std::exchange(m_matches, {}));
        std::optional<Mailbox::Publication> publication;
        std::optional<Mailbox::SearchPublication> result;
        std::optional<Mailbox::CopyPublication> copy;
        { std::lock_guard lock{m_mailbox->mutex}; m_mailbox->closed = true;
          publication = std::exchange(m_mailbox->publication, {}); result = std::exchange(m_mailbox->search, {}); copy = std::exchange(m_mailbox->copy, {}); }
        if (publication) retire(std::move(publication->rows));
        if (result) retire(std::move(result->matches));
        if (copy) retire(std::move(copy->text));
        m_mailbox->retry();
    }
    m_services = std::move(services);
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    m_mailbox = std::make_shared<Mailbox>();
    m_mailbox->services = m_services;
    if (m_presentation) {
        m_mailbox->retry_all = [services = m_presentation] { if (services) services->retryRetirements(); };
        m_presentation->track(m_mailbox);
    }
    const QPointer<MonospaceOutputModel> observer{this};
    m_mailbox->notify = [post = m_services.post, observer] {
        const auto turn = [observer] { if (observer) observer->publish(); };
        post(turn);
    };
    refresh();
}

void MonospaceOutputModel::retire(std::shared_ptr<const void> value)
{
    if (m_mailbox && !m_mailbox->retire(std::move(value))) {
        m_waiting_retirement = true;
        retryCleanup();
    }
}

void MonospaceOutputModel::retryCleanup()
{
    if (m_closed || m_retry_scheduled) return;
    m_retry_scheduled = true;
    const QPointer<MonospaceOutputModel> observer{this};
    const auto retry = [observer] {
        if (!observer) return;
        observer->m_retry_scheduled = false;
        if (observer->m_mailbox->retry() && observer->m_mailbox->pending() <= 8) {
            observer->m_waiting_retirement = false;
            observer->refresh();
        } else observer->retryCleanup();
    };
    if (m_services.retry) m_services.retry(retry);
    else QTimer::singleShot(1, this, retry);
}

void MonospaceOutputModel::close()
{
    if (std::exchange(m_closed, true)) return;
    m_refresh_timer.stop();
    if (m_projection) { m_projection->cancelled = true; retire(std::exchange(m_projection, {})); }
    if (m_search) { m_search->cancelled = true; retire(std::exchange(m_search, {})); }
    retire(std::exchange(m_rows, {})); retire(std::exchange(m_matches, {}));
    std::optional<Mailbox::Publication> publication;
    std::optional<Mailbox::SearchPublication> search_result;
    std::optional<Mailbox::CopyPublication> copy;
    { std::lock_guard lock{m_mailbox->mutex}; m_mailbox->closed = true; publication = std::exchange(m_mailbox->publication, {}); search_result = std::exchange(m_mailbox->search, {}); copy = std::exchange(m_mailbox->copy, {}); }
    if (publication) retire(std::move(publication->rows));
    if (search_result) retire(std::move(search_result->matches));
    if (copy) retire(std::move(copy->text));
    // Quiet fence: no scene/model signals while the application closes admission.
}

int MonospaceOutputModel::rowCount(const QModelIndex& parent) const { return parent.isValid() || !m_rows ? 0 : m_rows->size(); }
QVariant MonospaceOutputModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_rows || index.row() < 0 || index.row() >= m_rows->size()) return {};
    const auto& row = m_rows->at(index.row());
    switch (role) {
    case ContentRole: return row.content;
    case PlainContentRole: return row.plain;
    case LeftRole: return row.left;
    case RightRole: return row.right;
    case CategoryRole: return row.category;
    default: return {};
    }
}
QHash<int, QByteArray> MonospaceOutputModel::roleNames() const
{
    return {{ContentRole, "content"}, {PlainContentRole, "plainContent"}, {LeftRole, "leftText"}, {RightRole, "rightText"}, {CategoryRole, "category"}};
}

void MonospaceOutputModel::setSourceModel(QAbstractItemModel* source)
{
    if (m_source == source) return;
    if (m_source) disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (source) {
        const auto changed = [this] { ++m_source_revision; refresh(); };
        connect(source, &QAbstractItemModel::modelReset, this, changed);
        connect(source, &QAbstractItemModel::rowsInserted, this, changed);
        connect(source, &QAbstractItemModel::rowsRemoved, this, changed);
        connect(source, &QAbstractItemModel::dataChanged, this, changed);
        connect(source, &QAbstractItemModel::layoutChanged, this, changed);
        connect(source, &QObject::destroyed, this, changed);
    }
    ++m_source_revision; refresh(); Q_EMIT sourceModelChanged();
}
#define OUTPUT_ROLE_SETTER(Name, Member) \
void MonospaceOutputModel::Name(const QString& role) { if (Member == role) return; Member = role.left(128); ++m_source_revision; refresh(); Q_EMIT sourceModelChanged(); }
OUTPUT_ROLE_SETTER(setContentRole, m_content_role)
OUTPUT_ROLE_SETTER(setLeftRole, m_left_role)
OUTPUT_ROLE_SETTER(setRightRole, m_right_role)
OUTPUT_ROLE_SETTER(setCategoryRole, m_category_role)
#undef OUTPUT_ROLE_SETTER
void MonospaceOutputModel::setRichText(bool rich) { if (m_rich == rich) return; m_rich = rich; ++m_source_revision; refresh(); Q_EMIT sourceModelChanged(); }
void MonospaceOutputModel::setActive(bool active)
{
    if (m_active == active || m_closed) return;
    m_active = active;
    if (active) refresh();
    else {
        m_refresh_timer.stop();
        if (m_projection) { m_projection->cancelled = true; retire(std::exchange(m_projection, {})); }
        if (m_search) { m_search->cancelled = true; retire(std::exchange(m_search, {})); }
        beginResetModel(); retire(std::exchange(m_rows, {})); endResetModel();
        retire(std::exchange(m_matches, {})); m_match_index = -1;
        m_busy = m_search_busy = false;
        Q_EMIT countChanged(); Q_EMIT searchChanged(); Q_EMIT currentMatchChanged(); Q_EMIT changed();
    }
    Q_EMIT activeChanged();
}

void MonospaceOutputModel::refresh()
{
    if (!m_active || m_closed) return;
    if (m_mailbox->pending() > 8 || m_waiting_retirement) {
        m_waiting_retirement = true;
        retryCleanup();
        return;
    }
    m_refresh_timer.start();
}

void MonospaceOutputModel::startCapture()
{
    if (!m_active || m_closed) return;
    if (m_mailbox->pending() > 8 || m_waiting_retirement) { m_waiting_retirement = true; retryCleanup(); return; }
    if (!m_services.scheduler || !m_services.retire) { m_error = tr("Output is temporarily unavailable."); m_busy = false; Q_EMIT changed(); return; }
    if (m_projection) { m_projection->cancelled = true; retire(std::exchange(m_projection, {})); }
    auto projection = std::make_shared<Projection>();
    projection->generation = ++m_generation; projection->source_revision = m_source_revision; projection->rich = m_rich;
    const int count = m_source ? m_source->rowCount() : 0;
    projection->next_row = count - 1; projection->first_row = std::max(0, count - OUTPUT_ROWS);
    projection->truncated = count > OUTPUT_ROWS;
    m_projection = projection; m_busy = true; m_error.clear(); Q_EMIT changed();
    captureNext(projection);
}

void MonospaceOutputModel::captureNext(const std::shared_ptr<Projection>& projection)
{
    if (m_closed || !m_active || projection != m_projection || projection->cancelled) return;
    if (projection->source_revision != m_source_revision) { refresh(); return; }
    QVector<Projection::Source> input;
    qsizetype captured{0};
    const auto roles = m_source ? m_source->roleNames() : QHash<int, QByteArray>{};
    const auto role = [&](const QString& name) { return name.isEmpty() ? -1 : roles.key(name.toUtf8(), -1); };
    const int continuation_role = role(QStringLiteral("continuation"));
    const int text_role = role(m_content_role), left_role = role(m_left_role), right_role = role(m_right_role), category_role = role(m_category_role);
    for (int i = 0; m_source && i < CAPTURE_ROWS && captured < CAPTURE_CHARS && projection->next_row >= projection->first_row; ++i) {
        const auto index = m_source->index(projection->next_row--, 0);
        const auto original = m_source->data(index, text_role).toString();
        auto text = original.left(SOURCE_ROW_CHARS);
        if (text.size() < original.size()) { text += tr("\n[Long output line omitted beyond viewer limit.]"); projection->truncated = true; }
        if (captured + text.size() > CAPTURE_CHARS && !input.isEmpty()) { ++projection->next_row; break; }
        if (projection->source_chars + text.size() > SOURCE_CHARS) { projection->truncated = true; projection->next_row = -1; break; }
        captured += text.size(); projection->source_chars += text.size();
        input.append({std::move(text), left_role < 0 ? QString{} : m_source->data(index, left_role).toString().left(128),
            right_role < 0 ? QString{} : m_source->data(index, right_role).toString().left(128),
            category_role < 0 ? -1 : m_source->data(index, category_role).toInt(),
            continuation_role >= 0 && m_source->data(index, continuation_role).toBool()});
    }
    const bool final = projection->next_row < projection->first_row || !m_source;
    const auto mailbox = m_mailbox;
    const QPointer<MonospaceOutputModel> observer{this};
    const auto accepted = execution::Submit(*m_services.scheduler, [projection, input = std::move(input), final] {
        if (projection->cancelled) return false;
        for (const auto& source : input) {
            std::optional<QTextDocument> document; // Only rich text needs a document.
            QString plain = source.text;
            if (projection->rich) { document.emplace(); document->setHtml(source.text); plain = document->toPlainText(); }
            QVector<Row> chunks;
            int start{0};
            do {
                int end = std::min(start + ROW_CHARS, int(plain.size()));
                const auto newline = plain.indexOf(QLatin1Char('\n'), start);
                if (newline >= 0 && newline < end) end = newline;
                if (end > start && end < plain.size() && plain[end - 1].isHighSurrogate() && plain[end].isLowSurrogate()) --end;
                const auto text = plain.mid(start, end - start);
                QString content = projection->rich ? FragmentHtml(*document, start, end) : text;
                if (content.size() > SOURCE_ROW_CHARS) { content = text.toHtmlEscaped(); projection->truncated = true; }
                const qsizetype bytes = sizeof(Row) + 2 * (content.size() + text.size() + source.left.size() + source.right.size());
                if (projection->bytes + bytes > PROJECTION_BYTES || projection->rows.size() + chunks.size() >= OUTPUT_ROWS) { projection->truncated = true; break; }
                projection->bytes += bytes;
                chunks.append({std::move(content), text, start == 0 ? source.left : QString{}, start == 0 ? source.right : QString{}, source.category,
                    start == 0 ? source.continuation : plain[start - 1] != QLatin1Char('\n')});
                start = end + (newline == end ? 1 : 0);
                if (start == end && end == plain.size()) break;
            } while (start <= plain.size());
            for (auto it = chunks.crbegin(); it != chunks.crend(); ++it) projection->rows.append(*it);
        }
        if (final) std::reverse(projection->rows.begin(), projection->rows.end());
        return final;
    }, [mailbox, projection, observer, post = m_services.post](bool done) {
        if (projection->cancelled) return;
        if (done) mailbox->publish({projection->generation, std::make_shared<const Rows>(std::move(projection->rows)), projection->truncated, {}});
        else {
            const auto next = [observer, generation = projection->generation] {
                if (observer && observer->m_projection && observer->m_projection->generation == generation)
                    observer->captureNext(observer->m_projection);
            };
            post(next);
        }
    }, [mailbox, projection](std::exception_ptr) { mailbox->publish({projection->generation, {}, false, tr("Output could not be prepared.")}); },
    {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    if (!accepted) { m_busy = false; m_error = tr("Output could not be queued."); Q_EMIT changed(); }
}

void MonospaceOutputModel::publish()
{
    // Take one publication kind per GUI turn. Unprocessed large values stay
    // mailbox-owned across reentrant Qt signals and are tracked by the services.
    const auto mailbox = m_mailbox;
    std::optional<Mailbox::Publication> publication;
    std::optional<Mailbox::SearchPublication> result;
    std::optional<Mailbox::CopyPublication> copy;
    {
        std::lock_guard lock{mailbox->mutex}; mailbox->notice = false;
        if (mailbox->publication) publication = std::exchange(mailbox->publication, {});
        else if (mailbox->search) result = std::exchange(mailbox->search, {});
        else copy = std::exchange(mailbox->copy, {});
    }
    if (m_closed || !m_active) {
        if (publication) mailbox->retire(std::move(publication->rows));
        if (result) mailbox->retire(std::move(result->matches));
        if (copy) mailbox->retire(std::move(copy->text));
        return;
    }
    const QPointer<MonospaceOutputModel> observer{this};
    if (publication) {
        if (publication->generation != m_generation) retire(std::move(publication->rows));
        else {
            m_busy = false; m_error = publication->error; m_truncated = publication->truncated;
            if (publication->rows) {
                beginResetModel();
                if (!observer) { mailbox->retire(std::move(publication->rows)); return; }
                if (m_closed) { endResetModel(); mailbox->retire(std::move(publication->rows)); return; }
                retire(std::exchange(m_rows, std::move(publication->rows))); endResetModel();
                if (!observer || m_closed) return;
                Q_EMIT countChanged(); if (!observer || m_closed) return;
                search(false); if (!observer || m_closed) return;
            }
            Q_EMIT changed(); if (!observer || m_closed) return;
        }
    }
    if (result) {
        if (result->revision != m_search_revision) retire(std::move(result->matches));
        else {
            retire(std::exchange(m_matches, std::move(result->matches)));
            m_search_busy = false; m_search_limited = result->limited;
            m_match_index = searchResultCount() == 0 ? -1 : result->reset || m_match_index < 0 ? 0 : std::min(m_match_index, searchResultCount() - 1);
            Q_EMIT searchChanged(); if (!observer || m_closed) return;
            Q_EMIT currentMatchChanged(); if (!observer || m_closed) return;
        }
    }
    if (copy) {
        const bool copied = copy->generation == m_generation && bool(copy->text) && QGuiApplication::clipboard();
        if (copied) QGuiApplication::clipboard()->setText(*copy->text);
        // Clipboard signals may destroy the view. The local mailbox still owns
        // the retirement transport, so this transfer needs no surviving QObject.
        mailbox->retire(std::move(copy->text));
        if (!observer || m_closed) return;
        if (!copied) { m_error = tr("Output could not be copied."); Q_EMIT changed(); if (!observer || m_closed) return; }
        Q_EMIT copyFinished(copied); if (!observer || m_closed) return;
    }
    mailbox->retry();
    bool remaining{false};
    { std::lock_guard lock{mailbox->mutex}; remaining = mailbox->publication || mailbox->search || mailbox->copy; }
    if (remaining) mailbox->wake();
    if (m_waiting_retirement && mailbox->pending() <= 8) { m_waiting_retirement = false; refresh(); }
}

void MonospaceOutputModel::setSearchText(const QString& text)
{
    const auto bounded = text.left(256);
    if (m_search_text == bounded) return;
    m_search_text = bounded; search(true); Q_EMIT searchChanged();
}

void MonospaceOutputModel::search(bool reset)
{
    if (m_search) { m_search->cancelled = true; retire(std::exchange(m_search, {})); }
    ++m_search_revision;
    if (m_closed || !m_active || m_search_text.isEmpty() || !m_rows) {
        retire(std::exchange(m_matches, {})); m_match_index = -1; m_search_busy = m_search_limited = false;
        Q_EMIT searchChanged(); Q_EMIT currentMatchChanged(); return;
    }
    auto work = std::make_shared<Search>();
    work->revision = m_search_revision; work->text = m_search_text; work->rows = m_rows; work->reset = reset;
    m_search = work; m_search_busy = true;
    stepSearch(work);
}

void MonospaceOutputModel::stepSearch(const std::shared_ptr<Search>& work)
{
    const auto mailbox = m_mailbox;
    const auto scheduler = m_services.scheduler;
    const QPointer<MonospaceOutputModel> observer{this};
    const auto accepted = execution::Submit(*scheduler, [work] {
        for (int n = 0; !work->cancelled && n < SEARCH_ROWS && work->row < work->rows->size(); ++n, ++work->row) {
            const auto& text = work->rows->at(work->row).plain;
            int start{0};
            while ((start = text.indexOf(work->text, start, Qt::CaseInsensitive)) >= 0) {
                if (work->matches->size() >= SEARCH_MATCHES) { work->limited = true; return true; }
                work->matches->append({work->row, start, start + int(work->text.size())});
                start += work->text.size();
            }
        }
        return work->row >= work->rows->size();
    }, [mailbox, observer, work, post = m_services.post](bool done) {
        if (work->cancelled) return;
        if (done) mailbox->publishSearch({work->revision, work->matches, work->limited, work->reset});
        else {
            const auto next = [observer, revision = work->revision] {
                if (observer && observer->m_search && observer->m_search->revision == revision)
                    observer->stepSearch(observer->m_search);
            };
            post(next);
        }
    }, [mailbox, work](std::exception_ptr) { mailbox->publishSearch({work->revision, std::make_shared<const Matches>(), true, work->reset}); },
    {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    if (!accepted) { m_search_busy = false; m_search_limited = true; Q_EMIT searchChanged(); }
}

void MonospaceOutputModel::copyAll()
{
    if (m_closed || !m_active || !m_rows || !m_services.scheduler) { Q_EMIT copyFinished(false); return; }
    const auto mailbox = m_mailbox;
    const auto generation = m_generation;
    const auto admission = execution::Submit(*m_services.scheduler, [rows = m_rows] {
        QString text;
        for (int row = 0; row < rows->size(); ++row) {
            const auto& value = rows->at(row);
            if (row && !value.continuation) text += QLatin1Char('\n');
            text += value.plain;
        }
        return std::make_shared<const QString>(std::move(text));
    }, [mailbox, generation](std::shared_ptr<const QString> text) { mailbox->publishCopy({generation, std::move(text)}); },
    [mailbox, generation](std::exception_ptr) { mailbox->publishCopy({generation, {}}); },
    {execution::Lane::Domain, {}, execution::WorkClass::Action});
    if (!admission) Q_EMIT copyFinished(false);
}

int MonospaceOutputModel::searchResultCount() const { return m_matches ? m_matches->size() : 0; }
int MonospaceOutputModel::currentSearchRow() const { return m_matches && m_match_index >= 0 ? m_matches->at(m_match_index).row : -1; }
int MonospaceOutputModel::currentSearchStart() const { return m_matches && m_match_index >= 0 ? m_matches->at(m_match_index).start : -1; }
int MonospaceOutputModel::currentSearchEnd() const { return m_matches && m_match_index >= 0 ? m_matches->at(m_match_index).end : -1; }
void MonospaceOutputModel::setMatchIndex(int index) { m_match_index = index; Q_EMIT currentMatchChanged(); }
void MonospaceOutputModel::nextSearchResult() { if (searchResultCount()) setMatchIndex((m_match_index + 1) % searchResultCount()); }
void MonospaceOutputModel::previousSearchResult() { if (searchResultCount()) setMatchIndex((m_match_index + searchResultCount() - 1) % searchResultCount()); }
