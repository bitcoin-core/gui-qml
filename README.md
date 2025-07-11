# Bitcoin Core App

*(The QML GUI)*

**WARNING: THIS IS EXPERIMENTAL, DO NOT USE BUILDS FROM THIS REPO FOR REAL TRANSACTIONS!**

This directory contains the source code for an experimental Bitcoin Core graphical user interface (GUI) built using the [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html) framework.

Unsecure CI artifacts are available for local testing of the master branch, avoiding the need to build. These can be found under the [Actions](https://github.com/bitcoin-core/gui-qml/actions?query=branch%3Aqt6) tab. It is required to have and be logged into a github account in order to download these.

Note: For macOS, the CI artifact binary must be made executable and code-signed before it can
be ran. To make executable and apply a signature, run the following on the unzipped CI artifact:

```
chmod +x ./Downloads/bitcoin-core-app && codesign -s - ./Downloads/bitcoin-core-app
```

## Goals and Limitations

The current Bitcoin Core GUI has gathered enough technical debt and hacked on features; it is time to begin anew.
This project will start from a clean slate to produce a feature-rich GUI with intuitive user flows and first-class design.

The primary goals of the project can be summed up as follows:

- Implement UX/UI best-practices as documented in the [Bitcoin Design Guide](https://bitcoin.design/guide/)
- Engage with the Bitcoin Design community to implement well-designed features
- Work alongside the Bitcoin Design community to develop an aesthetic GUI
- Develop a mobile-optimized GUI

Avoid conflicts with the Bitcoin Core repository by importing it unmodified as a [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules).
As such, this project **can not** accept pull requests making any significant changes unrelated to the GUI.
Pull requests must be focused on developing the GUI itself, adding build support, or improving relevant documentation.


## Development Process

This repo is synced with the [Bitcoin Core repo](https://github.com/bitcoin/bitcoin) on a regular basis.

Contributions are welcome from all, developers and designers. If you are a new contributor, please read [CONTRIBUTING.md](https://github.com/bitcoin/bitcoin/blob/master/CONTRIBUTING.md).

### Minimum Required Qt Version

All development must adhere to the current upstream Qt Version to minimize our divergence from upstream and avoid costly changes. Review of open PR's must ensure that changes are compatible with this Qt version. Currently, the required version is [Qt 6.2](https://github.com/bitcoin/bitcoin/blob/master/doc/dependencies.md#build-1).

As the Qt Version changes upstream, refactoring is allowed to use the newly available features.

### Policies

This project has custom policies for development, see:
- [Icon Policy](./doc/icon-policy.md)
- [Translator Comments Policy](./doc/translator-comments.md)

## Compile and Run

The master branch is only guaranteed to work and build on Debian-based systems and macOS.
Support for more systems will be confirmed and documented as the project matures.

### Dependencies

Bitcoin Core App requires all the same dependencies as Bitcoin Core, see the
appropriate document for your platform:

- [build-osx.md](https://github.com/bitcoin/bitcoin/blob/master/doc/build-osx.md)

- [build-unix.md](https://github.com/bitcoin/bitcoin/blob/master/doc/build-unix.md)

In addition the following dependencies are required for the GUI:

#### Debian-based systems:

```
sudo apt install \
  qt6-base-dev \
  qt6-tools-dev \
  qt6-l10n-tools \
  qt6-tools-dev-tools \
  qt6-declarative-dev \
  qml6-module-qtquick \
  qml6-module-qtqml \
  libgl-dev \
  libqrencode-dev
```

Additionally, to support Wayland protocol for modern desktop environments:

```
sudo apt install qt6-wayland
```

#### macOS:

```
brew install qt@6 qrencode
```

### Build

1. Install the required dependencies for your platform and clone the repository
2. Fetch the Bitcoin Core submodule:
```
git submodule update --init
```
3. Configure
```
cmake -B build
```
4. Build
```
cmake --build build -j$(nproc)
```

### Run

Binaries are exported to the `build/` directory:
```
build/bin/bitcoin-core-app
```



