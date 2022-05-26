package=qt
include packages/qt_details.mk
$(package)_version=$(qt_details_version)
$(package)_download_path=$(qt_details_download_path)
$(package)_file_name=$(qt_details_qtbase_file_name)
$(package)_sha256_hash=$(qt_details_qtbase_sha256_hash)
ifneq ($(host),$(build))
$(package)_dependencies := native_$(package)
endif
$(package)_linux_dependencies=freetype fontconfig libxcb libxkbcommon libxcb_util libxcb_util_cursor libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm libglvnd
$(package)_qt_libs=corelib network widgets gui plugins testlib
$(package)_linguist_tools = lrelease lupdate lconvert
$(package)_patches_path := $(qt_details_patches_path)
$(package)_patches := dont_hardcode_pwd.patch
$(package)_patches += qt.pro
$(package)_patches += qttools_src.pro
$(package)_patches += mac-qmake.conf
$(package)_patches += fix_qt_pkgconfig.patch
$(package)_patches += no-xlib.patch
$(package)_patches += dont_hardcode_x86_64.patch
$(package)_patches += fix_montery_include.patch
$(package)_patches += fix_android_jni_static.patch
$(package)_patches += dont_hardcode_pwd.patch
$(package)_patches += qtbase-moc-ignore-gcc-macro.patch
$(package)_patches += qtbase_avoid_native_float16.patch
$(package)_patches += qtbase_avoid_qmain.patch
$(package)_patches += qtbase_platformsupport.patch
$(package)_patches += qtbase_plugins_cocoa.patch
$(package)_patches += qtbase_skip_tools.patch
$(package)_patches += rcc_hardcode_timestamp.patch
$(package)_patches += qttools_skip_dependencies.patch
$(package)_patches += duplicate_lcqpafonts.patch
$(package)_patches += fast_fixed_dtoa_no_optimize.patch
$(package)_patches += guix_cross_lib_path.patch
$(package)_patches += fix_android_plugin_names.patch
$(package)_patches += fix_riscv_atomic.patch

$(package)_qttranslations_file_name=$(qt_details_qttranslations_file_name)
$(package)_qttranslations_sha256_hash=$(qt_details_qttranslations_sha256_hash)

$(package)_qttools_file_name=$(qt_details_qttools_file_name)
$(package)_qttools_sha256_hash=$(qt_details_qttools_sha256_hash)

$(package)_qtgraphicaleffects_file_name = qtgraphicaleffects-$($(package)_suffix)
$(package)_qtgraphicaleffects_sha256_hash = 237fd5ead50866128a7ae3820f454d0e6b955e648892429fc3b99b8b7fb1f677

$(package)_qtquickcontrols2_file_name = qtquickcontrols2-$($(package)_suffix)
$(package)_qtquickcontrols2_sha256_hash = c4a37bace5a0f6a9ec997097a5e331b438f4b5019d925aa2673fcc036825afb3

$(package)_qttranslations_file_name=qttranslations-$($(package)_suffix)
$(package)_qttranslations_sha256_hash=c92af4171397a0ed272330b4fa0669790fcac8d050b07c8b8cc565ebeba6735e


$(package)_extra_sources := $($(package)_qttranslations_file_name)
$(package)_extra_sources += $($(package)_qtdeclarative_file_name)
$(package)_extra_sources += $($(package)_qtgraphicaleffects_file_name)
$(package)_extra_sources += $($(package)_qtquickcontrols2_file_name)
$(package)_extra_sources += $($(package)_qttools_file_name)

$(package)_top_download_path=$(qt_details_top_download_path)
$(package)_top_cmakelists_file_name=$(qt_details_top_cmakelists_file_name)
$(package)_top_cmakelists_download_file=$(qt_details_top_cmakelists_download_file)
$(package)_top_cmakelists_sha256_hash=$(qt_details_top_cmakelists_sha256_hash)
$(package)_top_cmake_download_path=$(qt_details_top_cmake_download_path)
$(package)_top_cmake_ecmoptionaladdsubdirectory_file_name=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_file_name)
$(package)_top_cmake_ecmoptionaladdsubdirectory_download_file=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_download_file)
$(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)
$(package)_top_cmake_qttoplevelhelpers_file_name=$(qt_details_top_cmake_qttoplevelhelpers_file_name)
$(package)_top_cmake_qttoplevelhelpers_download_file=$(qt_details_top_cmake_qttoplevelhelpers_download_file)
$(package)_top_cmake_qttoplevelhelpers_sha256_hash=$(qt_details_top_cmake_qttoplevelhelpers_sha256_hash)

