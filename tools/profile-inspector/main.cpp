// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/profileinspection.h>

#include <chainparams.h>
#include <compat/compat.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util/translation.h>
#include <univalue.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

const TranslateFn G_TRANSLATION_FUN{};

MAIN_FUNCTION
{
    // QtCore preserves native Unicode arguments on Windows. No QGuiApplication,
    // event loop, node initialization, wallet loading or logging is started.
    QCoreApplication application{argc, argv};
    const auto arguments = application.arguments();
    QString tag;
    const auto reply = [&](bool success, bool has_wallet_data, const QString& error) {
        QJsonObject object{{QStringLiteral("protocol"), 1}, {QStringLiteral("request_tag"), tag}, {QStringLiteral("ok"), success}};
        if (success) object.insert(QStringLiteral("has_wallet_data"), has_wallet_data);
        else object.insert(QStringLiteral("error"), error.left(512));
        const QByteArray output = QJsonDocument{object}.toJson(QJsonDocument::Compact) + '\n';
        return std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout) == static_cast<size_t>(output.size());
    };
    try {
        if (arguments.size() != 9 && arguments.size() != 11) throw std::runtime_error{"Invalid wallet directory helper argument count."};
        if (arguments[1] != QStringLiteral("--protocol") || arguments[2] != QStringLiteral("1")
            || arguments[3] != QStringLiteral("--request-tag") || arguments[5] != QStringLiteral("--walletdir")
            || arguments[7] != QStringLiteral("--chain")) throw std::runtime_error{"Invalid wallet directory helper protocol."};
        tag = arguments[4];
        const auto network = ChainTypeFromString(arguments[8].toStdString());
        if (!network) throw std::runtime_error{"Unsupported wallet directory network."};
        profileinspection::Request request{arguments[6], *network, {}};
        if (arguments.size() == 11) {
            if (arguments[9] != QStringLiteral("--signetchallenge")) throw std::runtime_error{"Invalid wallet directory network override."};
            request.signet_challenge = arguments[10];
        }
        if (const auto error = profileinspection::Validate(request); !error.isEmpty()) throw std::runtime_error{error.toStdString()};
        if (tag != profileinspection::Tag(request)) throw std::runtime_error{"Wallet directory request tag mismatch."};
        auto init = interfaces::MakeGuiInit(argc, argv);
        if (!init) throw std::runtime_error{"Public local initialization is unavailable."};
        auto node = init->makeNode();
        if (!node) throw std::runtime_error{"Public node configuration is unavailable."};
        node->forceSetting("walletdir", UniValue{request.directory.toStdString()});
        if (request.signet_challenge) node->forceSetting("signetchallenge", UniValue{request.signet_challenge->toStdString()});
        // Public startup glue selects the exact immutable proposed network.
        // This child's forced settings and Params never touch its parent.
        SelectParams(request.network);
        auto chain = init->makeChain();
        if (!chain) throw std::runtime_error{"Public chain configuration is unavailable."};
        auto loader = init->makeWalletLoader(*chain);
        if (!loader) throw std::runtime_error{"Public wallet directory inspection is unavailable."};
        if (loader->getWalletDir() != request.directory.toStdString()) throw std::runtime_error{"The requested wallet directory is unavailable."};
        const bool present = !loader->listWalletDir().empty();
        return reply(true, present, {}) ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        reply(false, false, QString::fromUtf8(error.what()));
    } catch (...) {
        reply(false, false, QStringLiteral("Wallet directory inspection failed."));
    }
    return EXIT_FAILURE;
}
