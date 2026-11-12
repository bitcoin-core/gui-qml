// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/viewonlyimport.h>

#include <base58.h>
#include <chainparams.h>
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
#include <QVariantList>

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
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
    ViewOnlyImport::Source source;
};

// A comma would mean more keys or a script tree.
const QRegularExpression SINGLE_KEY_SCRIPT{QStringLiteral(R"(^(pkh|wpkh|sh\(wpkh|tr)\([^,]*$)")};
const QRegularExpression CHANGE_CHAIN{QStringLiteral(R"(/1/\*\))")};
// The key inside the innermost script, with its origin if it has one.
const QRegularExpression SCRIPT_KEY{QStringLiteral(R"(\((\[[0-9a-fA-F]{8}[^\]]*\])?([^()\[\]/#,]+)(?=[/)]))")};

QString KeyLabel(const std::string& descriptor)
{
    const QRegularExpressionMatch match{SCRIPT_KEY.match(QString::fromStdString(descriptor))};
    return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
}

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
    check.source = {check.types.front(), KeyLabel(descriptor), check.ranged, parsed.size() > 1};
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
            result.sources.push_back(check.source);
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
            ViewOnlyImport failed{Failed(Kind::Descriptors, filled == 1 ? check.error : QObject::tr("Line %1: %2").arg(i + 1).arg(check.error))};
            failed.error_line = int(i);
            return failed;
        }
        ImportDescriptorRequest request;
        request.descriptor = check.descriptor;
        request.active = check.ranged;
        // A multipath descriptor sets receive and change itself.
        if (check.ranged && check.count == 1) request.internal = check.change_chain;
        result.undated.push_back(result.requests.size());
        result.requests.push_back(std::move(request));
        result.sources.push_back(check.source);
        AddTypes(result, check);
    }
    if (std::ranges::none_of(result.requests, &ImportDescriptorRequest::active)) {
        return Failed(Kind::Descriptors, QObject::tr("Add a ranged descriptor, one ending in /*, so the wallet can give out addresses."));
    }
    return result;
}

using KeyVersion = std::array<unsigned char, 4>;

struct KnownKeyVersion {
    KeyVersion version;
    //! Testnet, signet and regtest share these.
    bool test;
    bool is_private;
    bool multisig;
    std::optional<OutputType> type;
};

// BIP32 and SLIP-132 versions. The prefix of a ypub or zpub names its script type.
const std::array KNOWN_KEY_VERSIONS{
    KnownKeyVersion{{0x04, 0x88, 0xb2, 0x1e}, false, false, false, std::nullopt},             // xpub
    KnownKeyVersion{{0x04, 0x9d, 0x7c, 0xb2}, false, false, false, OutputType::P2SH_SEGWIT},  // ypub
    KnownKeyVersion{{0x04, 0xb2, 0x47, 0x46}, false, false, false, OutputType::BECH32},       // zpub
    KnownKeyVersion{{0x02, 0x95, 0xb4, 0x3f}, false, false, true, std::nullopt},              // Ypub
    KnownKeyVersion{{0x02, 0xaa, 0x7e, 0xd3}, false, false, true, std::nullopt},              // Zpub
    KnownKeyVersion{{0x04, 0x35, 0x87, 0xcf}, true, false, false, std::nullopt},              // tpub
    KnownKeyVersion{{0x04, 0x4a, 0x52, 0x62}, true, false, false, OutputType::P2SH_SEGWIT},   // upub
    KnownKeyVersion{{0x04, 0x5f, 0x1c, 0xf6}, true, false, false, OutputType::BECH32},        // vpub
    KnownKeyVersion{{0x02, 0x42, 0x89, 0xef}, true, false, true, std::nullopt},               // Upub
    KnownKeyVersion{{0x02, 0x57, 0x54, 0x83}, true, false, true, std::nullopt},               // Vpub
    KnownKeyVersion{{0x04, 0x88, 0xad, 0xe4}, false, true, false, std::nullopt},              // xprv
    KnownKeyVersion{{0x04, 0x9d, 0x78, 0x78}, false, true, false, std::nullopt},              // yprv
    KnownKeyVersion{{0x04, 0xb2, 0x43, 0x0c}, false, true, false, std::nullopt},              // zprv
    KnownKeyVersion{{0x02, 0x95, 0xb0, 0x05}, false, true, true, std::nullopt},               // Yprv
    KnownKeyVersion{{0x02, 0xaa, 0x7a, 0x99}, false, true, true, std::nullopt},               // Zprv
    KnownKeyVersion{{0x04, 0x35, 0x83, 0x94}, true, true, false, std::nullopt},               // tprv
    KnownKeyVersion{{0x04, 0x4a, 0x4e, 0x28}, true, true, false, std::nullopt},               // uprv
    KnownKeyVersion{{0x04, 0x5f, 0x18, 0xbc}, true, true, false, std::nullopt},               // vprv
    KnownKeyVersion{{0x02, 0x42, 0x85, 0xb5}, true, true, true, std::nullopt},                // Uprv
    KnownKeyVersion{{0x02, 0x57, 0x50, 0x48}, true, true, true, std::nullopt},                // Vprv
};

