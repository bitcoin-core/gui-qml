// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_VIEWONLYIMPORT_H
#define BITCOIN_QML_VIEWONLYIMPORT_H

#include <outputtype.h>
#include <wallet/imports.h>

#include <QString>
#include <QVariantMap>

#include <cstddef>
#include <vector>

struct ViewOnlyImport {
    enum class Kind { Empty, ExtendedKey, Descriptors, DescriptorJson };

    Kind kind{Kind::Empty};
    QString error;
    std::vector<wallet::ImportDescriptorRequest> requests;
    //! Requests without a creation time. They take the birth time the user picks.
    std::vector<size_t> undated;
    std::vector<OutputType> output_types;
    size_t descriptor_count{0};
    //! A plain xpub without a key origin path does not say its script type.
    bool needs_address_type{false};
};

//! Reads an extended public key, descriptors one per line, or the JSON
//! that listdescriptors prints and importdescriptors takes. address_type
//! ("bech32", ...) is used for a key that does not say its own.
ViewOnlyImport ParseViewOnlyImport(const QString& text, const QString& address_type = {});

//! The parse result for the form: status (empty, invalid, valid), error,
//! kind, descriptorCount, addressTypes, needsBirthDate, createdTime
//! (seconds, or -1 when no descriptor has one), needsAddressType and
//! addressTypeOptions ({id, label}).
QVariantMap DescribeViewOnlyImport(const QString& text, const QString& address_type = {});

#endif // BITCOIN_QML_VIEWONLYIMPORT_H
