# Generated by devtools/yamaker.

GTEST()

LICENSE(BSD-3-Clause)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/re2
    contrib/restricted/abseil-cpp/absl/base
    contrib/restricted/abseil-cpp/absl/flags
    contrib/restricted/abseil-cpp/absl/log
    contrib/restricted/abseil-cpp/absl/strings
)

ADDINCL(
    contrib/libs/re2
)

NO_COMPILER_WARNINGS()

NO_UTIL()

EXPLICIT_DATA()

CFLAGS(
    -DGTEST_LINKED_AS_SHARED_LIBRARY=1
)

SRCDIR(contrib/libs/re2)

SRCS(
    re2/testing/backtrack.cc
    re2/testing/charclass_test.cc
    re2/testing/compile_test.cc
    re2/testing/dfa_test.cc
    re2/testing/dump.cc
    re2/testing/exhaustive_tester.cc
    re2/testing/filtered_re2_test.cc
    re2/testing/mimics_pcre_test.cc
    re2/testing/null_walker.cc
    re2/testing/parse_test.cc
    re2/testing/possible_match_test.cc
    re2/testing/re2_arg_test.cc
    re2/testing/re2_test.cc
    re2/testing/regexp_generator.cc
    re2/testing/regexp_test.cc
    re2/testing/required_prefix_test.cc
    re2/testing/search_test.cc
    re2/testing/set_test.cc
    re2/testing/simplify_test.cc
    re2/testing/string_generator.cc
    re2/testing/string_generator_test.cc
    re2/testing/tester.cc
    util/pcre.cc
)

END()