$(package)_extra_sources += $($(package)_top_cmakelists_file_name)-$($(package)_version)
$(package)_extra_sources += $($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version)
$(package)_extra_sources += $($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version)

define $(package)_set_vars
$(package)_config_opts_release := -release
$(package)_config_opts_debug := -debug
$(package)_config_opts := -no-egl
$(package)_config_opts += -no-eglfs
$(package)_config_opts += -no-evdev
$(package)_config_opts += -no-gif
$(package)_config_opts += -no-glib
$(package)_config_opts += -no-icu
$(package)_config_opts += -no-ico
$(package)_config_opts += -no-kms
$(package)_config_opts += -no-linuxfb
$(package)_config_opts += -no-libjpeg
$(package)_config_opts += -no-libproxy
$(package)_config_opts += -no-libudev
$(package)_config_opts += -no-mtdev
$(package)_config_opts += -no-opengl
$(package)_config_opts += -no-openssl
$(package)_config_opts += -no-openvg
$(package)_config_opts += -no-reduce-relocations
$(package)_config_opts += -no-schannel
$(package)_config_opts += -no-sctp
$(package)_config_opts += -no-securetransport
$(package)_config_opts += -no-system-proxies
$(package)_config_opts += -no-use-gold-linker
$(package)_config_opts += -no-zstd
$(package)_config_opts += -nomake examples
$(package)_config_opts += -nomake tests
$(package)_config_opts += -prefix $(host_prefix)
$(package)_config_opts += -qt-doubleconversion
$(package)_config_opts += -qt-harfbuzz
ifneq ($(host),$(build))
$(package)_config_opts += -qt-host-path $(build_prefix)
endif
$(package)_config_opts += -qt-libpng
$(package)_config_opts += -qt-pcre
$(package)_config_opts += -qt-zlib
$(package)_config_opts += -static
$(package)_config_opts += -no-feature-backtrace
$(package)_config_opts += -no-feature-colordialog
$(package)_config_opts += -no-feature-concurrent
$(package)_config_opts += -no-feature-dial
$(package)_config_opts += -no-feature-gssapi
$(package)_config_opts += -no-feature-http
$(package)_config_opts += -no-feature-image_heuristic_mask
$(package)_config_opts += -no-feature-keysequenceedit
$(package)_config_opts += -no-feature-lcdnumber
$(package)_config_opts += -no-feature-libresolv
$(package)_config_opts += -no-feature-networkdiskcache
$(package)_config_opts += -no-feature-networkproxy
$(package)_config_opts += -no-feature-printsupport
$(package)_config_opts += -no-feature-sessionmanager
$(package)_config_opts += -no-feature-socks5
$(package)_config_opts += -no-feature-sql
$(package)_config_opts += -no-feature-textmarkdownreader
$(package)_config_opts += -no-feature-textmarkdownwriter
$(package)_config_opts += -no-feature-textodfwriter
$(package)_config_opts += -no-feature-topleveldomain
$(package)_config_opts += -no-feature-udpsocket
$(package)_config_opts += -no-feature-undocommand
$(package)_config_opts += -no-feature-undogroup
$(package)_config_opts += -no-feature-undostack
$(package)_config_opts += -no-feature-undoview
$(package)_config_opts += -no-feature-vnc
$(package)_config_opts += -no-feature-vulkan

# Core tools.
$(package)_config_opts += -no-feature-androiddeployqt
$(package)_config_opts += -no-feature-macdeployqt
$(package)_config_opts += -no-feature-qmake
$(package)_config_opts += -no-feature-windeployqt

ifeq ($(host),$(build))
# Qt Tools module.
$(package)_config_opts += -feature-linguist
$(package)_config_opts += -no-feature-assistant
$(package)_config_opts += -no-feature-clang
$(package)_config_opts += -no-feature-clangcpp
$(package)_config_opts += -no-feature-designer
$(package)_config_opts += -no-feature-pixeltool
$(package)_config_opts += -no-feature-qdoc
$(package)_config_opts += -no-feature-qtattributionsscanner
$(package)_config_opts += -no-feature-qtdiag
$(package)_config_opts += -no-feature-qtplugininfo
endif
$(package)_config_opts += -feature-qml-animation
$(package)_config_opts += -no-feature-qml-debug
$(package)_config_opts += -feature-qml-delegate-model
$(package)_config_opts += -feature-qml-devtools
$(package)_config_opts += -feature-qml-itemmodel
$(package)_config_opts += -feature-qml-list-model
$(package)_config_opts += -feature-qml-locale
$(package)_config_opts += -no-feature-qml-network
$(package)_config_opts += -feature-qml-object-model
$(package)_config_opts += -no-feature-qml-preview
$(package)_config_opts += -no-feature-qml-profiler
$(package)_config_opts += -feature-qml-sequence-object
$(package)_config_opts += -feature-qml-table-model
$(package)_config_opts += -no-feature-qml-worker-script
$(package)_config_opts += -no-feature-qml-xml-http-request

