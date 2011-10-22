#include "stdafx.h"
#include "serialize.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! Auxiliary constants and functions.
namespace {

const ui8 Padding[YTAlignment] = { 0 };

} // namespace <anonymous>

int GetPaddingSize(i64 size)
{
    int result = static_cast<int>(size % YTAlignment);
    return result == 0 ? 0 : YTAlignment - result;
}

i64 AlignUp(i64 size)
{
    return size + GetPaddingSize(size);
}

i32 AlignUp(i32 size)
{
    return size + GetPaddingSize(size);
}

void WritePadding(TOutputStream& output, i64 recordSize)
{
    output.Write(&Padding, GetPaddingSize(recordSize));
}

void WritePadding(TFile& output, i64 recordSize)
{
    output.Write(&Padding, GetPaddingSize(recordSize));
}

////////////////////////////////////////////////////////////////////////////////

// There are optimized versions of these Read/Write functions in protobuf/io/coded_stream.cc.
int WriteVarUInt64(ui64 value, TOutputStream* output)
{
    bool stop = false;
    int bytesWritten = 0;
    while (!stop) {
        ++bytesWritten;
        ui8 byte = static_cast<ui8> (value | 0x80);
        value >>= 7;
        if (value == 0) {
            stop = true;
            byte &= 0x7F;
        }
        output->Write(byte);
    }
    return bytesWritten;
}

int WriteVarInt32(i32 value, TOutputStream* output)
{
    return WriteVarUInt64(static_cast<ui64>(ZigZagEncode32(value)), output);
}

int WriteVarInt64(i64 value, TOutputStream* output)
{
    return WriteVarUInt64(static_cast<ui64>(ZigZagEncode64(value)), output);
}

int ReadVarUInt64(ui64* value, TInputStream* input)
{
    size_t count = 0;
    ui64 result = 0;

    ui8 byte = 0;
    do {
        if (7 * count > 8 * sizeof(ui64) ) {
            // TODO: exception message
            throw yexception();
        }
        input->Read(&byte, 1);
        result |= (static_cast<ui64> (byte & 0x7F)) << (7 * count);
        ++count;
    } while (byte & 0x80);

    *value = result;
    return count;
}

int ReadVarInt32(i32* value, TInputStream* input)
{
    ui64 varInt;
    int bytesRead = ReadVarUInt64(&varInt, input);
    if (varInt > Max<ui32>()) {
        // TODO: exception message
        throw yexception();
    }
    *value = ZigZagDecode32(static_cast<ui32> (varInt));
    return bytesRead;
}

int ReadVarInt64(i64* value, TInputStream* input)
{
    ui64 varInt;
    int bytesRead = ReadVarUInt64(&varInt, input);
    *value = ZigZagDecode64(varInt);
    return bytesRead;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace

