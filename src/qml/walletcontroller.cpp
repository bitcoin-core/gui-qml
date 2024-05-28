// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletcontroller.h>

#include <interfaces/wallet.h>
#include <support/allocators/secure.h>
#include <wallet/wallet.h>


WalletController::WalletController(interfaces::Node& node)
    : m_node(node)
{
}

void WalletController::createSingleSigWallet(const QString& name, const QString& passphrase)
{
    uint64_t flags = 0;
    std::vector<bilingual_str> warning_message;
    SecureString secure_passphrase;
    flags |= wallet::WALLET_FLAG_DESCRIPTORS;
    secure_passphrase.assign(passphrase.toStdString());
    m_node.walletLoader().createWallet(name.toStdString(), secure_passphrase, flags, warning_message);
}