// [fingerprint/path]key, the key origin form wallets export.
const QRegularExpression KEY_ORIGIN{QStringLiteral(R"(^\[([0-9a-fA-F]{8})((?:/[0-9]+['hH]?)*)\]([1-9A-HJ-NP-Za-km-z]+)$)")};
const QRegularExpression ORIGIN_PURPOSE{QStringLiteral(R"(^/(44|49|84|86)['hH])")};

struct ExtendedKey {
    bool recognized{false};
    QString error;
    //! The key with this network's xpub or tpub version.
    std::string key;
    std::optional<OutputType> type;
};

ExtendedKey ReadExtendedKey(const std::string& text)
{
    std::vector<unsigned char> data;
    if (!DecodeBase58Check(text, data, BIP32_EXTKEY_WITH_VERSION_SIZE) || data.size() != BIP32_EXTKEY_WITH_VERSION_SIZE) return {};
    const auto known{std::ranges::find_if(KNOWN_KEY_VERSIONS, [&](const KnownKeyVersion& known) {
        return std::equal(known.version.begin(), known.version.end(), data.begin());
    })};
    if (known == KNOWN_KEY_VERSIONS.end()) return {};

    ExtendedKey result;
    result.recognized = true;
    if (known->is_private) {
        result.error = QObject::tr("This is a private key. Enter the extended public key instead.");
        return result;
    }
    if (known->multisig) {
        result.error = QObject::tr("This key is from a multisig wallet. Only single-key wallets can be imported.");
        return result;
    }
    const std::vector<unsigned char>& prefix{Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY)};
    const KeyVersion& mainnet{KNOWN_KEY_VERSIONS.front().version};
    if (known->test == std::equal(mainnet.begin(), mainnet.end(), prefix.begin(), prefix.end())) {
        result.error = QObject::tr("This key is for a different network.");
        return result;
    }
    std::copy(prefix.begin(), prefix.end(), data.begin());
    result.key = EncodeBase58Check(data);
    if (!DecodeExtPubKey(result.key).pubkey.IsValid()) {
        result.error = QObject::tr("This extended public key is not valid.");
        return result;
    }
    result.type = known->type;
    return result;
}

std::optional<OutputType> PurposeType(const QString& path)
{
    const QRegularExpressionMatch match{ORIGIN_PURPOSE.match(path)};
    if (!match.hasMatch()) return std::nullopt;
    switch (match.captured(1).toInt()) {
    case 44: return OutputType::LEGACY;
    case 49: return OutputType::P2SH_SEGWIT;
    case 84: return OutputType::BECH32;
    case 86: return OutputType::BECH32M;
    }
    return std::nullopt;
}

QString WrapKey(OutputType type, const QString& key)
{
    switch (type) {
    case OutputType::LEGACY: return QStringLiteral("pkh(%1)").arg(key);
    case OutputType::P2SH_SEGWIT: return QStringLiteral("sh(wpkh(%1))").arg(key);
    case OutputType::BECH32: return QStringLiteral("wpkh(%1)").arg(key);
    case OutputType::BECH32M: return QStringLiteral("tr(%1)").arg(key);
    case OutputType::UNKNOWN: break;
    }
    return {};
}

ViewOnlyImport ParseExtendedKey(const ExtendedKey& key, const QString& origin, const QString& path, const QString& address_type)
{
    using Kind = ViewOnlyImport::Kind;
    if (!key.error.isEmpty()) return Failed(Kind::ExtendedKey, key.error);
    // The key's prefix wins, then a BIP44, 49, 84 or 86 path.
    std::optional<OutputType> type{key.type ? key.type : PurposeType(path)};
    const bool needs_address_type{!type};
    if (!type && !address_type.isEmpty()) {
        type = ParseOutputType(address_type.toStdString());
        if (!type || *type == OutputType::UNKNOWN) return Failed(Kind::ExtendedKey, QObject::tr("Choose a valid address type."));
    }
    ViewOnlyImport result;
    if (type) result = ParseLines({WrapKey(*type, origin + QString::fromStdString(key.key) + QStringLiteral("/<0;1>/*"))});
    result.kind = Kind::ExtendedKey;
    result.needs_address_type = needs_address_type;
    return result;
}

} // namespace

