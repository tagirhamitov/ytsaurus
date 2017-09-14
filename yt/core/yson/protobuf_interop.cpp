#include "protobuf_interop.h"

#include <yt/core/yson/protos/protobuf_interop.pb.h>

#include <yt/core/yson/consumer.h>
#include <yt/core/yson/writer.h>

#include <yt/core/ypath/token.h>

#include <yt/core/misc/zigzag.h>
#include <yt/core/misc/varint.h>
#include <yt/core/misc/variant.h>

#include <contrib/libs/protobuf/descriptor.h>
#include <contrib/libs/protobuf/wire_format.h>

#include <contrib/libs/protobuf/io/coded_stream.h>
#include <contrib/libs/protobuf/io/zero_copy_stream.h>
#include <contrib/libs/protobuf/io/zero_copy_stream_impl_lite.h>

namespace NYT {
namespace NYson {

using namespace NYson;
using namespace NYTree;
using namespace NYPath;
using namespace ::google::protobuf;
using namespace ::google::protobuf::io;
using namespace ::google::protobuf::internal;

////////////////////////////////////////////////////////////////////////////////

class TProtobufField;
class TProtobufEnumType;

static constexpr size_t TypicalFieldCount = 16;
using TFieldNumberList = SmallVector<int, TypicalFieldCount>;

////////////////////////////////////////////////////////////////////////////////

class TProtobufTypeRegistry
{
public:
    TStringBuf InternString(const TString& str)
    {
        auto guard = Guard(SpinLock_);
        InternedStrings_.push_back(str);
        return InternedStrings_.back();
    }

    TStringBuf GetYsonName(const FieldDescriptor* descriptor)
    {
        const auto& name = descriptor->options().GetExtension(NYT::NYson::NProto::field_name);
        if (name) {
            return InternString(name);
        } else {
            return InternString(descriptor->name());
        }
    }

    TStringBuf GetYsonLiteral(const EnumValueDescriptor* descriptor)
    {
        const auto& name = descriptor->options().GetExtension(NYT::NYson::NProto::enum_value_name);
        if (name) {
            return InternString(name);
        } else {
            return InternString(CamelCaseToUnderscoreCase(descriptor->name()));
        }
    }

    const TProtobufMessageType* ReflectMessageType(const Descriptor* descriptor);
    const TProtobufEnumType* ReflectEnumType(const EnumDescriptor* descriptor);

    static TProtobufTypeRegistry* Get()
    {
        return Singleton<TProtobufTypeRegistry>();
    }

private:
    Y_DECLARE_SINGLETON_FRIEND();
    TProtobufTypeRegistry() = default;

    TSpinLock SpinLock_;
    std::vector<TString> InternedStrings_;
    yhash<const Descriptor*, std::unique_ptr<TProtobufMessageType>> MessageTypeMap_;
    yhash<const EnumDescriptor*, std::unique_ptr<TProtobufEnumType>> EnumTypeMap_;
};

////////////////////////////////////////////////////////////////////////////////

class TProtobufField
{
public:
    TProtobufField(TProtobufTypeRegistry* registry, const FieldDescriptor* descriptor)
        : Underlying_(descriptor)
        , YsonName_(registry->GetYsonName(descriptor))
        , MessageType_(descriptor->type() == FieldDescriptor::TYPE_MESSAGE ? registry->ReflectMessageType(
            descriptor->message_type()) : nullptr)
        , EnumType_(descriptor->type() == FieldDescriptor::TYPE_ENUM ? registry->ReflectEnumType(
            descriptor->enum_type()) : nullptr)
    { }

    ui32 GetTag() const
    {
        return ::google::protobuf::internal::WireFormat::MakeTag(Underlying_);
    }

    const TString& GetFullName() const
    {
        return Underlying_->full_name();
    }

    TStringBuf GetYsonName() const
    {
        return YsonName_;
    }

    int GetNumber() const
    {
        return Underlying_->number();
    }

    FieldDescriptor::Type GetType() const
    {
        return Underlying_->type();
    }

    bool IsRepeated() const
    {
        return Underlying_->is_repeated();
    }

    bool IsRequired() const
    {
        return Underlying_->is_required();
    }

    bool IsOptional() const
    {
        return Underlying_->is_optional();
    }

    const TProtobufMessageType* GetMessageType() const
    {
        return MessageType_;
    }

    const TProtobufEnumType* GetEnumType() const
    {
        return EnumType_;
    }

private:
    const FieldDescriptor* const Underlying_;
    const TStringBuf YsonName_;
    const TProtobufMessageType* MessageType_;
    const TProtobufEnumType* EnumType_;
};

////////////////////////////////////////////////////////////////////////////////

class TProtobufMessageType
{
public:
    TProtobufMessageType(TProtobufTypeRegistry* registry, const Descriptor* descriptor)
        : Registry_(registry)
        , Underlying_(descriptor)
    { }

