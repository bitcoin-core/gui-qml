# Bitcoin Core QML GUI

**WARNING: THIS IS EXPERIMENTAL, DO NOT USE BUILDS FROM THIS REPO FOR REAL TRANSACTIONS!**

To test the recent GUI from the main branch, insecure CI artifacts can be used:
- for Windows: [`insecure_win_gui.zip`](https://api.cirrus-ci.com/v1/artifact/github/bitcoin-core/gui-qml/win64/insecure_win_gui.zip)
- for macOS: [`insecure_mac_gui.zip`](https://api.cirrus-ci.com/v1/artifact/github/bitcoin-core/gui-qml/macos/insecure_mac_gui.zip)
- for Android: [`insecure_android_apk.zip`](https://api.cirrus-ci.com/v1/artifact/github/bitcoin-core/gui-qml/android/insecure_android_apk.zip)

This directory contains the source code for an experimental Bitcoin Core graphical user interface (GUI) built using the [Qt Quick](https://doc.qt.io/qt-5/qtquick-index.html) framework.

# Goals and Limitations

The current Bitcoin Core GUI has gathered enough technical debt and hacked on features; it is time to begin anew.
This project will start from a clean slate to produce a feature-rich GUI with intuitive user flows and first-class design.

The primary goals of the project can be summed up as follows:

- Implement UX/UI best-practices as documented in the [Bitcoin Design Guide](https://bitcoin.design/guide/)
- Engage with the Bitcoin Design community to implement well-designed features
- Work alongside the Bitcoin Design community to develop an aesthetic GUI
- Develop a mobile-optimized GUI

It is important that we stay as conflict-free as possible with the Bitcoin Core repo.
As such, this project will aim to make very few changes outside of the qml directory.
Pull requests must be focused on developing the GUI itself, adding build support,
or improving relevant documentation.

Note that this project will **not** accept pull requests making any significant changes unrelated to the GUI.

# Development Process

This repo is synced with the [Bitcoin Core repo](https://github.com/bitcoin/bitcoin) on a weekly basis, or as needed to resolve conflicts.

Contributions are welcome from all, developers and designers. If you are a new contributor, please read [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Minimum Required Qt Version

All development must adhere to the current upstream Qt Version to minimize our divergence from upstream and avoid costly changes. Review of open PR's must ensure that changes are compatible with this Qt version. Currently, the required version is [Qt 5.12.11](https://github.com/bitcoin-core/gui-qml/blob/a79a2249d027047f08dffe3f04951d52eac198f1/depends/packages/qt.mk#L2).

As the Qt Version changes upstream, refactoring is allowed to use the now available features.

# Compile and Run

The master branch is only guaranteed to work and build on Debian-based systems and macOS.
Support for more systems will be confirmed and documented as the project matures.

### Dependencies
Aside from the dependencies listed in [build-unix.md](../../doc/build-unix.md), Debian based systems require the following additional dependencies to compile:

```
sudo apt install qtdeclarative5-dev qtquickcontrols2-5-dev
```

The following runtime dependencies are also required for dynamic builds;
they are not needed for static builds:

```
sudo apt install qml-module-qtquick2 qml-module-qtquick-controls qml-module-qtquick-controls2 qml-module-qtquick-dialogs qml-module-qtquick-window2
```

No additional dependencies, besides those in [build-osx.md](../../doc/build-osx.md), are needed for macOS.

### Build

For instructions on how to build and compile Bitcoin Core, refer to your respective systems build docs.

To add support for building the qml GUI,
you must configure with the following option:

```
./configure --with-qml
```
### Run

To run the qml GUI:
```
./src/qt/bitcoin-qt
```