$(package)_config_opts_darwin := -no-dbus
$(package)_config_opts_darwin += -no-feature-printsupport
$(package)_config_opts_darwin += -pch
$(package)_config_opts_darwin += -no-feature-corewlan
$(package)_config_opts_darwin += -no-freetype
$(package)_config_opts_darwin += -no-pkg-config

$(package)_config_opts_linux := -dbus-runtime
$(package)_config_opts_linux += -fontconfig
$(package)_config_opts_linux += -no-feature-process
$(package)_config_opts_linux += -no-feature-xlib
$(package)_config_opts_linux += -no-xcb-xlib
$(package)_config_opts_linux += -pkg-config
$(package)_config_opts_linux += -system-freetype
$(package)_config_opts_linux += -xcb
ifneq ($(LTO),)
$(package)_config_opts_linux += -ltcg
endif
$(package)_config_opts_linux += -no-feature-vulkan
$(package)_config_opts_arm_linux += -platform linux-g++ -xplatform bitcoin-linux-g++
$(package)_config_opts_i686_linux  = -xplatform linux-g++-32
ifneq (,$(findstring -stdlib=libc++,$($(1)_cxx)))
$(package)_config_opts_x86_64_linux = -xplatform linux-clang-libc++
else
$(package)_config_opts_x86_64_linux = -xplatform linux-g++-64
endif

$(package)_config_opts_mingw32 := -no-dbus
$(package)_config_opts_mingw32 += -no-freetype
$(package)_config_opts_mingw32 += -no-pkg-config
$(package)_config_opts_mingw32 += -no-opengl
$(package)_config_opts_mingw32 += -no-d3d12
$(package)_config_opts_mingw32 += -xplatform win32-g++
$(package)_config_opts_mingw32 += "QMAKE_CFLAGS = '$($(package)_cflags) $($(package)_cppflags)'"
$(package)_config_opts_mingw32 += "QMAKE_CXXFLAGS = '$($(package)_cflags) $($(package)_cppflags)'"
$(package)_config_opts_mingw32 += "QMAKE_LFLAGS = '$($(package)_ldflags)'"
$(package)_config_opts_mingw32 += -device-option CROSS_COMPILE="$(host)-"
$(package)_config_opts_mingw32 += -pch
ifneq ($(LTO),)
$(package)_config_opts_mingw32 += -ltcg
endif

# CMake build options.
$(package)_config_env := CC="$$($(package)_cc)"
$(package)_config_env += CXX="$$($(package)_cxx)"
$(package)_config_env_darwin := OBJC="$$($(package)_cc)"
$(package)_config_env_darwin += OBJCXX="$$($(package)_cxx)"

$(package)_cmake_opts := -DCMAKE_PREFIX_PATH=$(host_prefix)
$(package)_cmake_opts += -DQT_FEATURE_cxx20=ON
$(package)_cmake_opts += -DQT_ENABLE_CXX_EXTENSIONS=OFF
ifneq ($(V),)
$(package)_cmake_opts += --log-level=STATUS
endif

