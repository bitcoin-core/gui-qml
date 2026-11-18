// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTWORK_H
#define BITCOIN_QML_PSBTWORK_H

#include <common/messages.h>
#include <qml/psbtserialization.h>
#include <qml/psbtworktypes.h>
#include <qml/models/psbtfeeservices.h>
#include <qml/walletreview.h>
#include <qml/secretinput.h>
#include <support/allocators/secure.h>
#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace interfaces { class Wallet; class Node; }
namespace psbtwork {
struct BroadcastResult {
    bool accepted{false};
    QString error;
};
// Composition retains Node until the owning scheduler drains. The callable is
// invoked only on Core; it never looks up a GUI facade or selected wallet.
using Broadcast = std::function<BroadcastResult(CTransactionRef)>;
Broadcast NodeBroadcast(interfaces::Node* node);
// Summary pages are deterministic projections of Native-captured immutable
// values. Capture calls the public Wallet ownership query and must run on Core;
// page formatting is pure and may run on the Domain lane.
std::shared_ptr<const SummarySource> CaptureSummarySource(
    std::shared_ptr<const PartiallySignedTransaction> assessment,
    std::shared_ptr<const psbtfee::Analysis> proof,
    interfaces::Wallet* wallet);
SummaryPage ProjectSummaryPage(std::shared_ptr<const SummarySource> source, int offset, int count,
                               quint64 generation = 0, quint64 request = 0);
// Credential materialization is a Native effect. Keep its failure independent
// of wallet unlock/signing and injectable without exhausting process memory.
using Materialize = std::function<SecureString(const secretinput::Captured&)>;
struct Context {
    psbtfee::Lookup lookup;
    Broadcast broadcast;
    Materialize materialize{secretinput::Materialize};
    psbtpolicy::Read policy{};
};
struct Request {
    Kind kind{Kind::Inspect};
    std::shared_ptr<const PartiallySignedTransaction> document;
    // Exact immutable revision which supplied the published capabilities.
    // Export continues to use document; submission never repeats inspection.
    std::shared_ptr<const PartiallySignedTransaction> assessment;
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
    QString path, status;
    std::optional<SecureString> passphrase; // Native/test callers only.
    secretinput::Captured captured_passphrase;
};
// All helpers below are worker/test-setup utilities. Callers retain the original
// Session Action/borrow throughout effects, outcomes and native result release.
Result Execute(interfaces::Wallet* wallet, Request request, const Context& context);
QString LocalFilePath(const QString& path);
// Narrow Native I/O port. LoadFile keeps the default QFile on the worker stack
// and delegates its policy to this driver.
class FileReader
{
public:
    virtual ~FileReader() = default;
    virtual bool open() = 0;
    virtual qint64 initialSize() = 0;
    virtual QByteArray read(qint64 maximum) = 0;
    virtual bool hasError() = 0;
    virtual QString errorString() = 0;
};
QString ReadFile(FileReader& reader, PartiallySignedTransaction& document);
QString LoadFile(const QString& path, PartiallySignedTransaction& document);
QString ErrorText(common::PSBTError error);
std::optional<std::pair<int, int>> Multisig(const PartiallySignedTransaction& document, size_t input);
struct DocumentResult { std::shared_ptr<const PartiallySignedTransaction> document; QString error; };
DocumentResult ExportReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> document,
                            CTransactionRef reviewed, bool fill_metadata,
                            std::shared_ptr<const psbtfee::Analysis> proof = {}, std::optional<CAmount> approved_fee = {});
QVariantMap CollectReviewFlow(interfaces::Wallet& wallet, CTransactionRef transaction,
    std::shared_ptr<const PartiallySignedTransaction> document = {},
    std::shared_ptr<const psbtfee::Analysis> proof = {}, const QSet<QString>& recipients = {}, int display_unit = 0,
    const QMap<QString, QString>& reviewed_notes = {});
struct ImportResult {
    enum class Disposition { Review, Known, Unsupported };
    Disposition disposition{Disposition::Unsupported};
    std::shared_ptr<const walletreview::Snapshot> review;
    std::shared_ptr<const PartiallySignedTransaction> document;
    QString message, txid;
    bool key_can_send{false}, can_commit_complete{false}, key_can_broadcast{false};
};
ImportResult PrepareReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> document,
                          const Context& context, int display_unit = 0);
} // namespace psbtwork
#endif // BITCOIN_QML_PSBTWORK_H
