#ifndef TRANSACTION_INL_H_
#error "Direct inclusion of this file is not allowed, include transaction.h"
// For the sake of sane code completion.
#include "transaction.h"
#endif

#include <yt/yt/client/api/rowset.h>

namespace NYT::NSequoiaClient {

////////////////////////////////////////////////////////////////////////////////

template <class TRow>
TFuture<std::vector<TRow>> ISequoiaTransaction::LookupRows(
    const std::vector<TRow>& keys,
    NTransactionClient::TTimestamp timestamp,
    const NTableClient::TColumnFilter& columnFilter)
{
    auto tableDescriptor = TRow::TTable::Get();

    std::vector<NTableClient::TLegacyKey> rawKeys;
    rawKeys.reserve(keys.size());
    for (const auto& key : keys) {
        rawKeys.push_back(tableDescriptor->ToKey(key, GetRowBuffer()));
    }

    auto asyncRowset = LookupRows(
        tableDescriptor->GetType(),
        MakeSharedRange(std::move(rawKeys), GetRowBuffer()),
        timestamp,
        columnFilter);
    return asyncRowset.Apply(BIND([=, keyCount = std::ssize(keys)] (const NApi::IUnversionedRowsetPtr& rowset) {
        auto wireRows = rowset->GetRows();
        YT_VERIFY(std::ssize(wireRows) == keyCount);

        std::vector<TRow> rows;
        rows.reserve(wireRows.size());
        for (auto wireRow : wireRows) {
            rows.push_back(tableDescriptor->FromUnversionedRow(wireRow, rowset->GetNameTable()));
        }

        return rows;
    }));
}

template <class TRow>
void ISequoiaTransaction::DatalessLockRow(
    NObjectClient::TCellTag masterCellTag,
    const TRow& row,
    NTableClient::ELockType lockType)
{
    const auto& tableDescriptor = TRow::TTable::Get();
    DatalessLockRow(
        masterCellTag,
        tableDescriptor->GetType(),
        tableDescriptor->ToKey(row, GetRowBuffer()),
        lockType);
}

template <class TRow>
void ISequoiaTransaction::LockRow(
    const TRow& row,
    NTableClient::ELockType lockType)
{
    const auto& tableDescriptor = TRow::TTable::Get();
    LockRow(
        tableDescriptor->GetType(),
        tableDescriptor->ToKey(row, GetRowBuffer()),
        lockType);
}

template <class TRow>
void ISequoiaTransaction::WriteRow(const TRow& row)
{
    const auto& tableDescriptor = TRow::TTable::Get();
    WriteRow(
        tableDescriptor->GetType(),
        tableDescriptor->ToUnversionedRow(row, GetRowBuffer()));
}

template <class TRow>
void ISequoiaTransaction::DeleteRow(const TRow& row)
{
    const auto& tableDescriptor = TRow::TTable::Get();
    DeleteRow(
        tableDescriptor->GetType(),
        tableDescriptor->ToKey(row, GetRowBuffer()));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSequoiaClient
