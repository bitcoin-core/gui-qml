// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
  icon.color: Theme.color.neutral0
  icon.source: Theme.dark ? "image://images/sun" : "image://images/moon"
  icon.height: 40
  icon.width: 40
  background: null
  onClicked: Theme.toggleDark()
}
