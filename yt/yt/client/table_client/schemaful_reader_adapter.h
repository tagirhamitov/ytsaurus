#pragma once

#include "row_base.h"

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

using TSchemalessReaderFactory = std::function<ISchemalessUnversionedReaderPtr(
    TNameTablePtr,
    const TColumnFilter&)> ;

ISchemafulUnversionedReaderPtr CreateSchemafulReaderAdapter(
    TSchemalessReaderFactory createReader,
    const TTableSchema& schema,
    const TColumnFilter& columnFilter = {});

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
