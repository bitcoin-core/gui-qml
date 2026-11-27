// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentargs.h>
#include <common/args.h>

#include <QDir>
#include <QStringDecoder>
#include <QUrl>

namespace desktopintent {
Arguments ParseArguments(int argc, char* const argv[], const QString& producer, const QString& working_directory)
{
    Arguments result;
    const auto fail = [&] {
        result.error = QStringLiteral("Expected bounded payment requests after the application options; options after a request are not allowed.");
        result.intents.clear();
        return result;
    };
    if (argc < 1 || !argv || producer.isEmpty() || producer.size() > Queue::MAX_KEY_CHARS - 24) return fail();
    result.options_argc = argc;
    bool requests{false};
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) return fail();
        if (argv[index][0] && IsSwitchChar(argv[index][0])) {
            if (requests) return fail();
            continue;
        }
        if (!requests) { requests = true; result.options_argc = index; }
        if (result.intents.size() >= Queue::MAX_PENDING) return fail();
        size_t bytes{0};
        constexpr size_t MAX_BYTES{static_cast<size_t>(Queue::MAX_INPUT_CHARS) * 4};
        while (bytes <= MAX_BYTES && argv[index][bytes]) ++bytes;
        if (bytes > MAX_BYTES) return fail();
        QStringDecoder decoder{QStringDecoder::Utf8};
        const QString text = decoder(QByteArrayView{argv[index], static_cast<qsizetype>(bytes)});
        if (decoder.hasError() || text.isEmpty() || text.size() > Queue::MAX_INPUT_CHARS) return fail();
        Input input{producer + QLatin1Char(':') + QString::number(result.intents.size()), Kind::Text, text, Origin::Arguments};
        if (!text.startsWith(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive)) {
            const QUrl url{text, QUrl::StrictMode};
            QString path{text};
            if (text.startsWith(QLatin1String{"file:"}, Qt::CaseInsensitive)) {
                if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty()) return fail();
                path = url.toLocalFile();
            } else if (url.scheme().size() > 1) return fail();
            if (!QDir::isAbsolutePath(path)) {
                if (!QDir::isAbsolutePath(working_directory)) return fail();
                path = QDir{working_directory}.absoluteFilePath(path);
            }
            input.kind = Kind::File;
            input.value = QDir::cleanPath(path);
            if (input.value.size() > Queue::MAX_INPUT_CHARS) return fail();
        }
        result.intents.push_back(std::move(input));
    }
    return result;
}
} // namespace desktopintent
