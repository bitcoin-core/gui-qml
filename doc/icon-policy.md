# Icon policy
This document provides guidelines for contributing icons to Bitcoin Core.

## Preparing icons

Prefer Scalable Vector Graphics (SVG) for new icons. Use the SVG directly as
the production asset so it stays sharp at every display scale. A PNG copy
is not required.

Store SVGs in `qml/res/src/`, register them with a `.svg` alias in
`qml/bitcoin_qml.qrc`, and load them through `qrc:/icons/<name>.svg`.

## Legacy PNG icons

Existing icons loaded through the image provider may still use Portable
Network Graphics (PNG) production assets. Keep their SVG sources in
`qml/res/src/` and their PNGs in `qml/res/icons/` until they are migrated to
SVG. When a PNG needs updating, generate it from its SVG source in a
vector-based tool.

### Optimizing PNG files
Production (PNG) files must be processed by the [optimize-pngs.py](https://github.com/bitcoin-core/bitcoin-maintainer-tools/blob/master/optimize-pngs.py) script before their inclusion in Bitcoin Core.
PNG optimization removes various unnecessary color profiles, ancillary data,
and text chunks, resulting in a lossless file size reduction.

## Contributing
Bitcoin Core primarily uses icons from the [Bitcoin Icon set](https://github.com/BitcoinDesign/Bitcoin-Icons),
an open source icon set made for Bitcoin applications.
If a proposed feature requires an icon to be designed,
an issue should be opened in the [Bitcoin Icons repo](https://github.com/BitcoinDesign/Bitcoin-Icons/issues)
with the request.

Icons are not to be added to Bitcoin Core prior to a production use case.
If an icon is not being used, it should be removed.

## Attribution
Icon additions must include appropriate attribution to the author, license information, and any comments relevant to the icon documented under the
[contrib/debian/copyright](../../../contrib/debian/copyright) file.
