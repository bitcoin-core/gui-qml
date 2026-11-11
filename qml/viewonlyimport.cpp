// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/viewonlyimport.h>

#include <key_io.h>
#include <pubkey.h>
#include <qml/util.h>
#include <script/descriptor.h>
#include <script/signingprovider.h>
#include <univalue.h>
#include <util/string.h>

#include <QDateTime>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <exception>
#include <optional>
#include <set>
#include <string>

using wallet::ImportDescriptorRequest;

namespace {

struct DescriptorCheck {
    QString error;
    std::string descriptor;
    std::vector<OutputType> types;
    size_t count{0};
    bool ranged{false};
    bool change_chain{false};
};

// A comma would mean more keys or a script tree.
const QRegularExpression SINGLE_KEY_SCRIPT{QStringLiteral(R"(^(pkh|wpkh|sh\(wpkh|tr)\([^,]*$)")};
const QRegularExpression CHANGE_CHAIN{QStringLiteral(R"(/1/\*\))")};

// Parse derives public keys from private ones, which needs the signing
// context, so private keys are found before parsing.
bool HasPrivateKey(const std::string& descriptor)
{
    return std::ranges::any_of(util::SplitString(descriptor, "()[],/#<>;*'"), [](const std::string& token) {
        return DecodeExtKey(token).key.IsValid() || DecodeSecret(token).IsValid();
    });
}

DescriptorCheck CheckDescriptor(const std::string& descriptor)
{
    DescriptorCheck check;
    if (HasPrivateKey(descriptor)) {
        check.error = QObject::tr("This includes a private key. Use the public descriptor instead.");
        return check;
    }
    FlatSigningProvider keys;
    std::string error;
    const auto parsed{Parse(descriptor, keys, error, /*require_checksum=*/false)};
    if (parsed.empty()) {
        check.error = QString::fromStdString(error);
        return check;
    }
    if (!keys.keys.empty()) {
        check.error = QObject::tr("This includes a private key. Use the public descriptor instead.");
        return check;
    }
    for (const auto& desc : parsed) {
        std::set<CPubKey> pubkeys;
        std::set<CExtPubKey> ext_pubs;
        desc->GetPubKeys(pubkeys, ext_pubs);
        const std::optional<OutputType> type{desc->GetOutputType()};
        if (!type || pubkeys.size() + ext_pubs.size() != 1 ||
            !SINGLE_KEY_SCRIPT.match(QString::fromStdString(desc->ToString())).hasMatch()) {
            check.error = QObject::tr("Only single-key descriptors can be imported: pkh, wpkh, sh(wpkh), or tr without scripts.");
            return check;
        }
        if (std::ranges::find(check.types, *type) == check.types.end()) check.types.push_back(*type);
    }
    check.count = parsed.size();
    check.ranged = parsed.front()->IsRange();
    check.change_chain = parsed.size() == 1 &&
                         CHANGE_CHAIN.match(QString::fromStdString(parsed.front()->ToString())).hasMatch();
    // The wallet's importer requires a checksum.
    check.descriptor = descriptor.find('#') == std::string::npos
        ? descriptor + "#" + GetDescriptorChecksum(descriptor)
        : descriptor;
    return check;
}

void AddTypes(ViewOnlyImport& result, const DescriptorCheck& check)
{
    for (const OutputType type : check.types) {
        if (std::ranges::find(result.output_types, type) == result.output_types.end()) {
            result.output_types.push_back(type);
        }
    }
    result.descriptor_count += check.count;
}

ViewOnlyImport Failed(ViewOnlyImport::Kind kind, const QString& error)
{
    ViewOnlyImport result;
    result.kind = kind;
    result.error = error;
    return result;
}

std::optional<std::pair<int64_t, int64_t>> ReadRange(const UniValue& value)
{
    if (value.isNum()) return std::pair<int64_t, int64_t>{0, value.getInt<int64_t>()};
    if (value.isArray() && value.size() == 2 && value[0].isNum() && value[1].isNum()) {
        return std::pair<int64_t, int64_t>{value[0].getInt<int64_t>(), value[1].getInt<int64_t>()};
    }
    return std::nullopt;
}

ViewOnlyImport ParseJson(const std::string& text)
{
    using Kind = ViewOnlyImport::Kind;
    UniValue root;
    if (!root.read(text)) return Failed(Kind::DescriptorJson, QObject::tr("This is not valid JSON."));
    // listdescriptors wraps the list in an object.
    const UniValue& list{root.isObject() ? root.find_value("descriptors") : root};
    if (!list.isArray() || list.empty()) {
        return Failed(Kind::DescriptorJson, QObject::tr("Expected a list of descriptors, like the one listdescriptors prints."));
    }

    ViewOnlyImport result;
    result.kind = Kind::DescriptorJson;
    for (size_t i = 0; i < list.size(); ++i) {
        //: %1 is the position of a descriptor in a pasted list.
        const QString position{QObject::tr("Descriptor %1").arg(i + 1)};
        try {
            const UniValue& entry{list[i]};
            const UniValue& desc{entry.isObject() ? entry.find_value("desc") : NullUniValue};
            if (!desc.isStr()) return Failed(Kind::DescriptorJson, QObject::tr("%1 has no \"desc\" text.").arg(position));
            const DescriptorCheck check{CheckDescriptor(desc.get_str())};
            if (!check.error.isEmpty()) return Failed(Kind::DescriptorJson, QStringLiteral("%1: %2").arg(position, check.error));

            ImportDescriptorRequest request;
            request.descriptor = check.descriptor;
            const UniValue& timestamp{entry.find_value("timestamp")};
            if (timestamp.isNull()) {
                result.undated.push_back(result.requests.size());
            } else if (timestamp.isNum() && timestamp.getInt<int64_t>() >= 0) {
                request.timestamp = timestamp.getInt<int64_t>();
            } else if (!timestamp.isStr() || timestamp.get_str() != "now") {
                return Failed(Kind::DescriptorJson, QObject::tr("%1 has an invalid timestamp.").arg(position));
            }
            if (const UniValue& active{entry.find_value("active")}; !active.isNull()) request.active = active.get_bool();
            if (const UniValue& internal{entry.find_value("internal")}; !internal.isNull()) request.internal = internal.get_bool();
            if (const UniValue& label{entry.find_value("label")}; !label.isNull()) request.label = label.get_str();
            if (const UniValue& range{entry.find_value("range")}; !range.isNull()) {
                request.range = ReadRange(range);
                if (!request.range) return Failed(Kind::DescriptorJson, QObject::tr("%1 has an invalid range.").arg(position));
                if (const auto bounds{CheckDescriptorRangeBounds(request.range->first, request.range->second)}; !bounds) {
                    return Failed(Kind::DescriptorJson, QStringLiteral("%1: %2").arg(position, QString::fromStdString(bounds.error())));
                }
            }
            const UniValue& next_index{entry.find_value("next_index")};
            const UniValue& next{next_index.isNull() ? entry.find_value("next") : next_index};
            if (!next.isNull()) request.next_index = next.getInt<int64_t>();

            result.requests.push_back(std::move(request));
            AddTypes(result, check);
        } catch (const std::exception&) {
            return Failed(Kind::DescriptorJson, QObject::tr("%1 has a field of the wrong type.").arg(position));
        }
    }
    if (std::ranges::none_of(result.requests, &ImportDescriptorRequest::active)) {
        return Failed(Kind::DescriptorJson, QObject::tr("None of these descriptors is active, so the wallet could not give out addresses."));
    }
    return result;
}

ViewOnlyImport ParseLines(const QStringList& lines)
{
    using Kind = ViewOnlyImport::Kind;
    ViewOnlyImport result;
    result.kind = Kind::Descriptors;
    const qsizetype filled{std::ranges::count_if(lines, [](const QString& line) { return !line.trimmed().isEmpty(); })};
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line{lines[i].trimmed()};
        if (line.isEmpty()) continue;
        if (filled == 1 && !line.contains(QLatin1Char('('))) {
            return Failed(Kind::Descriptors, QObject::tr("Enter descriptors, or an extended public key (xpub)."));
        }
        const DescriptorCheck check{CheckDescriptor(line.toStdString())};
        if (!check.error.isEmpty()) {
            //: %1 is a line number, %2 explains what is wrong with that line.
            return Failed(Kind::Descriptors, filled == 1 ? check.error : QObject::tr("Line %1: %2").arg(i + 1).arg(check.error));
        }
        ImportDescriptorRequest request;
        request.descriptor = check.descriptor;
        request.active = check.ranged;
        // A multipath descriptor sets receive and change itself.
        if (check.ranged && check.count == 1) request.internal = check.change_chain;
        result.undated.push_back(result.requests.size());
        result.requests.push_back(std::move(request));
        AddTypes(result, check);
    }
    if (std::ranges::none_of(result.requests, &ImportDescriptorRequest::active)) {
        return Failed(Kind::Descriptors, QObject::tr("Add a ranged descriptor, one ending in /*, so the wallet can give out addresses."));
    }
    return result;
}

} // namespace

