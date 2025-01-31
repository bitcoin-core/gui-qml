// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/snapshotqml.h>

#include <node/utxo_snapshot.h>
#include <sync.h>
#include <util/fs.h>
#include <util/fs_helpers.h>

SnapshotQml::SnapshotQml(interfaces::Node& node, QString path)
    : m_node(node), m_path(path) {}

bool SnapshotQml::processPath()
{
    const fs::path path = fs::u8path(m_path.toStdString());
    if (!fs::exists(path)) {
        return false;
    }

    FILE* snapshot_file{fsbridge::fopen(path, "rb")};
    AutoFile afile{snapshot_file};
    if (afile.IsNull()) {
        return false;
    }

    node::SnapshotMetadata metadata;
    try {
        afile >> metadata;
    } catch (const std::exception& e) {
        return false;
    }

    bool result = m_node.loadSnapshot(afile, metadata, false);
    if (!result) {
        return false;
    }
    return true;
}