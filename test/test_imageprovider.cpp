// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/imageprovider.h>
#include <qml/networkstyle.h>

#include <memory>

class ImageProviderTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void requestPixmap_requiresSizePointer();
    void requestPixmap_requiresValidRequestedSize();
    void requestPixmap_appId_setsRequestedOutputSize();
    void requestPixmap_iconResource_setsRequestedOutputSize();
    void requestPixmap_unknownId_returnsNullPixmap();
    void requestPixmap_colorQuery_drawsIconInThatColor();
    void requestPixmap_badColorQuery_returnsNullPixmap();
};

void ImageProviderTests::initTestCase()
{
    Q_INIT_RESOURCE(bitcoin_qml);
    Q_INIT_RESOURCE(bitcoin_compat);
}

void ImageProviderTests::requestPixmap_requiresSizePointer()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("app"), nullptr, QSize(32, 32));
    QVERIFY(pixmap.isNull());
}

void ImageProviderTests::requestPixmap_requiresValidRequestedSize()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size{7, 9};
    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("app"), &size, QSize{});
    QVERIFY(pixmap.isNull());
    QCOMPARE(size, QSize(7, 9));
}

void ImageProviderTests::requestPixmap_appId_setsRequestedOutputSize()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size;
    const QSize requested_size{40, 24};
    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("app"), &size, requested_size);
    Q_UNUSED(pixmap);
    QCOMPARE(size, requested_size);
}

void ImageProviderTests::requestPixmap_iconResource_setsRequestedOutputSize()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size;
    const QSize requested_size{24, 24};
    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("search"), &size, requested_size);
    Q_UNUSED(pixmap);
    QCOMPARE(size, requested_size);
}

void ImageProviderTests::requestPixmap_unknownId_returnsNullPixmap()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size{3, 5};
    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("definitely_missing_icon"), &size, QSize(32, 32));
    QVERIFY(pixmap.isNull());
    QCOMPARE(size, QSize(3, 5));
}

void ImageProviderTests::requestPixmap_colorQuery_drawsIconInThatColor()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size;
    const QPixmap pixmap = provider.requestPixmap(QStringLiteral("export?color=12ab34"), &size, QSize(24, 24));
    QVERIFY(!pixmap.isNull());
    QCOMPARE(size, QSize(24, 24));
    // The glyph's pixels take the color (within rounding of their partial
    // alpha); the transparent background stays transparent.
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int drawn = 0;
    int transparent = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() == 0) {
                ++transparent;
                continue;
            }
            if (pixel.alpha() < 128) continue;
            ++drawn;
            QVERIFY(qAbs(pixel.red() - 0x12) <= 3);
            QVERIFY(qAbs(pixel.green() - 0xab) <= 3);
            QVERIFY(qAbs(pixel.blue() - 0x34) <= 3);
        }
    }
    QVERIFY(drawn > 0);
    QVERIFY(transparent > 0);
}

void ImageProviderTests::requestPixmap_badColorQuery_returnsNullPixmap()
{
    const std::unique_ptr<const NetworkStyle> style{NetworkStyle::instantiate(ChainType::MAIN)};
    QVERIFY(style != nullptr);
    ImageProvider provider(style.get());

    QSize size;
    QVERIFY(provider.requestPixmap(QStringLiteral("export?color=red"), &size, QSize(24, 24)).isNull());
    QVERIFY(provider.requestPixmap(QStringLiteral("export?colour=12ab34"), &size, QSize(24, 24)).isNull());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ImageProviderTests)
#else
QTEST_MAIN(ImageProviderTests)
#endif
#include "test_imageprovider.moc"
