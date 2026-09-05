// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/networkstyle.h>

#include <qml/guiconstants.h>

#include <util/chaintype.h>

#include <tinyformat.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPixmap>

static const struct {
    const ChainType networkId;
    const char* appName;
    const int iconColorHueShift;
    const int iconColorSaturationReduction;
} network_styles[] = {
    {ChainType::MAIN, QAPP_APP_NAME_DEFAULT, 0, 0},
    {ChainType::TESTNET, QAPP_APP_NAME_TESTNET, 70, 30},
    {ChainType::TESTNET4, QAPP_APP_NAME_TESTNET4, 70, 30},
    {ChainType::SIGNET, QAPP_APP_NAME_SIGNET, 35, 15},
    {ChainType::REGTEST, QAPP_APP_NAME_REGTEST, 160, 30},
};

// titleAddText needs to be const char* for tr()
NetworkStyle::NetworkStyle(const QString& appName,
                           const int iconColorHueShift,
                           const int iconColorSaturationReduction,
                           const char* _titleAddText)
    : appName(appName)
    , titleAddText(qApp->translate("SplashScreen", _titleAddText))
{
    QPixmap pixmap(":/icons/bitcoin");

    if (iconColorHueShift != 0 && iconColorSaturationReduction != 0) {
        QImage img = pixmap.toImage();

        int h, s, l, a;
        for (int y = 0; y < img.height(); y++) {
            QRgb* scL = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); x++) {
                a = qAlpha(scL[x]);
                QColor col(scL[x]);
                col.getHsl(&h, &s, &l);

                h += iconColorHueShift;
                if (s > iconColorSaturationReduction) {
                    s -= iconColorSaturationReduction;
                }
                col.setHsl(h, s, l, a);
                scL[x] = col.rgba();
            }
        }
        pixmap.convertFromImage(img);
    }

    appIcon = QIcon(pixmap);
    trayAndWindowIcon = QIcon(pixmap.scaled(QSize(256, 256)));
}

const NetworkStyle* NetworkStyle::instantiate(const ChainType networkId)
{
    std::string titleAddText = networkId == ChainType::MAIN ? "" : strprintf("[%s]", ChainTypeToString(networkId));
    for (const auto& network_style : network_styles) {
        if (networkId == network_style.networkId) {
            return new NetworkStyle(
                network_style.appName,
                network_style.iconColorHueShift,
                network_style.iconColorSaturationReduction,
                titleAddText.c_str());
        }
    }
    return nullptr;
}
