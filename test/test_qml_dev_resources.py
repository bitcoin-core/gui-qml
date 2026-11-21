#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Check alias handling and live edits in the QML development mirror."""

from pathlib import Path
import errno
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


class DevResourcesTest(unittest.TestCase):
    def test_live_sources_assets_and_removed_aliases(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory).resolve()
            output = source / "mirror"
            probe = source / "symlink-probe"
            try:
                probe.symlink_to(source, target_is_directory=True)
            except OSError as error:
                if error.errno in (errno.EPERM, errno.EACCES, errno.ENOTSUP):
                    self.skipTest("Symbolic links are unavailable: " + str(error))
                raise
            probe.unlink()
            component = source / "Compatibility.qml"
            component.write_text("import QtQuick\nQtObject {}\n")
            icon = source / "icon.svg"
            icon.write_text('<svg xmlns="http://www.w3.org/2000/svg"/>')
            manifest = source / "resources.qrc"
            manifest.write_text(
                '<RCC><qresource prefix="/qml/controls">'
                '<file alias="AppSettings.qml">Compatibility.qml</file>'
                '</qresource><qresource prefix="/icons">'
                '<file alias="icon">icon.svg</file></qresource></RCC>'
            )
            command = [sys.executable, str(Path(__file__).parent / "qml/prepare_dev_resources.py"),
                       "--output", str(output), str(manifest)]
            subprocess.run(command, check=True)
            mirror = output / "qml/controls/AppSettings.qml"
            self.assertTrue(mirror.is_symlink())
            self.assertEqual(mirror.resolve(), component)
            component.write_text("import QtQuick\nQtObject { property int value: 42 }\n")
            self.assertEqual(mirror.read_text(), component.read_text())

            assets = output / "assets.qrc"
            entries = ET.parse(assets).findall(".//file")
            self.assertEqual([(entry.get("alias"), entry.text) for entry in entries],
                             [("icon", str(icon))])
            timestamp = assets.stat().st_mtime_ns
            subprocess.run(command, check=True)
            self.assertEqual(assets.stat().st_mtime_ns, timestamp)

            # Switching Qt compatibility variants must update the same alias.
            replacement = source / "Replacement.qml"
            replacement.write_text("import QtQuick\nQtObject {}\n")
            manifest.write_text(manifest.read_text().replace("Compatibility.qml", "Replacement.qml"))
            subprocess.run(command, check=True)
            self.assertEqual(mirror.resolve(), replacement)

            # Directory-based discovery must not run removed test files.
            manifest.write_text('<RCC><qresource prefix="/qml"/></RCC>')
            subprocess.run(command, check=True)
            self.assertFalse(mirror.is_symlink())
            self.assertFalse((output / "icons/icon").is_symlink())


if __name__ == "__main__":
    unittest.main()
