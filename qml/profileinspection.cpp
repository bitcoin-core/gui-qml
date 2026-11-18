// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/profileinspection.h>

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <utility>

namespace profileinspection {
namespace {
constexpr qsizetype MAX_DIRECTORY_CHARACTERS{32768};
constexpr qsizetype MAX_CHALLENGE_CHARACTERS{65536};
constexpr qsizetype MAX_REPLY_BYTES{4096};
Result Invalid(QString reason)
{
    Result result;
    result.outcome = Outcome::Rejected;
    result.error = std::move(reason);
    return result;
}
}
QString Validate(const Request& request)
{
    if (request.network != ChainType::MAIN && request.network != ChainType::TESTNET && request.network != ChainType::TESTNET4
        && request.network != ChainType::SIGNET && request.network != ChainType::REGTEST) return QStringLiteral("The wallet directory network is unsupported.");
    if (request.directory.isEmpty() || request.directory.size() > MAX_DIRECTORY_CHARACTERS
        || request.directory.contains(QChar{u'\0'}) || !QDir::isAbsolutePath(request.directory)
        || QString::fromUtf8(request.directory.toUtf8()) != request.directory) {
        return QStringLiteral("The wallet directory must be an absolute path within the supported input budget.");
    }
    if (request.signet_challenge && (request.network != ChainType::SIGNET
        || request.signet_challenge->size() > MAX_CHALLENGE_CHARACTERS || request.signet_challenge->contains(QChar{u'\0'})
        || QString::fromUtf8(request.signet_challenge->toUtf8()) != *request.signet_challenge)) {
        return QStringLiteral("The wallet directory network override exceeds its supported input budget.");
    }
    return {};
}
QString Tag(const Request& request)
{
    if (!Validate(request).isEmpty()) return {};
    const QJsonObject input{{QStringLiteral("directory"), request.directory},
        {QStringLiteral("chain"), QString::fromStdString(ChainTypeToString(request.network))},
        {QStringLiteral("signet_challenge"), request.signet_challenge ? QJsonValue{*request.signet_challenge} : QJsonValue{QJsonValue::Null}}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument{input}.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QStringList Arguments(const Request& request)
{
    if (!Validate(request).isEmpty()) return {};
    QStringList args{QStringLiteral("--protocol"), QStringLiteral("1"), QStringLiteral("--request-tag"), Tag(request),
        QStringLiteral("--walletdir"), request.directory, QStringLiteral("--chain"), QString::fromStdString(ChainTypeToString(request.network))};
    if (request.signet_challenge) args << QStringLiteral("--signetchallenge") << *request.signet_challenge;
    return args;
}
Result Decode(const Request& request, const QByteArray& output)
{
    if (const auto error = Validate(request); !error.isEmpty()) return Invalid(error);
    if (output.isEmpty() || output.size() > MAX_REPLY_BYTES) return Invalid(QStringLiteral("The wallet directory helper returned an invalid response size."));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return Invalid(QStringLiteral("The wallet directory helper response could not be read."));
    const auto object = document.object();
    const auto protocol = object.value(QStringLiteral("protocol"));
    const auto tag = object.value(QStringLiteral("request_tag"));
    const auto ok = object.value(QStringLiteral("ok"));
    if (!protocol.isDouble() || protocol.toDouble() != 1.0 || !tag.isString() || tag.toString() != Tag(request)
        || !ok.isBool() || object.size() != 4) {
        return Invalid(QStringLiteral("The wallet directory helper response does not match the original request."));
    }
    Result result;
    if (ok.toBool()) {
        const auto present = object.value(QStringLiteral("has_wallet_data"));
        if (!present.isBool()) return Invalid(QStringLiteral("The wallet directory helper omitted its classification."));
        result.outcome = Outcome::Succeeded;
        result.has_wallet_data = present.toBool();
    } else {
        const auto detail = object.value(QStringLiteral("error"));
        if (!detail.isString() || detail.toString().isEmpty()) return Invalid(QStringLiteral("The wallet directory helper omitted its failure detail."));
        result.error = detail.toString().left(1024);
        result.outcome = Outcome::Failed;
    }
    return result;
}
} // namespace profileinspection
