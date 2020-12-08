#pragma once

#include "public.h"

#include <yt/client/chunk_client/proto/read_limit.pb.h>

#include <yt/client/table_client/unversioned_row.h>

#include <yt/core/yson/consumer.h>

#include <yt/core/ytree/public.h>
#include <yt/core/ytree/serialize.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

class TReadLimit
{
public:
    TReadLimit() = default;

    explicit TReadLimit(const NProto::TReadLimit& readLimit);
    explicit TReadLimit(NProto::TReadLimit&& readLimit);
    explicit TReadLimit(const std::unique_ptr<NProto::TReadLimit>& protoLimit);

    explicit TReadLimit(const NTableClient::TLegacyOwningKey& key);
    explicit TReadLimit(NTableClient::TLegacyOwningKey&& key);

    TReadLimit& operator= (const NProto::TReadLimit& protoLimit);
    TReadLimit& operator= (NProto::TReadLimit&& protoLimit);

    TReadLimit GetSuccessor() const;

    const NProto::TReadLimit& AsProto() const;

    const NTableClient::TLegacyOwningKey& GetLegacyKey() const;
    bool HasLegacyKey() const;
    TReadLimit& SetLegacyKey(const NTableClient::TLegacyOwningKey& key);
    TReadLimit& SetLegacyKey(NTableClient::TLegacyOwningKey&& key);

    i64 GetRowIndex() const;
    bool HasRowIndex() const;
    TReadLimit& SetRowIndex(i64 rowIndex);

    i64 GetOffset() const;
    bool HasOffset() const;
    TReadLimit& SetOffset(i64 offset);

    i64 GetChunkIndex() const;
    bool HasChunkIndex() const;
    TReadLimit& SetChunkIndex(i64 chunkIndex);

    i32 GetTabletIndex() const;
    bool HasTabletIndex() const;
    TReadLimit& SetTabletIndex(i32 tabletIndex);

    bool IsTrivial() const;

    void MergeLowerLegacyKey(const NTableClient::TLegacyOwningKey& key);
    void MergeUpperLegacyKey(const NTableClient::TLegacyOwningKey& key);

    void MergeLowerRowIndex(i64 rowIndex);
    void MergeUpperRowIndex(i64 rowIndex);

    void Persist(const TStreamPersistenceContext& context);

    size_t SpaceUsed() const;

private:
    NProto::TReadLimit ReadLimit_;
    NTableClient::TLegacyOwningKey Key_;

    void InitKey();
    void InitCopy(const NProto::TReadLimit& readLimit);
    void InitMove(NProto::TReadLimit&& readLimit);

};

////////////////////////////////////////////////////////////////////////////////

TString ToString(const TReadLimit& limit);

bool IsTrivial(const TReadLimit& limit);
bool IsTrivial(const NProto::TReadLimit& limit);

void ToProto(NProto::TReadLimit* protoReadLimit, const TReadLimit& readLimit);
void FromProto(TReadLimit* readLimit, const NProto::TReadLimit& protoReadLimit);

void Serialize(const TReadLimit& readLimit, NYson::IYsonConsumer* consumer);
void Deserialize(TReadLimit& readLimit, NYTree::INodePtr node);

////////////////////////////////////////////////////////////////////////////////

class TLegacyReadRange
{
public:
    TLegacyReadRange() = default;
    TLegacyReadRange(const TReadLimit& lowerLimit, const TReadLimit& upperLimit);
    explicit TLegacyReadRange(const TReadLimit& exact);

    explicit TLegacyReadRange(const NProto::TReadRange& range);
    explicit TLegacyReadRange(NProto::TReadRange&& range);
    TLegacyReadRange& operator= (const NProto::TReadRange& range);
    TLegacyReadRange& operator= (NProto::TReadRange&& range);

    DEFINE_BYREF_RW_PROPERTY(TReadLimit, LowerLimit);
    DEFINE_BYREF_RW_PROPERTY(TReadLimit, UpperLimit);

    void Persist(const TStreamPersistenceContext& context);

private:
    void InitCopy(const NProto::TReadRange& range);
    void InitMove(NProto::TReadRange&& range);
};

////////////////////////////////////////////////////////////////////////////////

TString ToString(const TLegacyReadRange& range);

void ToProto(NProto::TReadRange* protoReadRange, const TLegacyReadRange& readRange);
void FromProto(TLegacyReadRange* readRange, const NProto::TReadRange& protoReadRange);

void Serialize(const TLegacyReadRange& readRange, NYson::IYsonConsumer* consumer);
void Deserialize(TLegacyReadRange& readRange, NYTree::INodePtr node);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
