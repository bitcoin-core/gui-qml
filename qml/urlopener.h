// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_URLOPENER_H
#define BITCOIN_QML_URLOPENER_H

#include <QDesktopServices>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <utility>

//! Hands an external URL to the operating system's default handler.
//!
//! This is the single point where the app leaves for an external application,
//! so the scheme allowlist lives here rather than in QML: every URL that
//! reaches it is either a compile-time constant or built from the
//! user-configured third-party transaction template, and refusing anything
//! outside http/https keeps a future caller from inheriting an unchecked
//! handoff to the OS handler registry.
//!
//! The open call itself is injectable so tests can drive both outcomes without
//! launching a real browser, which keeps the production QML on one
//! unconditional path.
class UrlOpener : public QObject
{
    Q_OBJECT

public:
    using OpenUrlFn = std::function<bool(const QUrl&)>;

    explicit UrlOpener(QObject* parent = nullptr)
        : QObject(parent)
        , m_open_url_fn([](const QUrl& url) { return QDesktopServices::openUrl(url); })
    {
    }

    //! Returns false when the URL is malformed, uses a scheme other than
    //! http/https, or no handler could be launched for it.
    //!
    //! Note that a launched handler that later fails still reports success:
    //! the platform only tells us whether a handler was started.
    Q_INVOKABLE bool openUrl(const QString& url)
    {
        const QUrl parsed(url, QUrl::StrictMode);
        if (!parsed.isValid() || parsed.host().isEmpty()) return false;
        const QString scheme = parsed.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;
        return m_open_url_fn(parsed);
    }

    void setOpenUrlFnForTesting(OpenUrlFn fn) { m_open_url_fn = std::move(fn); }

private:
    OpenUrlFn m_open_url_fn;
};

#endif // BITCOIN_QML_URLOPENER_H