$(package)_cmake_opts += -DQT_USE_DEFAULT_CMAKE_OPTIMIZATION_FLAGS=ON
$(package)_cmake_opts += -DCMAKE_C_FLAGS="$$($(package)_cppflags) $$($$($(package)_type)_CFLAGS) -ffile-prefix-map=$$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_C_FLAGS_RELEASE="$$($$($(package)_type)_release_CFLAGS)"
$(package)_cmake_opts += -DCMAKE_C_FLAGS_DEBUG="$$($$($(package)_type)_debug_CFLAGS)"
$(package)_cmake_opts += -DCMAKE_CXX_FLAGS="$$($(package)_cppflags) $$($$($(package)_type)_CXXFLAGS) -ffile-prefix-map=$$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_CXX_FLAGS_RELEASE="$$($$($(package)_type)_release_CXXFLAGS)"
$(package)_cmake_opts += -DCMAKE_CXX_FLAGS_DEBUG="$$($$($(package)_type)_debug_CXXFLAGS)"
ifeq ($(host_os),darwin)
$(package)_cmake_opts += -DCMAKE_OBJC_FLAGS="$$($(package)_cppflags) $$($$($(package)_type)_CFLAGS) -ffile-prefix-map=$$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_OBJC_FLAGS_RELEASE="$$($$($(package)_type)_release_CFLAGS)"
$(package)_cmake_opts += -DCMAKE_OBJC_FLAGS_DEBUG="$$($$($(package)_type)_debug_CFLAGS)"
$(package)_cmake_opts += -DCMAKE_OBJCXX_FLAGS="$$($(package)_cppflags) $$($$($(package)_type)_CXXFLAGS) -ffile-prefix-map=$$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_OBJCXX_FLAGS_RELEASE="$$($$($(package)_type)_release_CXXFLAGS)"
$(package)_cmake_opts += -DCMAKE_OBJCXX_FLAGS_DEBUG="$$($$($(package)_type)_debug_CXXFLAGS)"
endif
$(package)_cmake_opts += -DCMAKE_EXE_LINKER_FLAGS="$$($$($(package)_type)_LDFLAGS)"
$(package)_cmake_opts += -DCMAKE_EXE_LINKER_FLAGS_RELEASE="$$($$($(package)_type)_release_LDFLAGS)"
$(package)_cmake_opts += -DCMAKE_EXE_LINKER_FLAGS_DEBUG="$$($$($(package)_type)_debug_LDFLAGS)"

ifneq ($(host),$(build))
$(package)_cmake_opts += -DCMAKE_SYSTEM_NAME=$($(host_os)_cmake_system_name)
$(package)_cmake_opts += -DCMAKE_SYSTEM_VERSION=$($(host_os)_cmake_system_version)
$(package)_cmake_opts += -DCMAKE_SYSTEM_PROCESSOR=$(host_arch)
endif
ifeq ($(host_os),darwin)
$(package)_cmake_opts += -DCMAKE_INSTALL_NAME_TOOL=true
$(package)_cmake_opts += -DCMAKE_FRAMEWORK_PATH=$(OSX_SDK)/System/Library/Frameworks
$(package)_cmake_opts += -DQT_INTERNAL_APPLE_SDK_VERSION=$(OSX_SDK_VERSION)
$(package)_cmake_opts += -DQT_INTERNAL_XCODE_VERSION=$(XCODE_VERSION)
$(package)_cmake_opts += -DQT_NO_APPLE_SDK_MAX_VERSION_CHECK=ON
endif
endef

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qtdeclarative_file_name),$($(package)_qtdeclarative_file_name),$($(package)_qtdeclarative_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qtgraphicaleffects_file_name),$($(package)_qtgraphicaleffects_file_name),$($(package)_qtgraphicaleffects_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qtquickcontrols2_file_name),$($(package)_qtquickcontrols2_file_name),$($(package)_qtquickcontrols2_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttranslations_file_name),$($(package)_qttranslations_file_name),$($(package)_qttranslations_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttools_file_name),$($(package)_qttools_file_name),$($(package)_qttools_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_download_path),$($(package)_top_cmakelists_download_file),$($(package)_top_cmakelists_file_name)-$($(package)_version),$($(package)_top_cmakelists_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_ecmoptionaladdsubdirectory_download_file),$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version),$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_qttoplevelhelpers_download_file),$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version),$($(package)_top_cmake_qttoplevelhelpers_sha256_hash))
endef

ifeq ($(host),$(build))
define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qtdeclarative_sha256_hash)  $($(package)_source_dir)/$($(package)_qtdeclarative_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qtgraphicaleffects_sha256_hash)  $($(package)_source_dir)/$($(package)_qtgraphicaleffects_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qtquickcontrols2_sha256_hash)  $($(package)_source_dir)/$($(package)_qtquickcontrols2_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttranslations_sha256_hash)  $($(package)_source_dir)/$($(package)_qttranslations_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttools_sha256_hash)  $($(package)_source_dir)/$($(package)_qttools_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmakelists_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_qttoplevelhelpers_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source) -C qtbase && \
  mkdir qtdeclarative && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qtdeclarative_file_name) -C qtdeclarative && \
  mkdir qtgraphicaleffects && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qtgraphicaleffects_file_name) -C qtgraphicaleffects && \
  mkdir qtquickcontrols2 && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qtquickcontrols2_file_name) -C qtquickcontrols2 && \
  mkdir qttranslations && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttranslations_file_name) -C qttranslations && \
  mkdir qttools && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttools_file_name) -C qttools && \
  cp $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version) ./$($(package)_top_cmakelists_file_name) && \
  mkdir cmake && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name) && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_qttoplevelhelpers_file_name)
