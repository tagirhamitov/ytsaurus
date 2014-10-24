#pragma once

#include "common.h"
#include "enum.h"

namespace NYT {

///////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TError;

}  // namespace NProto

///////////////////////////////////////////////////////////////////////////////

template <class T>
class TErrorOr;

typedef TErrorOr<void> TError;

template <class T>
struct TErrorTraits;

DECLARE_REFCOUNTED_STRUCT(TLeaseEntry)
typedef TLeaseEntryPtr TLease;

class TStreamSaveContext;
class TStreamLoadContext;

template <class TSaveContext, class TLoadContext>
class TCustomPersistenceContext;

struct TValueBoundComparer;
struct TValueBoundSerializer;

template <class T, class C, class = void>
struct TSerializerTraits;

class TChunkedMemoryPool;

template <class TKey, class TComparer>
class TSkipList;

class TBlobOutput;
class TFakeStringBufStore;

class TStringBuilder;

struct ICheckpointableInputStream;
struct ICheckpointableOutputStream;

DECLARE_REFCOUNTED_CLASS(TSlruCacheConfig)

///////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EErrorCode,
    ((OK)              (0))
    ((Generic)         (1))
    ((Timeout)         (104))
);

///////////////////////////////////////////////////////////////////////////////

} // namespace NYT
