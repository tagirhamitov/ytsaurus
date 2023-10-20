# Generated by devtools/yamaker from nixpkgs 0da76dab4c2acce5ebf404c400d38ad95c52b152.

LIBRARY()

LICENSE(Apache-2.0)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(2017-08-21)

ORIGINAL_SOURCE(https://github.com/jmacd/xdelta/archive/7508fd2a823443b1f0173ca361620f21d62a7d37.tar.gz)

ADDINCL(
    contrib/libs/xdelta3
)

NO_COMPILER_WARNINGS()

NO_RUNTIME()

CFLAGS(
    -DNOT_MAIN=1
    -DSECONDARY_DJW=1
    -DSECONDARY_FGK=1
    -DXD3_DEBUG=0
    -DXD3_MAIN=1
)

SRCS(
    xdelta3.c
)

END()