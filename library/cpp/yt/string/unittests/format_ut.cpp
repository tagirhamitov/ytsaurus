#include <library/cpp/testing/gtest/gtest.h>

#include <library/cpp/yt/string/format.h>

#include <library/cpp/yt/small_containers/compact_vector.h>
#include <library/cpp/yt/small_containers/small_vector.h>

#include <limits>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

// Some compile-time sanity checks.
static_assert(TFormatTraits<int>::HasCustomFormatValue);
static_assert(TFormatTraits<double>::HasCustomFormatValue);
static_assert(TFormatTraits<void*>::HasCustomFormatValue);
static_assert(TFormatTraits<const char*>::HasCustomFormatValue);
static_assert(TFormatTraits<TStringBuf>::HasCustomFormatValue);
static_assert(TFormatTraits<TString>::HasCustomFormatValue);
static_assert(TFormatTraits<std::vector<int>>::HasCustomFormatValue);
static_assert(TFormatTraits<SmallVector<int, 1>>::HasCustomFormatValue);
static_assert(TFormatTraits<TCompactVector<int, 1>>::HasCustomFormatValue);
static_assert(TFormatTraits<std::set<int>>::HasCustomFormatValue);
static_assert(TFormatTraits<std::map<int, int>>::HasCustomFormatValue);
static_assert(TFormatTraits<std::multimap<int, int>>::HasCustomFormatValue);
static_assert(TFormatTraits<THashSet<int>>::HasCustomFormatValue);
static_assert(TFormatTraits<THashMap<int, int>>::HasCustomFormatValue);
static_assert(TFormatTraits<THashMultiSet<int>>::HasCustomFormatValue);
static_assert(TFormatTraits<std::pair<int, int>>::HasCustomFormatValue);
static_assert(TFormatTraits<std::optional<int>>::HasCustomFormatValue);
static_assert(TFormatTraits<TDuration>::HasCustomFormatValue);
static_assert(TFormatTraits<TInstant>::HasCustomFormatValue);

struct TUnformattable
{ };
static_assert(!TFormatTraits<TUnformattable>::HasCustomFormatValue);

////////////////////////////////////////////////////////////////////////////////

TEST(TFormatTest, Nothing)
{
    EXPECT_EQ("abc", Format("a%nb%nc", 1, 2));
}

TEST(TFormatTest, Verbatim)
{
    EXPECT_EQ("", Format(""));
    EXPECT_EQ("test", Format("test"));
    EXPECT_EQ("%", Format("%%"));
    EXPECT_EQ("%hello%world%", Format("%%hello%%world%%"));
}

TEST(TFormatTest, MultipleArgs)
{
    EXPECT_EQ("2+2=4", Format("%v+%v=%v", 2, 2, 4));
}

TEST(TFormatTest, Strings)
{
    EXPECT_EQ("test", Format("%s", "test"));
    EXPECT_EQ("test", Format("%s", TStringBuf("test")));
    EXPECT_EQ("test", Format("%s", TString("test")));

    EXPECT_EQ("   abc", Format("%6s", TString("abc")));
    EXPECT_EQ("abc   ", Format("%-6s", TString("abc")));
    EXPECT_EQ("       abc", Format("%10v", TString("abc")));
    EXPECT_EQ("abc       ", Format("%-10v", TString("abc")));
    EXPECT_EQ("abc", Format("%2s", TString("abc")));
    EXPECT_EQ("abc", Format("%-2s", TString("abc")));
    EXPECT_EQ("abc", Format("%0s", TString("abc")));
    EXPECT_EQ("abc", Format("%-0s", TString("abc")));
    EXPECT_EQ(100, std::ssize(Format("%100v", "abc")));
}

TEST(TFormatTest, Integers)
{
    EXPECT_EQ("123", Format("%d", 123));
    EXPECT_EQ("123", Format("%v", 123));

    EXPECT_EQ("042", Format("%03d", 42));
    EXPECT_EQ("42", Format("%01d", 42));

    EXPECT_EQ("2147483647", Format("%d", std::numeric_limits<i32>::max()));
    EXPECT_EQ("-2147483648", Format("%d", std::numeric_limits<i32>::min()));

    EXPECT_EQ("0", Format("%u", 0U));
    EXPECT_EQ("0", Format("%v", 0U));
    EXPECT_EQ("4294967295", Format("%u", std::numeric_limits<ui32>::max()));
    EXPECT_EQ("4294967295", Format("%v", std::numeric_limits<ui32>::max()));

    EXPECT_EQ("9223372036854775807", Format("%" PRId64, std::numeric_limits<i64>::max()));
    EXPECT_EQ("9223372036854775807", Format("%v", std::numeric_limits<i64>::max()));
    EXPECT_EQ("-9223372036854775808", Format("%" PRId64, std::numeric_limits<i64>::min()));
    EXPECT_EQ("-9223372036854775808", Format("%v", std::numeric_limits<i64>::min()));

    EXPECT_EQ("0", Format("%" PRIu64, 0ULL));
    EXPECT_EQ("0", Format("%v", 0ULL));
    EXPECT_EQ("18446744073709551615", Format("%" PRIu64, std::numeric_limits<ui64>::max()));
    EXPECT_EQ("18446744073709551615", Format("%v", std::numeric_limits<ui64>::max()));
}

TEST(TFormatTest, Floats)
{
    EXPECT_EQ("3.14", Format("%.2f", 3.1415F));
    EXPECT_EQ("3.14", Format("%.2v", 3.1415F));
    EXPECT_EQ("3.14", Format("%.2lf", 3.1415));
    EXPECT_EQ("3.14", Format("%.2v", 3.1415));
    EXPECT_EQ(TString(std::to_string(std::numeric_limits<double>::max())),
            Format("%lF", std::numeric_limits<double>::max()));
}

TEST(TFormatTest, Bool)
{
    EXPECT_EQ("True", Format("%v", true));
    EXPECT_EQ("False", Format("%v", false));
    EXPECT_EQ("true", Format("%lv", true));
    EXPECT_EQ("false", Format("%lv", false));
}

TEST(TFormatTest, Quotes)
{
    EXPECT_EQ("\"True\"", Format("%Qv", true));
    EXPECT_EQ("'False'", Format("%qv", false));
    EXPECT_EQ("'\\\'\"'", Format("%qv", "\'\""));
    EXPECT_EQ("\"\\x01\"", Format("%Qv", "\x1"));
    EXPECT_EQ("'\\x1b'", Format("%qv", '\x1b'));
}

TEST(TFormatTest, Nullable)
{
    EXPECT_EQ("1", Format("%v", std::make_optional<int>(1)));
    EXPECT_EQ("<null>", Format("%v", std::nullopt));
    EXPECT_EQ("<null>", Format("%v", std::optional<int>()));
    EXPECT_EQ("3.14", Format("%.2f", std::optional<double>(3.1415)));
}

TEST(TFormatTest, Pointers)
{
    // No idea if pointer format is standardized, check against Sprintf.
    auto p = reinterpret_cast<void*>(123);
    EXPECT_EQ(Sprintf("%p", reinterpret_cast<void*>(123)), Format("%p", p));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT

