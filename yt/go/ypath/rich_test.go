package ypath

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestParse(t *testing.T) {
	for _, testCase := range []struct {
		input    string
		expected *Rich
	}{
		{
			input:    "/",
			expected: &Rich{Path: Root},
		},
		{
			input:    "  /",
			expected: &Rich{Path: "/"},
		},
		{
			input:    "  //foo bar",
			expected: &Rich{Path: "//foo bar"},
		},
		{
			input:    "<append=%true;>#bc67d12a-e1971bb1-3066dfe2-a94c59a8/foo",
			expected: NewRich(Path("#bc67d12a-e1971bb1-3066dfe2-a94c59a8").Child("foo")).SetAppend(),
		},
		{
			input:    "//foo{bar,zog}",
			expected: NewRich(Root.Child("foo")).SetColumns([]string{"bar", "zog"}),
		},
		{
			input:    "//foo{bar,zog}   ",
			expected: NewRich(Root.Child("foo")).SetColumns([]string{"bar", "zog"}),
		},
		{
			input:    "//foo{}",
			expected: NewRich(Root.Child("foo")).SetColumns([]string{}),
		},
		{
			input:    "//foo{  }",
			expected: NewRich(Root.Child("foo")).SetColumns([]string{}),
		},
		{
			input:    "//foo[:]",
			expected: NewRich(Root.Child("foo")).AddRange(Full()),
		},
		{
			input:    "//foo[,]",
			expected: NewRich(Root.Child("foo")).AddRange(Full()).AddRange(Full()),
		},
		{
			input:    "//foo[,:]",
			expected: NewRich(Root.Child("foo")).AddRange(Full()).AddRange(Full()),
		},
		{
			input:    "//foo[#1:#2]",
			expected: NewRich(Root.Child("foo")).AddRange(Interval(RowIndex(1), RowIndex(2))),
		},
		{
			input:    "//foo[#1:]",
			expected: NewRich(Root.Child("foo")).AddRange(StartingFrom(RowIndex(1))),
		},
		{
			input:    "//foo[:#1]",
			expected: NewRich(Root.Child("foo")).AddRange(UpTo(RowIndex(1))),
		},
		{
			input:    "//foo[#1:#2]  ",
			expected: NewRich(Root.Child("foo")).AddRange(Interval(RowIndex(1), RowIndex(2))),
		},
		{
			input:    "//foo[a]",
			expected: NewRich(Root.Child("foo")).AddRange(Exact(Key([]byte("a")))),
		},
		{
			input:    "//foo[()]",
			expected: NewRich(Root.Child("foo")).AddRange(Exact(Key())),
		},
		{
			input: "//foo[( a, 1, 2u , %true,#)]",
			expected: NewRich(Root.Child("foo")).
				AddRange(Exact(Key(
					[]byte("a"),
					int64(1),
					uint64(2),
					true,
					nil,
				))),
		},
		{
			input:    "//foo\\[a]",
			expected: NewRich(Root.Child("foo\\[a]")),
		},
	} {
		t.Logf("testing %q", testCase.input)
		path, err := Parse(testCase.input)
		if assert.NoError(t, err) {
			assert.Equal(t, testCase.expected, path)
		}
	}
}

func TestParseInvalid(t *testing.T) {
	for _, testCase := range []string{
		"",
		"<",
		"a",
		"//foo{a,b}{a,b}",
		"//foo[:][:]",
		"//foo[:]{a,b}",
		"//foo{a,b}{a,b}",
		"//foo[:]/@foo",
		"//foo{a,b}/@foo",
		"//t[(a,)]",
		"//t[(a)",
		"//t[(a",
		"//t[(a,",
		"//t[(",
		"//t[",
	} {
		_, err := Parse(testCase)
		t.Logf("got error: %v", err)
		assert.Error(t, err, "%q", testCase)
	}
}