ViewOnlyImport ParseViewOnlyImport(const QString& text)
{
    using Kind = ViewOnlyImport::Kind;
    const QString trimmed{text.trimmed()};
    if (trimmed.isEmpty()) return {};
    if (trimmed.startsWith(QLatin1Char('[')) || trimmed.startsWith(QLatin1Char('{'))) {
        return ParseJson(trimmed.toStdString());
    }

    const std::string key{trimmed.toStdString()};
    if (DecodeExtKey(key).key.IsValid()) {
        return Failed(Kind::ExtendedKey, QObject::tr("This is a private key. Enter the extended public key instead."));
    }
    if (DecodeExtPubKey(key).pubkey.IsValid()) {
        // A bare key has no script type. Keep the native SegWit default.
        ViewOnlyImport result{ParseLines({QStringLiteral("wpkh(%1/<0;1>/*)").arg(trimmed)})};
        result.kind = Kind::ExtendedKey;
        return result;
    }
    return ParseLines(trimmed.split(QLatin1Char('\n')));
}

QVariantMap DescribeViewOnlyImport(const QString& text)
{
    using Kind = ViewOnlyImport::Kind;
    const ViewOnlyImport parsed{ParseViewOnlyImport(text)};
    QVariantMap result;
    if (parsed.kind == Kind::Empty) {
        result.insert(QStringLiteral("status"), QStringLiteral("empty"));
        return result;
    }
    const QString kind{parsed.kind == Kind::ExtendedKey ? QStringLiteral("extendedKey")
                       : parsed.kind == Kind::Descriptors ? QStringLiteral("descriptors")
                       : QStringLiteral("descriptorJson")};
    result.insert(QStringLiteral("kind"), kind);
    if (!parsed.error.isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("invalid"));
        result.insert(QStringLiteral("error"), parsed.error);
        return result;
    }

    QStringList address_types;
    for (const OutputType type : parsed.output_types) address_types.append(QmlUtil::OutputTypeLabel(type));
    std::optional<int64_t> created;
    for (size_t i = 0; i < parsed.requests.size(); ++i) {
        if (std::ranges::find(parsed.undated, i) != parsed.undated.end()) continue;
        // An unset timestamp here was "now".
        const int64_t time{parsed.requests[i].timestamp.value_or(QDateTime::currentSecsSinceEpoch())};
        created = std::min(created.value_or(time), time);
    }
    result.insert(QStringLiteral("status"), QStringLiteral("valid"));
    result.insert(QStringLiteral("descriptorCount"), qulonglong(parsed.descriptor_count));
    result.insert(QStringLiteral("addressTypes"), address_types);
    result.insert(QStringLiteral("needsBirthDate"), !parsed.undated.empty());
    result.insert(QStringLiteral("createdTime"), qlonglong(created.value_or(-1)));
    return result;
}
