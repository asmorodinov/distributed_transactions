# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(MIT)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.15.2)

ORIGINAL_SOURCE(https://github.com/gabime/spdlog/archive/v1.15.2.tar.gz)

PEERDIR(
    contrib/libs/fmt
)

ADDINCL(
    GLOBAL contrib/restricted/spdlog/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

CFLAGS(
    GLOBAL -DSPDLOG_FMT_EXTERNAL
    -DFMT_LOCALE
    -DFMT_SHARED
    -DSPDLOG_COMPILED_LIB
    -DSPDLOG_PREVENT_CHILD_FD
    -DSPDLOG_SHARED_LIB
)

IF (OS_LINUX OR OS_WINDOWS)
    # NB:
    # On Windows _fwrite_nolock() will be used
    # On Android these are available since API_LEVEL 28 (Android P)
    CFLAGS(
        -DSPDLOG_FWRITE_UNLOCKED
    )
ENDIF()

SRCS(
    src/async.cpp
    src/cfg.cpp
    src/color_sinks.cpp
    src/file_sinks.cpp
    src/spdlog.cpp
    src/stdout_sinks.cpp
)

END()