ViewOnlyImport ParseViewOnlyImport(const QString& text, const QString& address_type)
{
    const QString trimmed{text.trimmed()};
    if (trimmed.isEmpty()) return {};

    const QRegularExpressionMatch origin{KEY_ORIGIN.match(trimmed)};
    const QString key_text{origin.hasMatch() ? origin.captured(3) : trimmed};
    if (const ExtendedKey key{ReadExtendedKey(key_text.toStdString())}; key.recognized) {
        const QString origin_text{origin.hasMatch() ? QStringLiteral("[%1%2]").arg(origin.captured(1), origin.captured(2)) : QString{}};
        return ParseExtendedKey(key, origin_text, origin.captured(2), address_type);
    }
    if (trimmed.startsWith(QLatin1Char('[')) || trimmed.startsWith(QLatin1Char('{'))) {
        return ParseJson(trimmed.toStdString());
    }
    return ParseLines(text.split(QLatin1Char('\n')));
}

QVariantMap DescribeViewOnlyImport(const QString& text, const QString& address_type)
{
    using Kind = ViewOnlyImport::Kind;
    const ViewOnlyImport parsed{ParseViewOnlyImport(text, address_type)};
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
        result.insert(QStringLiteral("errorLine"), parsed.error_line);
        return result;
    }

    QStringList address_types;
    for (const OutputType type : parsed.output_types) address_types.append(QmlUtil::OutputTypeLabel(type));
    const int64_t now{QDateTime::currentSecsSinceEpoch()};
    std::optional<int64_t> created;
    // Receive and change of one key share a row.
    QVariantList rows;
    for (size_t i = 0; i < parsed.requests.size(); ++i) {
        const ImportDescriptorRequest& request{parsed.requests[i]};
        const ViewOnlyImport::Source& source{parsed.sources[i]};
        std::optional<int64_t> time;
        // An unset timestamp here was "now".
        if (std::ranges::find(parsed.undated, i) == parsed.undated.end()) time = request.timestamp.value_or(now);
        if (time) created = std::min(created.value_or(*time), *time);

        const QString type{QmlUtil::OutputTypeLabel(source.type)};
        const auto same_row = [&](const QVariant& row) {
            const QVariantMap map{row.toMap()};
            return map.value(QStringLiteral("addressType")) == type && map.value(QStringLiteral("key")) == source.key &&
                   map.value(QStringLiteral("active")) == request.active && map.value(QStringLiteral("ranged")) == source.ranged;
        };
        auto row_it{std::ranges::find_if(rows, same_row)};
        if (row_it == rows.end()) {
            rows.append(QVariantMap{{QStringLiteral("addressType"), type},
                                    {QStringLiteral("key"), source.key},
                                    {QStringLiteral("receive"), false},
                                    {QStringLiteral("change"), false},
                                    {QStringLiteral("active"), request.active},
                                    {QStringLiteral("ranged"), source.ranged},
                                    {QStringLiteral("createdTime"), qlonglong(-1)}});
            row_it = std::prev(rows.end());
        }
        QVariantMap row{row_it->toMap()};
        const bool change{request.internal.value_or(false)};
        if (source.multipath || !change) row.insert(QStringLiteral("receive"), true);
        if (source.multipath || change) row.insert(QStringLiteral("change"), true);
        if (time) {
            const qlonglong row_time{row.value(QStringLiteral("createdTime")).toLongLong()};
            row.insert(QStringLiteral("createdTime"), row_time < 0 ? qlonglong(*time) : std::min(row_time, qlonglong(*time)));
        }
        *row_it = row;
    }
    result.insert(QStringLiteral("status"), QStringLiteral("valid"));
    result.insert(QStringLiteral("descriptorCount"), qulonglong(parsed.descriptor_count));
    result.insert(QStringLiteral("addressTypes"), address_types);
    // A key waiting for its address type has no date either.
    result.insert(QStringLiteral("needsBirthDate"), !parsed.undated.empty() || (parsed.needs_address_type && parsed.requests.empty()));
    result.insert(QStringLiteral("createdTime"), qlonglong(created.value_or(-1)));
    result.insert(QStringLiteral("needsAddressType"), parsed.needs_address_type);
    result.insert(QStringLiteral("rows"), rows);
    if (parsed.needs_address_type) {
        QVariantList options;
        for (const OutputType type : {OutputType::BECH32, OutputType::BECH32M, OutputType::P2SH_SEGWIT, OutputType::LEGACY}) {
            options.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(FormatOutputType(type))},
                                       {QStringLiteral("label"), QmlUtil::OutputTypeLabel(type)}});
        }
        result.insert(QStringLiteral("addressTypeOptions"), options);
    }
    return result;
}
