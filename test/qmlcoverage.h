// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_QMLCOVERAGE_H
#define BITCOIN_QML_TEST_QMLCOVERAGE_H

class QQmlEngine;
class QString;

/** Explicitly opted-in, test-process-only QML instrumentation support.
 *
 * No environment variables: all functions are no-ops. Otherwise all three of
 * QML_COVERAGE_MANIFEST, QML_COVERAGE_RCC and QML_COVERAGE_OUTPUT are required.
 * Call prepare() before any QQmlEngine (it selects uncached source execution),
 * unregister the original application QRCs, registerResources(),
 * install() for every test engine, then finish() after those engines are gone.
 * Each false return must fail the test process. The report never certifies the
 * instrumenter's completeness; that requires independent qualification.
 */
namespace QmlCoverage {
bool requested();
bool prepare(QString* error);
bool registerResources(QString* error);
bool install(QQmlEngine* engine, QString* error);
bool finish(QString* error);
} // namespace QmlCoverage

#endif // BITCOIN_QML_TEST_QMLCOVERAGE_H