    void Build()
    {
        for (int index = 0; index < Underlying_->field_count(); ++index) {
            const auto* fieldDescriptor = Underlying_->field(index);
            auto fieldHolder = std::make_unique<TProtobufField>(Registry_, fieldDescriptor);
            auto* field = fieldHolder.get();
            if (field->IsRequired()) {
                RequiredFieldNumbers_.push_back(field->GetNumber());
            }
            YCHECK(NameToField_.emplace(field->GetYsonName(), std::move(fieldHolder)).second);
            YCHECK(NumberToField_.emplace(field->GetNumber(), field).second);
        }
    }

    const TString& GetFullName() const
    {
        return Underlying_->full_name();
    }

    const std::vector<int>& GetRequiredFieldNumbers() const
    {
        return RequiredFieldNumbers_;
    }

    const TProtobufField* FindFieldByName(const TStringBuf& name) const
    {
        auto it = NameToField_.find(name);
        return it == NameToField_.end() ? nullptr : it->second.get();
    }

    const TProtobufField* FindFieldByNumber(int number) const
    {
        auto it = NumberToField_.find(number);
        return it == NumberToField_.end() ? nullptr : it->second;
    }

private:
    TProtobufTypeRegistry* const Registry_;
    const Descriptor* const Underlying_;

    std::vector<int> RequiredFieldNumbers_;
    yhash<TStringBuf, std::unique_ptr<TProtobufField>> NameToField_;
    yhash<int, const TProtobufField*> NumberToField_;
};

////////////////////////////////////////////////////////////////////////////////

class TProtobufEnumType
{
public:
    TProtobufEnumType(TProtobufTypeRegistry* registry, const EnumDescriptor* descriptor)
        : Registry_(registry)
        , Underlying_(descriptor)
    { }

    void Build()
    {
        for (int index = 0; index < Underlying_->value_count(); ++index) {
            const auto* valueDescriptor = Underlying_->value(index);
            auto literal = Registry_->GetYsonLiteral(valueDescriptor);
            YCHECK(LitrealToValue_.emplace(literal, valueDescriptor->number()).second);
            YCHECK(ValueToLiteral_.emplace(valueDescriptor->number(), literal).second);
        }
    }

    const TString& GetFullName() const
    {
        return Underlying_->full_name();
    }

    TNullable<int> FindValueByLiteral(const TStringBuf& literal) const
    {
        auto it = LitrealToValue_.find(literal);
        return it == LitrealToValue_.end() ? Null : MakeNullable(it->second);
    }

    TStringBuf FindLiteralByValue(int value) const
    {
        auto it = ValueToLiteral_.find(value);
        return it == ValueToLiteral_.end() ? TStringBuf() : it->second;
    }

private:
    TProtobufTypeRegistry* const Registry_;
    const EnumDescriptor* const Underlying_;

    yhash<TStringBuf, int> LitrealToValue_;
    yhash<int, TStringBuf> ValueToLiteral_;
};

////////////////////////////////////////////////////////////////////////////////

const TProtobufMessageType* TProtobufTypeRegistry::ReflectMessageType(const Descriptor* descriptor)
{
    auto guard = Guard(SpinLock_);
    auto it = MessageTypeMap_.find(descriptor);
    if (it != MessageTypeMap_.end()) {
        return it->second.get();
    }
    auto typeHolder = std::make_unique<TProtobufMessageType>(this, descriptor);
    auto* type = typeHolder.get();
    it = MessageTypeMap_.emplace(descriptor, std::move(typeHolder)).first;
    guard.Release();
    type->Build();
    return type;
}

const TProtobufEnumType* TProtobufTypeRegistry::ReflectEnumType(const EnumDescriptor* descriptor)
{
    auto guard = Guard(SpinLock_);
    auto it = EnumTypeMap_.find(descriptor);
    if (it != EnumTypeMap_.end()) {
        return it->second.get();
    }
    auto typeHolder = std::make_unique<TProtobufEnumType>(this, descriptor);
    auto* type = typeHolder.get();
    it = EnumTypeMap_.emplace(descriptor, std::move(typeHolder)).first;
    guard.Release();
    type->Build();
    return type;
}

////////////////////////////////////////////////////////////////////////////////

const TProtobufMessageType* ReflectProtobufMessageType(const Descriptor* descriptor)
{
    return TProtobufTypeRegistry::Get()->ReflectMessageType(descriptor);
}

////////////////////////////////////////////////////////////////////////////////

class TYPathStack
{
public:
    void Push(const TStringBuf& literal)
    {
        Items_.push_back(literal);
    }

    void Push(int index)
    {
        Items_.push_back(index);
    }

    void Pop()
    {
        Items_.pop_back();
    }

    TYPath GetPath() const
    {
        if (Items_.empty()) {
            return "/";
        }
        TStringBuilder builder;
        for (const auto& item : Items_) {
            builder.AppendChar('/');
            switch (item.Tag()) {
                case TEntry::TagOf<TStringBuf>():
                    builder.AppendString(ToYPathLiteral(item.As<TStringBuf>()));
                    break;
                case TEntry::TagOf<int>():
                    builder.AppendFormat("%v", item.As<int>());
                    break;
                default:
                    Y_UNREACHABLE();
            }
        }
        return builder.Flush();
    }

private:
    using TEntry = TVariant<TStringBuf, int>;
    std::vector<TEntry> Items_;
};

////////////////////////////////////////////////////////////////////////////////

class TProtobufTranscoderBase
{
protected:
    TYPathStack YPathStack_;


