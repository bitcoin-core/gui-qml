# Bitcoin Core Android QML GUI

This guide will walk you through the steps to build Bitcoin Core QML APKs using WSL Ubuntu 22.04 on Windows 11.

## Prerequisites 

1. Install WSL Ubuntu 22.04 on Windows 11. You can find a comprehensive guide [here](https://ubuntu.com/tutorials/install-ubuntu-on-wsl2-on-windows-11-with-gui-support#1-overview).

## Installation Steps

### Step 1: Update WSL Ubuntu 22.04

After installing Ubuntu, run the following commands to update it:

```bash
sudo apt update
sudo apt upgrade
```

### Step 2: Install Bitcoin Core Dependencies

Next, install the necessary Bitcoin Core dependencies with the following commands:

```bash
sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git
```

Also install the QML specific dependencies:

```bash
sudo apt install qtdeclarative5-dev qtquickcontrols2-5-dev
```

### Step 3: Install Android Studio

Follow the instructions below to install Android Studio and Android NDK on your system:

1. Install OpenJDK-11-JDK:

    ```bash
    sudo apt install openjdk-11-jdk
    ```
   
2. Verify the installation by checking the java version:

    ```bash
    java --version
    ```

3. Install Android Studio using Snap:

    ```bash
    sudo snap install android-studio --classic
    ```
    
4. Run Android Studio:

    ```bash
    android-studio
    ```

You can also follow the full installation guide for Android Studio and Android NDK [here](https://linuxhint.com/install-android-studio-ubuntu22-04/).

### Step 4: Install Android NDK

To install Android NDK:

1. With a project open in Android Studio, click `Tools > SDK Manager`.
2. Click the `SDK Tools` tab.
3. Select the `NDK (Side by side)` and `CMake` checkboxes.
4. Click `OK`.
5. A dialog box will tell you how much space the NDK package consumes on disk.
6. Click `OK`.
7. When the installation is complete, click `Finish`.

You can find the full guide [here](https://developer.android.com/studio/projects/install-ndk).

### Step 5: Check Android Device Architecture

Before you proceed, ensure you check your Android Device's hardware architecture. Use usbip and adb for this. Detailed guide can be found [here](https://www.xda-developers.com/wsl-connect-usb-devices-windows-11/).

1. Connect your Android device to your PC and enable USB debugging in Developer Options.
2. Once usbip is installed, list all USB devices connected to your PC by running the following command as Administrator in `cmd`:

    ```bash
    usbipd wsl list
    ```
    
3. Note down the `BUSID` of the device you want to connect to WSL. Then run the following command, replacing `<busid>` with the `BUSID` of your device:

    ```bash
    usbipd wsl attach --busid <busid>
    ```

4. Install adb:

    ```bash
    sudo apt install adb
    ```
    
5. Check the hardware architecture of your device:

    ```bash
    adb shell getprop ro.product.cpu.abi
    ```

6. Note down the architecture (arm64-v8a, armeabi-v7a, x86_64, x86).

### Step 6: Install Gradle

1. Download Gradle 6.6.1:

    ```bash
    VERSION=6.6.1
    wget https://services.gradle.org/distributions/gradle-${VERSION}-bin.zip -P /tmp
    ```
    
2. Extract the file:

    ```bash
    sudo unzip -d /opt/gradle /tmp/gradle-${VERSION}-bin.zip
    ```
    
3. Set environment variables:

    ```bash
    sudo nano /etc/profile.d/gradle.sh
    ```

4. Add the following lines to the file:

    ```bash
    export GRADLE_HOME=/opt/gradle/gradle-${VERSION}
    export PATH=${GRADLE_HOME}/bin:${PATH}
    ```

5. Change the permissions:

    ```bash
    sudo chmod +x /etc/profile.d/gradle.sh
    ```

6. Load the environment variables:

    ```bash
    source /etc/profile.d/gradle.sh
    ```
    
7. Verify the installation by checking the Gradle version:

    ```bash
    gradle -v
    ```

You can follow the full guide to install Gradle [here](https://linuxhint.com/installing_gradle_ubuntu/).

### Step 7: Build APKs

Before building the APKs, run the following commands:

```bash
PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
sudo bash -c "echo 0 > /proc/sys/fs/binfmt_misc/status" # Disable WSL support for Win32 applications.
```

More details on this step can be found [here](https://github.com/bitcoin/bitcoin/blob/master/doc/build-windows.md#compiling-with-windows-subsystem-for-linux).

Now, you can build the APKs using the guide found [here](https://github.com/bitcoin-core/gui-qml/blob/main/doc/build-android.md).

if you get an error like this:

```bash
global/qlogging:1296:13 n = backtrace(...
```

find the file qlogging.cpp (depends/.../work/.../global/) then you need to edit the function with the follwing:

```
static QStringList backtraceFramesForLogMessage(int frameCount)
{
    QStringList result;
    if (frameCount == 0)
        return result;

#ifdef Q_OS_ANDROID
    result.append(QStringLiteral("Stack trace generation not supported on Android."));
#else
    // existing code here...
#endif

    return result;
}
```
Also make sure to add the ANDROID_HOME variable to your .bashrc file:

```bash
nano ~/.bashrc
```
then add the following line to the end of the file:

```bash
export ANDROID_HOME=/home/<username>/Android/Sdk
export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/platform-tools
```


Once the APKs are built, install the debug version on your connected device using the following command from within the `qt` directory:

```bash
adb install -r android/build/outputs/apk/debug/android-debug.apk
```
