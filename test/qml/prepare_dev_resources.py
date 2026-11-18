#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Mirror RCC aliases with symlinks; embed only non-QML assets for local tests."""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("manifests", nargs="+", type=Path)
    args = parser.parse_args()
    root = ET.Element("RCC", version="1.0")
    links = set()
    for manifest in args.manifests:
        for resource in ET.parse(manifest).getroot().findall("qresource"):
            assets = ET.SubElement(root, "qresource", resource.attrib)
            for entry in resource.findall("file"):
                source = (manifest.parent / entry.text).resolve(strict=True)
                alias = entry.get("alias", entry.text)
                destination = args.output / resource.get("prefix", "/").lstrip("/") / alias
                links.add(destination)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.is_symlink() and destination.resolve() != source:
                    destination.unlink()
                if not destination.is_symlink():
                    destination.symlink_to(source)
                if source.suffix not in (".qml", ".js", ".mjs") and source.name != "qmldir":
                    ET.SubElement(assets, "file", alias=alias).text = str(source)
    # Removed manifest entries must not survive directory-based test discovery.
    for destination in args.output.rglob("*"):
        if destination.is_symlink() and destination not in links:
            destination.unlink()
    output = args.output / "assets.qrc"
    content = ET.tostring(root, encoding="unicode")
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    main()