endef
else
define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmakelists_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_qttoplevelhelpers_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  $(build_TAR) --no-same-owner --strip-components=1 -xf $($(package)_source) -C qtbase && \
  cp $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version) ./$($(package)_top_cmakelists_file_name) && \
  mkdir cmake && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name) && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_qttoplevelhelpers_file_name)
endef
endif

define $(package)_preprocess_cmds
  patch -p1 -i $($(package)_patch_dir)/dont_hardcode_pwd.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase-moc-ignore-gcc-macro.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_avoid_native_float16.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_avoid_qmain.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_platformsupport.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_plugins_cocoa.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_skip_tools.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch
  patch -p1 -i $($(package)_patch_dir)/fix_montery_include.patch && \
  patch -p1 -i $($(package)_patch_dir)/use_android_ndk23.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch && \
  patch -p1 -i $($(package)_patch_dir)/duplicate_lcqpafonts.patch && \
  patch -p1 -i $($(package)_patch_dir)/fast_fixed_dtoa_no_optimize.patch && \
  patch -p1 -i $($(package)_patch_dir)/guix_cross_lib_path.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_android_plugin_names.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_riscv_atomic.patch && \
  mkdir -p qtbase/mkspecs/macx-clang-linux &&\
  cp -f qtbase/mkspecs/macx-clang/qplatformdefs.h qtbase/mkspecs/macx-clang-linux/ &&\
  cp -f $($(package)_patch_dir)/mac-qmake.conf qtbase/mkspecs/macx-clang-linux/qmake.conf && \
  cp -r qtbase/mkspecs/linux-arm-gnueabi-g++ qtbase/mkspecs/bitcoin-linux-g++ && \
  sed -i.old "s/arm-linux-gnueabi-/$(host)-/g" qtbase/mkspecs/bitcoin-linux-g++/qmake.conf && \
  echo "!host_build: QMAKE_CFLAGS     += $($(package)_cflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "!host_build: QMAKE_CXXFLAGS   += $($(package)_cxxflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "!host_build: QMAKE_LFLAGS     += $($(package)_ldflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  sed -i.old "s|QMAKE_CC                = \$$$$\$$$${CROSS_COMPILE}clang|QMAKE_CC                = $($(package)_cc)|" qtbase/mkspecs/common/clang.conf && \
  sed -i.old "s|QMAKE_CXX               = \$$$$\$$$${CROSS_COMPILE}clang++|QMAKE_CXX               = $($(package)_cxx)|" qtbase/mkspecs/common/clang.conf

endef
ifeq ($(host),$(build))
  $(package)_preprocess_cmds += && patch -p1 -i $($(package)_patch_dir)/qttools_skip_dependencies.patch
endif

define $(package)_config_cmds
  cd qtbase && \
  ./configure -top-level $($(package)_config_opts) -- $($(package)_cmake_opts)
endef

define $(package)_build_cmds
  cmake --build . -- $$(filter -j%,$$(MAKEFLAGS))
endef

define $(package)_stage_cmds
  cmake --install . --prefix $($(package)_staging_prefix_dir)

  # TODO

#   export PATH && \
#   $(MAKE) -C qtbase/src INSTALL_ROOT=$($(package)_staging_dir) $(addsuffix -install_subtargets,$(addprefix sub-,$($(package)_qt_libs))) && \
#   $(MAKE) -C qtdeclarative INSTALL_ROOT=$($(package)_staging_dir) sub-src-install_subtargets && \
#   $(MAKE) -C qtquickcontrols2 INSTALL_ROOT=$($(package)_staging_dir) sub-src-install_subtargets && \
#   $(MAKE) -C qttools/src/linguist INSTALL_ROOT=$($(package)_staging_dir) $(addsuffix -install_subtargets,$(addprefix sub-,$($(package)_linguist_tools))) && \
#   $(MAKE) -C qttranslations INSTALL_ROOT=$($(package)_staging_dir) install_subtargets
endef

define $(package)_postprocess_cmds
  rm -rf doc/
endef
