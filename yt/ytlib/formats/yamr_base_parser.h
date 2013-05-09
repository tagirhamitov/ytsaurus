#pragma once

#include "parser.h"
#include "yamr_table.h"

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

class TYamrBaseParser
    : public IParser
{
public:
    TYamrBaseParser(
        char fieldSeparator,
        char recordSeparator,
        bool enableKeyEscaping,
        bool enableValueEscaping,
        char escapingSymbol,
        bool hasSubkey,
        bool escapeCarriageReturn);

    virtual void Read(const TStringBuf& data) override;
    virtual void Finish() override;

protected:
    virtual void ConsumeKey(const TStringBuf& key) = 0;
    virtual void ConsumeSubkey(const TStringBuf& subkey) = 0;
    virtual void ConsumeValue(const TStringBuf& value) = 0;

    Stroka GetDebugInfo() const;
private:
    DECLARE_ENUM(EState,
        (InsideKey)
        (InsideSubkey)
        (InsideValue)
    );
    EState State;

    char FieldSeparator;
    char RecordSeparator;
    char EscapingSymbol;
    bool ExpectingEscapedChar;
    bool HasSubkey;

    Stroka CurrentToken;

    const char* Consume(const char* begin, const char* end);

    // returns pointer to next fragment or NULL if record is not fully present in [begin, end)
    const char* TryConsumeRecord(const char* begin, const char *end);

    void ProcessKey(const TStringBuf& key);
    void ProcessSubkey(const TStringBuf& subkey);
    void ProcessValue(const TStringBuf& value);

    void ThrowIncorrectFormat() const;

    void OnRangeConsumed(const char* begin, const char* end);
    void AppendToContextBuffer(char symbol);

    // Diagnostic Info
    i64 Offset;
    i64 Record;
    i32 BufferPosition;

    static const int ContextBufferSize = 64;
    char ContextBuffer[ContextBufferSize];

    TYamrTable Table;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
