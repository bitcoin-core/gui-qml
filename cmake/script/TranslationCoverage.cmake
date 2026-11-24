# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Writes {"<tag>": <percent>, ...} to OUTPUT for the "|"-separated
# bitcoin_qml_<tag>.ts files in TS_FILES. lrelease ships unfinished entries
# that have text, so only empty translations count as missing.

string(REPLACE "|" ";" ts_files "${TS_FILES}")
set(entries)
foreach(ts_file IN LISTS ts_files)
    get_filename_component(name "${ts_file}" NAME_WE)
    string(REGEX REPLACE "^bitcoin_qml_" "" tag "${name}")
    file(READ "${ts_file}" content)
    string(REGEX MATCHALL "<translation[ >]" all "${content}")
    string(REGEX MATCHALL "<translation type=\"(obsolete|vanished)\"" stale "${content}")
    string(REGEX MATCHALL "<translation type=\"unfinished\">[ \t\r\n]*(</translation>|<numerusform></numerusform>)" empty "${content}")
    list(LENGTH all all_count)
    list(LENGTH stale stale_count)
    list(LENGTH empty empty_count)
    math(EXPR total "${all_count} - ${stale_count}")
    if(total GREATER 0)
        math(EXPR percent "(${total} - ${empty_count}) * 100 / ${total}")
    else()
        set(percent 0)
    endif()
    list(APPEND entries "\"${tag}\": ${percent}")
endforeach()
list(JOIN entries ", " body)
set(json "{${body}}\n")

if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL json)
    file(WRITE "${OUTPUT}" "${json}")
endif()
