#include "owning_yamr_row.h"

#include <mapreduce/yt/interface/io.h>

namespace NYT {
namespace NTest {

////////////////////////////////////////////////////////////////////////////////

TOwningYaMRRow::TOwningYaMRRow(const TYaMRRow& row)
    : Key(row.Key.ToString())
    , SubKey(row.SubKey.ToString())
    , Value(row.Value.ToString())
{}

TOwningYaMRRow::TOwningYaMRRow(Stroka key, Stroka subKey, Stroka value)
    : Key(std::move(key))
    , SubKey(std::move(subKey))
    , Value(std::move(value))
{ }

bool operator == (const TOwningYaMRRow& row1, const TOwningYaMRRow& row2) {
    return row1.Key == row2.Key
        && row1.SubKey == row2.SubKey
        && row1.Value == row2.Value;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTest
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

template <>
void Out<NYT::NTest::TOwningYaMRRow>(TOutputStream& out, const NYT::NTest::TOwningYaMRRow& row) {
    out << "Row{" << row.Key << ", " << row.SubKey << ", " << row.Value << "}";
}

////////////////////////////////////////////////////////////////////////////////