    void SortFields(TFieldNumberList* numbers)
    {
        std::sort(numbers->begin(), numbers->end());
    }

    void ValidateRequiredFieldsPresent(const TProtobufMessageType* type, const TFieldNumberList& numbers)
    {
        if (numbers.size() == type->GetRequiredFieldNumbers().size()) {
            return;
        }

        for (auto number : type->GetRequiredFieldNumbers()) {
            if (!std::binary_search(numbers.begin(), numbers.end(), number)) {
                const auto* field = type->FindFieldByNumber(number);
                YCHECK(field);
                YPathStack_.Push(field->GetYsonName());
                THROW_ERROR_EXCEPTION("Missing required field %v",
                    YPathStack_.GetPath())
                    << TErrorAttribute("ypath", YPathStack_.GetPath())
                    << TErrorAttribute("protobuf_type", type->GetFullName())
                    << TErrorAttribute("protobuf_field", field->GetFullName());
            }
        }

        Y_UNREACHABLE();
    }

    void ValidateNoFieldDuplicates(const TProtobufMessageType* type, const TFieldNumberList& numbers)
    {
        for (auto index = 0; index + 1 < numbers.size(); ++index) {
            if (numbers[index] == numbers[index + 1]) {
                const auto* field = type->FindFieldByNumber(numbers[index]);
                YCHECK(field);
                YPathStack_.Push(field->GetYsonName());
                THROW_ERROR_EXCEPTION("Duplicate field %v",
                    YPathStack_.GetPath())
                    << TErrorAttribute("ypath", YPathStack_.GetPath())
                    << TErrorAttribute("protobuf_type", type->GetFullName());
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

class TProtobufWriter
    : public TProtobufTranscoderBase
    , public TYsonConsumerBase
{
public:
    TProtobufWriter(ZeroCopyOutputStream* outputStream, const TProtobufMessageType* rootType)
        : OutputStream_(outputStream)
        , RootType_(rootType)
        , BodyOutputStream_(&BodyString_)
        , BodyCodedStream_(&BodyOutputStream_)
    { }

    virtual void OnStringScalar(const TStringBuf& value) override
    {
        WriteScalar([&] {
            const auto* field = FieldStack_.back().Field;
            switch (field->GetType()) {
                case FieldDescriptor::TYPE_STRING:
                case FieldDescriptor::TYPE_BYTES:
                    BodyCodedStream_.WriteVarint64(value.length());
                    BodyCodedStream_.WriteRaw(value.begin(), static_cast<int>(value.length()));
                    break;

                case FieldDescriptor::TYPE_ENUM: {
                    const auto* enumType = field->GetEnumType();
                    auto maybeValue = enumType->FindValueByLiteral(value);
                    if (!maybeValue) {
                        THROW_ERROR_EXCEPTION("Field %v cannot have value %Qv",
                            YPathStack_.GetPath(),
                            value)
                            << TErrorAttribute("ypath", YPathStack_.GetPath())
                            << TErrorAttribute("proto_type", enumType->GetFullName());
                    }
                    BodyCodedStream_.WriteVarint32SignExtended(*maybeValue);
                    break;
                }

                default:
                    THROW_ERROR_EXCEPTION("Field %v cannot be parsed from \"string\" values",
                        YPathStack_.GetPath())
                        << TErrorAttribute("ypath", YPathStack_.GetPath())
                        << TErrorAttribute("proto_field", field->GetFullName());
            }
        });
    }

    virtual void OnInt64Scalar(i64 value) override
    {
        OnIntegerScalar(value);
    }

    virtual void OnUint64Scalar(ui64 value) override
    {
        OnIntegerScalar(value);
    }

    virtual void OnDoubleScalar(double value) override
    {
        WriteScalar([&] {
            const auto* field = FieldStack_.back().Field;
            switch (field->GetType()) {
                case FieldDescriptor::TYPE_DOUBLE: {
                    auto encodedValue = WireFormatLite::EncodeDouble(value);
                    BodyCodedStream_.WriteRaw(&encodedValue, sizeof(encodedValue));
                    break;
                }

                case FieldDescriptor::TYPE_FLOAT: {
                    auto encodedValue = WireFormatLite::EncodeFloat(value);
                    BodyCodedStream_.WriteRaw(&encodedValue, sizeof(encodedValue));
                    break;
                }

                default:
                    THROW_ERROR_EXCEPTION("Field %v cannot be parsed from \"double\" values",
                        YPathStack_.GetPath())
                        << TErrorAttribute("ypath", YPathStack_.GetPath())
                        << TErrorAttribute("proto_field", field->GetFullName());
            }
        });
    }

    virtual void OnBooleanScalar(bool value) override
    {
        WriteScalar([&] {
            const auto* field = FieldStack_.back().Field;
            auto type = field->GetType();
            if (type != FieldDescriptor::TYPE_BOOL) {
                THROW_ERROR_EXCEPTION("Field %v cannot be parsed from \"boolean\" values",
                    YPathStack_.GetPath())
                    << TErrorAttribute("ypath", YPathStack_.GetPath())
                    << TErrorAttribute("proto_field", field->GetFullName());
            }
            BodyCodedStream_.WriteVarint32(value ? 1 : 0);
        });
    }

    virtual void OnEntity() override
    {
        THROW_ERROR_EXCEPTION("Entities are not supported")
            << TErrorAttribute("ypath", YPathStack_.GetPath());
    }

    virtual void OnBeginList() override
    {
        ValidateNotRoot();
        ValidateRepeated();
    }

    virtual void OnListItem() override
    {
        Y_ASSERT(!TypeStack_.empty());
        const auto* field = FieldStack_.back().Field;
        int index = FieldStack_.back().CurrentListIndex++;
        FieldStack_.push_back({field, index, true});
        YPathStack_.Push(index);
    }

    virtual void OnEndList() override
    {
        Y_ASSERT(!TypeStack_.empty());
        FieldStack_.pop_back();
    }

    virtual void OnBeginMap() override
    {
        if (TypeStack_.empty()) {
            TypeStack_.push_back({RootType_});
            return;
        }

        const auto* field = FieldStack_.back().Field;
        if (field->GetType() != FieldDescriptor::TYPE_MESSAGE) {
            THROW_ERROR_EXCEPTION("Field %v cannot be parsed from \"map\" values",
                YPathStack_.GetPath())
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("proto_field", field->GetFullName());
        }

        ValidateNotRepeated();
        TypeStack_.push_back({field->GetMessageType()});
        WriteTag();
        int nestedIndex = BeginNestedMessage();
        NestedIndexStack_.push_back(nestedIndex);
    }

    virtual void OnKeyedItem(const TStringBuf& key) override
    {
        Y_ASSERT(TypeStack_.size() > 0);
        const auto* type = TypeStack_.back().Type;

        const auto* field = type->FindFieldByName(key);
        if (!field) {
            THROW_ERROR_EXCEPTION("Unknown field %Qv at %v",
                key,
                YPathStack_.GetPath())
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("proto_type", type->GetFullName());
        }

        auto number = field->GetNumber();
        if (field->IsRequired()) {
            TypeStack_.back().RequiredFieldNumbers.push_back(number);
        } else {
            TypeStack_.back().NonRequiredFieldNumbers.push_back(number);
        }

        FieldStack_.push_back({field, 0, false});
        YPathStack_.Push(field->GetYsonName());
    }

    virtual void OnEndMap() override
    {
        auto& typeEntry = TypeStack_.back();
        auto* type = typeEntry.Type;

        SortFields(&typeEntry.NonRequiredFieldNumbers);
        ValidateNoFieldDuplicates(type, typeEntry.NonRequiredFieldNumbers);

        SortFields(&typeEntry.RequiredFieldNumbers);
        ValidateNoFieldDuplicates(type, typeEntry.RequiredFieldNumbers);
        ValidateRequiredFieldsPresent(type, typeEntry.RequiredFieldNumbers);

        TypeStack_.pop_back();
        if (TypeStack_.empty()) {
            Finish();
            return;
        }

        FieldStack_.pop_back();
        YPathStack_.Pop();
        int nestedIndex = NestedIndexStack_.back();
        NestedIndexStack_.pop_back();
        EndNestedMessage(nestedIndex);
    }

    virtual void OnBeginAttributes() override
    {
        THROW_ERROR_EXCEPTION("Attributes are not supported")
            << TErrorAttribute("ypath", YPathStack_.GetPath());
    }

    virtual void OnEndAttributes() override
    {
        THROW_ERROR_EXCEPTION("Attributes are not supported")
            << TErrorAttribute("ypath", YPathStack_.GetPath());
    }

private:
    ZeroCopyOutputStream* const OutputStream_;
    const TProtobufMessageType* const RootType_;

    TString BodyString_;
    ::google::protobuf::io::StringOutputStream BodyOutputStream_;
    ::google::protobuf::io::CodedOutputStream BodyCodedStream_;

    struct TTypeEntry
    {
        const TProtobufMessageType* Type;
        TFieldNumberList RequiredFieldNumbers;
        TFieldNumberList NonRequiredFieldNumbers;
    };
    std::vector<TTypeEntry> TypeStack_;

    std::vector<int> NestedIndexStack_;

    struct TFieldEntry
    {
        const TProtobufField* Field;
        int CurrentListIndex;
        bool InList;
    };
    std::vector<TFieldEntry> FieldStack_;

    struct TNestedMessageEntry
    {
        int Lo;
        int Hi;
        int ByteSize;
    };
    std::vector<TNestedMessageEntry> NestedMessages_;


    int BeginNestedMessage()
    {
        auto index =  static_cast<int>(NestedMessages_.size());
        NestedMessages_.push_back({BodyCodedStream_.ByteCount(), -1, -1});
        return index;
    }

    void EndNestedMessage(int index)
    {
        Y_ASSERT(NestedMessages_[index].Hi == -1);
        NestedMessages_[index].Hi = BodyCodedStream_.ByteCount();
    }

    void Finish()
    {
        BodyCodedStream_.Trim();

        int bodyLength = static_cast<int>(BodyString_.length());
        NestedMessages_.push_back({bodyLength, std::numeric_limits<int>::max(), -1});

        {
            int nestedIndex = 0;
            std::function<int(int, int)> computeByteSize = [&] (int lo, int hi) {
                auto position = lo;
                int result = 0;
                while (true) {
                    auto& nestedMessage = NestedMessages_[nestedIndex];

                    {
                        auto threshold = std::min(hi, nestedMessage.Lo);
                        result += (threshold - position);
                        position = threshold;
                    }

                    if (nestedMessage.Lo == position && nestedMessage.Hi < std::numeric_limits<int>::max()) {
                        ++nestedIndex;
                        int nestedResult = computeByteSize(nestedMessage.Lo, nestedMessage.Hi);
                        nestedMessage.ByteSize = nestedResult;
                        result += BodyCodedStream_.VarintSize32(static_cast<ui32>(nestedResult));
                        result += nestedResult;
                        position = nestedMessage.Hi;
                    } else {
                        break;
                    }
                }
                return result;
            };
            computeByteSize(0, bodyLength);
        }

        {
            int nestedIndex = 0;
            std::function<void(int, int)> write = [&] (int lo, int hi) {
                auto position = lo;
                while (true) {
                    const auto& nestedMessage = NestedMessages_[nestedIndex];

                    {
                        auto threshold = std::min(hi, nestedMessage.Lo);
                        if (threshold > position) {
                            WriteRaw(BodyString_.data() + position, threshold - position);
                        }
                        position = threshold;
                    }

                    if (nestedMessage.Lo == position && nestedMessage.Hi < std::numeric_limits<int>::max()) {
                        ++nestedIndex;
                        char buf[16];
                        auto length = WriteVarUint64(buf, nestedMessage.ByteSize);
                        WriteRaw(buf, length);
                        write(nestedMessage.Lo, nestedMessage.Hi);
                        position = nestedMessage.Hi;
                    } else {
                        break;
                    }
                }
            };
            write(0, bodyLength);
        }
    }

    void WriteRaw(const char* data, int size)
    {
        while (true) {
            void* chunkData;
            int chunkSize;
            if (!OutputStream_->Next(&chunkData, &chunkSize)) {
                THROW_ERROR_EXCEPTION("Error writing to output stream");
            }
            auto bytesToWrite = std::min(chunkSize, size);
            ::memcpy(chunkData, data, bytesToWrite);
            if (bytesToWrite == size) {
                OutputStream_->BackUp(chunkSize - size);
                break;
            }
            data += bytesToWrite;
            size -= bytesToWrite;
        }
    }


    void ValidateNotRoot()
    {
        if (FieldStack_.empty()) {
            THROW_ERROR_EXCEPTION("Protobuf message can only be parsed from \"map\" values")
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("protobuf_type", RootType_->GetFullName());
        }
    }

    void ValidateNotRepeated()
    {
        if (FieldStack_.back().InList) {
            return;
        }
        const auto* field = FieldStack_.back().Field;
        if (field->IsRepeated()) {
            THROW_ERROR_EXCEPTION("Field %v is repeated and cannot be parsed from scalar values",
                YPathStack_.GetPath())
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("protobuf_field", field->GetFullName());
        }
    }

    void ValidateRepeated()
    {
        if (FieldStack_.back().InList) {
            THROW_ERROR_EXCEPTION("Items of list %v cannot be lists themselves",
                YPathStack_.GetPath())
                << TErrorAttribute("ypath", YPathStack_.GetPath());
        }

        const auto* field = FieldStack_.back().Field;
        if (!field->IsRepeated()) {
            THROW_ERROR_EXCEPTION("Field %v is not repeated and cannot be parsed from \"list\" values",
                YPathStack_.GetPath())
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("protobuf_field", field->GetFullName());
        }
    }

    void WriteTag()
    {
        Y_ASSERT(!FieldStack_.empty());
        const auto* field = FieldStack_.back().Field;
        BodyCodedStream_.WriteTag(field->GetTag());
    }

    template <class F>
    void WriteScalar(F func)
    {
        ValidateNotRoot();
        ValidateNotRepeated();
        WriteTag();
        func();
        FieldStack_.pop_back();
        YPathStack_.Pop();
    }


    template <class T>
    void OnIntegerScalar(T value)
    {
        WriteScalar([&] {
            const auto* field = FieldStack_.back().Field;
            switch (field->GetType()) {
                case FieldDescriptor::TYPE_INT32: {
                    auto i32Value = CheckedCast<i32>(value, STRINGBUF("i32"));
                    BodyCodedStream_.WriteVarint32SignExtended(i32Value);
                    break;
                }

                case FieldDescriptor::TYPE_INT64: {
                    auto i64Value = CheckedCast<i64>(value, STRINGBUF("i64"));
                    BodyCodedStream_.WriteVarint64(static_cast<ui64>(i64Value));
                    break;
                }

                case FieldDescriptor::TYPE_SINT32: {
                    auto i32Value = CheckedCast<i32>(value, STRINGBUF("i32"));
                    BodyCodedStream_.WriteVarint64(ZigZagEncode64(i32Value));
                    break;
                }

                case FieldDescriptor::TYPE_SINT64: {
                    auto i64Value = CheckedCast<i64>(value, STRINGBUF("i64"));
                    BodyCodedStream_.WriteVarint64(ZigZagEncode64(i64Value));
                    break;
                }

                case FieldDescriptor::TYPE_UINT32: {
                    auto ui32Value = CheckedCast<ui32>(value, STRINGBUF("ui32"));
                    BodyCodedStream_.WriteVarint32(ui32Value);
                    break;
                }

                case FieldDescriptor::TYPE_UINT64: {
                    auto ui64Value = CheckedCast<ui64>(value, STRINGBUF("ui64"));
                    BodyCodedStream_.WriteVarint64(ui64Value);
                    break;
                }

                case FieldDescriptor::TYPE_FIXED32: {
                    auto ui32Value = CheckedCast<ui32>(value, STRINGBUF("ui32"));
                    BodyCodedStream_.WriteRaw(&ui32Value, sizeof(ui32Value));
                    break;
                }

                case FieldDescriptor::TYPE_FIXED64: {
                    auto ui64Value = CheckedCast<ui64>(value, STRINGBUF("ui64"));
                    BodyCodedStream_.WriteRaw(&ui64Value, sizeof(ui64Value));
                    break;
                }

                default:
                    THROW_ERROR_EXCEPTION("Field %v cannot be parsed from integer values",
                        YPathStack_.GetPath())
                        << TErrorAttribute("ypath", YPathStack_.GetPath())
                        << TErrorAttribute("proto_field", field->GetFullName());
            }
        });
    }

    template <class TTo, class TFrom>
    static bool IsOutOfRange(TFrom value)
    {
        if (std::numeric_limits<TFrom>::min() != 0) {
            auto min = std::numeric_limits<TTo>::min();
            if (static_cast<i64>(value) < static_cast<i64>(min)) {
                return true;
            }
        }

        auto max = std::numeric_limits<TTo>::max();
        if (static_cast<ui64>(value) > static_cast<ui64>(max)) {
            return true;
        }

        return false;
    }

    template <class TTo, class TFrom>
    TTo CheckedCast(TFrom value, const TStringBuf& to)
    {
        const auto* field = FieldStack_.back().Field;
        if (IsOutOfRange<TTo, TFrom>(value)) {
            THROW_ERROR_EXCEPTION("Value %v of field %v cannot fit into %Qv",
                value,
                YPathStack_.GetPath(),
                to)
                << TErrorAttribute("ypath", YPathStack_.GetPath())
                << TErrorAttribute("protobuf_field", field->GetFullName());
        }
        return static_cast<TTo>(value);
    }
};

std::unique_ptr<IYsonConsumer> CreateProtobufWriter(
    ZeroCopyOutputStream* outputStream,
    const TProtobufMessageType* rootType)
{
    return std::make_unique<TProtobufWriter>(outputStream, rootType);
}

////////////////////////////////////////////////////////////////////////////////

class TProtobufParser
    : public TProtobufTranscoderBase
{
public:
    TProtobufParser(
        IYsonConsumer* consumer,
        ZeroCopyInputStream* inputStream,
        const TProtobufMessageType* rootType)
        : Consumer_(consumer)
        , RootType_(rootType)
        , InputStream_(inputStream)
        , CodedStream_(InputStream_)
    { }

    void Parse()
    {
        TypeStack_.push_back({RootType_});
        RepeatedFieldNumberStack_.push_back({-1, -1});

        Consumer_->OnBeginMap();

        while (true) {
            auto& typeEntry = TypeStack_.back();
            const auto* type = typeEntry.Type;

            auto tag = CodedStream_.ReadTag();
            if (tag == 0) {
                if (RepeatedFieldNumberStack_.back().FieldNumber != -1) {
                    Consumer_->OnEndList();
                }
                RepeatedFieldNumberStack_.pop_back();

                SortFields(&typeEntry.OptionalFieldNumbers);
                ValidateNoFieldDuplicates(type, typeEntry.OptionalFieldNumbers);

                SortFields(&typeEntry.RequiredFieldNumbers);
                ValidateNoFieldDuplicates(type, typeEntry.RequiredFieldNumbers);
                ValidateRequiredFieldsPresent(type, typeEntry.RequiredFieldNumbers);

                Consumer_->OnEndMap();

                TypeStack_.pop_back();
                if (TypeStack_.empty()) {
                    break;
                }

                YPathStack_.Pop();
                CodedStream_.PopLimit(LimitStack_.back());
                LimitStack_.pop_back();
                continue;
            }

            auto wireType = WireFormatLite::GetTagWireType(tag);
            auto fieldNumber = WireFormatLite::GetTagFieldNumber(tag);
            const auto* field = type->FindFieldByNumber(fieldNumber);
            if (!field) {
                THROW_ERROR_EXCEPTION("Unknown field number %v at %v",
                    fieldNumber,
                    YPathStack_.GetPath())
                    << TErrorAttribute("ypath", YPathStack_.GetPath())
                    << TErrorAttribute("proto_type", type->GetFullName());
            }

            if (RepeatedFieldNumberStack_.back().FieldNumber == fieldNumber) {
                Y_ASSERT(field->IsRepeated());
                Consumer_->OnListItem();
                YPathStack_.Push(++RepeatedFieldNumberStack_.back().ListIndex);
            } else {
                if (RepeatedFieldNumberStack_.back().FieldNumber != -1) {
                    Consumer_->OnEndList();
                    RepeatedFieldNumberStack_.back() = {-1, -1};
                    YPathStack_.Pop();
                }

                Consumer_->OnKeyedItem(field->GetYsonName());
                YPathStack_.Push(field->GetYsonName());

                if (field->IsRepeated()) {
                    RepeatedFieldNumberStack_.back() = {fieldNumber, 0};
                    Consumer_->OnBeginList();
                    Consumer_->OnListItem();
                    YPathStack_.Push(0);
                }
            }

            if (field->IsRequired()) {
                typeEntry.RequiredFieldNumbers.push_back(fieldNumber);
            } else if (field->IsOptional()) {
                typeEntry.OptionalFieldNumbers.push_back(fieldNumber);
            }

            switch (wireType) {
                case WireFormatLite::WIRETYPE_VARINT: {
                    ui64 unsignedValue;
                    if (!CodedStream_.ReadVarint64(&unsignedValue)) {
                        THROW_ERROR_EXCEPTION("Error reading \"varint\" value for field %v",
                            YPathStack_.GetPath())
                            << TErrorAttribute("ypath", YPathStack_.GetPath());
                    }

                    switch (field->GetType()) {
                        case FieldDescriptor::TYPE_BOOL:
                            ParseScalar([&] {
                                Consumer_->OnBooleanScalar(unsignedValue != 0);
                            });
                            break;

                        case FieldDescriptor::TYPE_ENUM: {
                            auto signedValue = static_cast<int>(unsignedValue);
                            const auto* enumType = field->GetEnumType();
                            auto literal = enumType->FindLiteralByValue(signedValue);
                            if (!literal) {
                                THROW_ERROR_EXCEPTION("Unknown value %v for field %v",
                                    signedValue,
                                    YPathStack_.GetPath())
                                    << TErrorAttribute("ypath", YPathStack_.GetPath())
                                    << TErrorAttribute("proto_field", field->GetFullName());
                            }
                            ParseScalar([&] {
                                Consumer_->OnStringScalar(literal);
                            });
                            break;
                        }

                        case FieldDescriptor::TYPE_INT32:
                        case FieldDescriptor::TYPE_INT64:
                            ParseScalar([&] {
                                auto signedValue = static_cast<i64>(unsignedValue);
                                Consumer_->OnInt64Scalar(signedValue);
                            });
                            break;

                        case FieldDescriptor::TYPE_UINT32:
                        case FieldDescriptor::TYPE_UINT64:
                            ParseScalar([&] {
                                Consumer_->OnUint64Scalar(unsignedValue);
                            });
                            break;

                        case FieldDescriptor::TYPE_SINT64:
                        case FieldDescriptor::TYPE_SINT32:
                            ParseScalar([&] {
                                auto signedValue = ZigZagDecode64(unsignedValue);
                                Consumer_->OnInt64Scalar(signedValue);
                            });
                            break;

                        default:
                            THROW_ERROR_EXCEPTION("Unexpected \"varint\" value for field %v",
                                YPathStack_.GetPath())
                                << TErrorAttribute("ypath", YPathStack_.GetPath())
                                << TErrorAttribute("proto_field", field->GetFullName());
                    }
                    break;
                }

                case WireFormatLite::WIRETYPE_FIXED32: {
                    ui32 unsignedValue;
                    if (!CodedStream_.ReadLittleEndian32(&unsignedValue)) {
                        THROW_ERROR_EXCEPTION("Error reading \"fixed32\" value for field %v",
                            YPathStack_.GetPath())
                            << TErrorAttribute("ypath", YPathStack_.GetPath());
                    }

                    switch (field->GetType()) {
                        case FieldDescriptor::TYPE_FIXED32:
                            ParseScalar([&] {
                                Consumer_->OnUint64Scalar(unsignedValue);
                            });
                            break;

                        case FieldDescriptor::TYPE_SFIXED32: {
                            ParseScalar([&] {
                                auto signedValue = static_cast<i32>(unsignedValue);
                                Consumer_->OnInt64Scalar(signedValue);
                            });
                            break;
                        }

                        case FieldDescriptor::TYPE_FLOAT: {
                            ParseScalar([&] {
                                auto floatValue = WireFormatLite::DecodeFloat(unsignedValue);
                                Consumer_->OnDoubleScalar(floatValue);
                            });
                            break;
                        }

                        default:
                            THROW_ERROR_EXCEPTION("Unexpected \"fixed32\" value for field %v",
                                YPathStack_.GetPath())
                                << TErrorAttribute("ypath", YPathStack_.GetPath())
                                << TErrorAttribute("proto_field", field->GetFullName());
                    }
                    break;
                }

                case WireFormatLite::WIRETYPE_FIXED64: {
                    ui64 unsignedValue;
                    if (!CodedStream_.ReadLittleEndian64(&unsignedValue)) {
                        THROW_ERROR_EXCEPTION("Error reading \"fixed64\" value for field %v",
                            YPathStack_.GetPath())
                            << TErrorAttribute("ypath", YPathStack_.GetPath());
                    }

                    switch (field->GetType()) {
                        case FieldDescriptor::TYPE_FIXED64:
                            ParseScalar([&] {
                                Consumer_->OnUint64Scalar(unsignedValue);
                            });
                            break;

                        case FieldDescriptor::TYPE_SFIXED64: {
                            ParseScalar([&] {
                                auto signedValue = static_cast<i64>(unsignedValue);
                                Consumer_->OnInt64Scalar(signedValue);
                            });
                            break;
                        }

                        case FieldDescriptor::TYPE_DOUBLE: {
                            ParseScalar([&] {
                                auto doubleValue = WireFormatLite::DecodeDouble(unsignedValue);
                                Consumer_->OnDoubleScalar(doubleValue);
                            });
                            break;
                        }

                        default:
                            THROW_ERROR_EXCEPTION("Unexpected \"fixed64\" value for field %v",
                                YPathStack_.GetPath())
                                << TErrorAttribute("ypath", YPathStack_.GetPath())
                                << TErrorAttribute("proto_field", field->GetFullName());
                    }
                    break;
                }

                case WireFormatLite::WIRETYPE_LENGTH_DELIMITED: {
                    ui64 length;
                    if (!CodedStream_.ReadVarint64(&length)) {
                        THROW_ERROR_EXCEPTION("Error reading \"varint\" value for field %v",
                            YPathStack_.GetPath())
                            << TErrorAttribute("ypath", YPathStack_.GetPath());
                    }

                    switch (field->GetType()) {
                        case FieldDescriptor::TYPE_BYTES:
                        case FieldDescriptor::TYPE_STRING: {
                            PooledStr_.resize(length);
                            if (!CodedStream_.ReadRaw(PooledStr_.data(), length)) {
                                THROW_ERROR_EXCEPTION("Error reading \"string\" value for field %v",
                                    YPathStack_.GetPath())
                                    << TErrorAttribute("ypath", YPathStack_.GetPath());
                            }
                            ParseScalar([&] {
                                Consumer_->OnStringScalar(TStringBuf(PooledStr_.data(), length));
                            });
                            break;
                        }

                        case FieldDescriptor::TYPE_MESSAGE: {
                            RepeatedFieldNumberStack_.push_back({-1, -1});
                            LimitStack_.push_back(CodedStream_.PushLimit(static_cast<int>(length)));
                            TypeStack_.push_back({field->GetMessageType()});
                            Consumer_->OnBeginMap();
                            break;
                        }

                        default:
                            THROW_ERROR_EXCEPTION("Unexpected \"length-delimited\" value for field %v",
                                YPathStack_.GetPath())
                                << TErrorAttribute("ypath", YPathStack_.GetPath())
                                << TErrorAttribute("proto_field", field->GetFullName());
                    }
                    break;
                }

                default:
                    THROW_ERROR_EXCEPTION("Unexpected wire type tag %x",
                        tag)
                        << TErrorAttribute("ypath", YPathStack_.GetPath());
            }
        }
    }

private:
    IYsonConsumer* const Consumer_;
    const TProtobufMessageType* const RootType_;
    ZeroCopyInputStream* const InputStream_;

    CodedInputStream CodedStream_;

    struct TTypeEntry
    {
        const TProtobufMessageType* Type;
        TFieldNumberList RequiredFieldNumbers;
        TFieldNumberList OptionalFieldNumbers;
    };
    std::vector<TTypeEntry> TypeStack_;

    std::vector<CodedInputStream::Limit> LimitStack_;

    struct TRepeatedFieldEntry
    {
        int FieldNumber;
        int ListIndex;
    };
    std::vector<TRepeatedFieldEntry> RepeatedFieldNumberStack_;

    std::vector<char> PooledStr_;


    template <class F>
    void ParseScalar(F func)
    {
        func();
        YPathStack_.Pop();
    }
};

void ParseProtobuf(
    IYsonConsumer* consumer,
    ZeroCopyInputStream* inputStream,
    const TProtobufMessageType* rootType)
{
    TProtobufParser parser(consumer, inputStream, rootType);
    parser.Parse();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
