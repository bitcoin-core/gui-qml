# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

get_target_property(qt_lib_type Qt6::Core TYPE)

function(import_plugins target)
  get_target_property(target_qt_lib_type Qt6::Core TYPE)
  if(target_qt_lib_type STREQUAL "STATIC_LIBRARY")
    set(plugins Qt6::QMinimalIntegrationPlugin)
    if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|FreeBSD|OpenBSD)$")
      list(APPEND plugins Qt6::QXcbIntegrationPlugin)
    elseif(WIN32)
      list(APPEND plugins Qt6::QWindowsIntegrationPlugin Qt6::QModernWindowsStylePlugin)
    elseif(APPLE)
      list(APPEND plugins Qt6::QCocoaIntegrationPlugin Qt6::QMacStylePlugin)
    endif()
    qt6_import_plugins(${target}
      INCLUDE ${plugins}
      EXCLUDE_BY_TYPE
        accessiblebridge
        platforms
        platforms_darwin
        xcbglintegrations
        platformthemes
        platforminputcontexts
        generic
        iconengines
        imageformats
        egldeviceintegrations
        styles
        networkaccess
        networkinformation
        tls
    )
  endif()
endfunction()
