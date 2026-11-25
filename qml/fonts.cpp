// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/fonts.h>

#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QString>

namespace {
void LoadFontResource(const QString& path)
{
    if (QFontDatabase::addApplicationFont(path) < 0) {
        qWarning() << "Failed to load font resource:" << path;
    }
}
} // namespace

void LoadBundledFonts()
{
    LoadFontResource(QStringLiteral(":/fonts/bitcoincoresans/regular"));
    LoadFontResource(QStringLiteral(":/fonts/bitcoincoresans/semibold"));
    LoadFontResource(QStringLiteral(":/fonts/robotomono/regular"));
    LoadFontResource(QStringLiteral(":/fonts/notosansarabic/regular"));
    LoadFontResource(QStringLiteral(":/fonts/notosansarabic/semibold"));
    // BitcoinCoreSans has no Arabic glyphs; don't depend on the system having some.
    QFont::insertSubstitution(QStringLiteral("BitcoinCoreSans"), QStringLiteral("Noto Sans Arabic"));
}
