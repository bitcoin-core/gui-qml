// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/effectivesettings.h>
#include <stdexcept>
namespace effectivesettings {
QString Name(Key key)
{
    switch(key) { case Key::Listen:return "listen";case Key::Natpmp:return "natpmp";case Key::Server:return "server";case Key::Prune:return "prune";case Key::Proxy:return "proxy";case Key::Onion:return "onion";case Key::Dbcache:return "dbcache";case Key::Threads:return "par";case Key::Mempool:return "maxmempool";case Key::Signer:return "signer"; }
    throw std::logic_error("Unknown settings field");
}
Kind Type(Key key) { if(key==Key::Listen||key==Key::Natpmp||key==Key::Server)return Kind::Boolean; if(key==Key::Proxy||key==Key::Onion||key==Key::Signer)return Kind::Text;return Kind::Integer; }
const Field* Find(const Snapshot& snapshot,Key key) { for(const auto& field:snapshot.fields)if(field.key==key)return &field;return nullptr; }
} // namespace effectivesettings
