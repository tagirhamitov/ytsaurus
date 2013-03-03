#pragma once

/*!
 * \file enum.h
 * \brief Smart enumerations
 */

#include "preprocessor.h"
#include "foreach.h"

#include <util/stream/base.h>
#include <util/string/cast.h>
#include <util/generic/typehelpers.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>

#include <stdexcept>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

/*!
 * \defgroup yt_enum Smart enumerations
 * \ingroup yt_commons
 *
 * \{
 *
 * A string literal could be associated with an instance of polymorphic
 * enumeration and this literal is preserved during casts.
 *
 * \page yt_enum_examples Examples
 * Please refer to the unit test for an actual example of usage
 * (unittests/enum_ut.cpp).
 *
 */

//! Base class tag for strongly-typed enumerations.
template <class T>
class TEnumBase
{ };

/*! \} */

////////////////////////////////////////////////////////////////////////////////

//! \internal
//! \defgroup yt_enum_mixins Mix-ins for the internals of enumerations.
//! \{

//! Declaration of an enumeration class.
/*!
 * \param name Name of the enumeration.
 * \param base Base class; either ##TEnumBase<T> or ##TPolymorphicEnumBase<T>.
 * \param seq Enumeration domain encoded as a <em>sequence</em>.
 */
#define ENUM__CLASS(name, seq) \
    class name \
        : public ::NYT::TEnumBase<name> \
    { \
    public: \
        enum EDomain \
        { \
            PP_FOR_EACH(ENUM__DOMAIN_ITEM, seq) \
        }; \
        \
        name() \
            : Value(static_cast<EDomain>(0)) \
        { } \
        \
        name(EDomain e) \
            : Value(e) \
        { } \
        \
        explicit name(int value) \
            : Value(static_cast<EDomain>(value)) \
        { } \
        \
        name& operator=(EDomain e) \
        { \
            Value = e; \
            return *this; \
        } \
        \
        operator EDomain() const \
        { \
            return Value; \
        } \
        \
        Stroka ToString() const \
        { \
            Stroka str(GetLiteralByValue(Value)); \
            if (LIKELY(!str.empty())) { \
                return str; \
            } else { \
                return Stroka(PP_STRINGIZE(name)) + "(" + ::ToString(static_cast<int>(Value)) + ")"; \
            } \
        } \
        \
        static const char* GetLiteralByValue(int value) \
        { \
            switch (value) \
            { \
                PP_FOR_EACH(ENUM__LITERAL_BY_VALUE_ITEM, seq) \
                default: \
                    return nullptr; \
            } \
        } \
        \
        static bool GetValueByLiteral(const char* literal, int* target) \
        { \
            PP_FOR_EACH(ENUM__VALUE_BY_LITERAL_ITEM, seq); \
            return false; \
        } \
        \
        static int GetDomainSize() \
        { \
            return PP_COUNT(seq); \
        } \
        \
        static std::vector<EDomain> GetDomainValues() \
        { \
            static const EDomain bits[] = { \
                PP_FOR_EACH(ENUM__GET_DOMAIN_VALUES_ITEM, seq) \
                static_cast<EDomain>(-1) \
            }; \
            return std::vector<EDomain>(bits, bits + sizeof(bits) / sizeof(bits[0]) - 1); \
        } \
        \
        static std::vector<Stroka> GetDomainNames() \
        { \
            static const char* names[] = { \
                PP_FOR_EACH(ENUM__GET_DOMAIN_NAMES_ITEM, seq) \
                nullptr \
            }; \
            return std::vector<Stroka>(names, names + sizeof(names) / sizeof(names[0]) - 1); \
        } \
        \
        static name FromString(const char* str) \
        { \
            int value; \
            if (!GetValueByLiteral(str, &value)) { \
                throw std::runtime_error(Sprintf("Error parsing %s value %s", \
                    PP_STRINGIZE(name), \
                    ~Stroka(str).Quote())); \
            } \
            return name(value); \
        } \
        \
        static name FromString(const Stroka& str) \
        { \
            return name::FromString(str.c_str()); \
        } \
        \
        static bool FromString(const char* str, name* target) \
        { \
            int value; \
            if (!GetValueByLiteral(str, &value)) { \
                return false; \
            } else { \
                *target = name(value); \
                return true; \
            } \
        } \
        \
        static bool FromString(const Stroka& str, name* target) \
        { \
            return name::FromString(str.c_str(), target); \
        } \
        \
    private: \
        EDomain Value;

//! EDomain declaration helper.
//! \{
#define ENUM__DOMAIN_ITEM(item) \
    PP_IF( \
        PP_IS_SEQUENCE(item), \
        ENUM__DOMAIN_ITEM_SEQ, \
        ENUM__DOMAIN_ITEM_ATOMIC \
    )(item)()

#define ENUM__DOMAIN_ITEM_ATOMIC(item) \
    item PP_COMMA

#define ENUM__DOMAIN_ITEM_SEQ(seq) \
    PP_ELEMENT(seq, 0) = PP_ELEMENT(seq, 1) PP_COMMA
//! \}

//! #GetLiteralByValue() helper.
//! \{
#define ENUM__LITERAL_BY_VALUE_ITEM(item) \
    PP_IF( \
        PP_IS_SEQUENCE(item), \
        ENUM__LITERAL_BY_VALUE_ITEM_SEQ, \
        ENUM__LITERAL_BY_VALUE_ITEM_ATOMIC \
    )(item)

#define ENUM__LITERAL_BY_VALUE_ITEM_SEQ(seq) \
    ENUM__LITERAL_BY_VALUE_ITEM_ATOMIC(PP_ELEMENT(seq, 0))

#define ENUM__LITERAL_BY_VALUE_ITEM_ATOMIC(item) \
    case static_cast<int>(item): \
        return PP_STRINGIZE(item);
//! \}

//! #GetValueByLiteral() helper.
//! \{
#define ENUM__VALUE_BY_LITERAL_ITEM(item) \
    PP_IF( \
        PP_IS_SEQUENCE(item), \
        ENUM__VALUE_BY_LITERAL_ITEM_SEQ, \
        ENUM__VALUE_BY_LITERAL_ITEM_ATOMIC \
    )(item)

#define ENUM__VALUE_BY_LITERAL_ITEM_SEQ(seq) \
    ENUM__VALUE_BY_LITERAL_ITEM_ATOMIC(PP_ELEMENT(seq, 0))

#define ENUM__VALUE_BY_LITERAL_ITEM_ATOMIC(item) \
    if (::strcmp(literal, PP_STRINGIZE(item)) == 0) { \
        *target = static_cast<int>(item); \
        return true; \
    }
//! \}

//! #GetDomainValues() helper.
//! \{
#define ENUM__GET_DOMAIN_VALUES_ITEM(item) \
    PP_IF( \
        PP_IS_SEQUENCE(item), \
        ENUM__GET_DOMAIN_VALUES_ITEM_SEQ, \
        ENUM__GET_DOMAIN_VALUES_ITEM_ATOMIC \
    )(item)

#define ENUM__GET_DOMAIN_VALUES_ITEM_SEQ(seq) \
    ENUM__GET_DOMAIN_VALUES_ITEM_ATOMIC(PP_ELEMENT(seq, 0))

#define ENUM__GET_DOMAIN_VALUES_ITEM_ATOMIC(item) \
    (item),
//! \}

//! #GetDomainNames() helper.
//! {
#define ENUM__GET_DOMAIN_NAMES_ITEM(item) \
    PP_IF( \
        PP_IS_SEQUENCE(item), \
        ENUM__GET_DOMAIN_NAMES_ITEM_SEQ, \
        ENUM__GET_DOMAIN_NAMES_ITEM_ATOMIC \
    )(item)

#define ENUM__GET_DOMAIN_NAMES_ITEM_SEQ(seq) \
    ENUM__GET_DOMAIN_NAMES_ITEM_ATOMIC(PP_ELEMENT(seq, 0))

#define ENUM__GET_DOMAIN_NAMES_ITEM_ATOMIC(item) \
    PP_STRINGIZE(item), \
//! \}

//! Declaration of relational operators; all at once.
#define ENUM__RELATIONAL_OPERATORS(name) \
    public: \
        ENUM__RELATIONAL_OPERATOR(name, < ) \
        ENUM__RELATIONAL_OPERATOR(name, > ) \
        ENUM__RELATIONAL_OPERATOR(name, <=) \
        ENUM__RELATIONAL_OPERATOR(name, >=) \
        ENUM__RELATIONAL_OPERATOR(name, ==) \
        ENUM__RELATIONAL_OPERATOR(name, !=)

//! Declaration of a single relational operator.
#define ENUM__RELATIONAL_OPERATOR(name, op) \
    bool operator op(EDomain other) const \
    { \
        return static_cast<int>(Value) op static_cast<int>(other); \
    }

//! \}
//! \endinternal

////////////////////////////////////////////////////////////////////////////////

//! Declares a strongly-typed enumeration.
/*!
 * \param name Name of the enumeration.
 * \param seq Enumeration domain encoded as a <em>sequence</em>.
 */
#define DECLARE_ENUM(name, seq) \
    BEGIN_DECLARE_ENUM(name, seq) \
        ENUM__RELATIONAL_OPERATORS(name) \
    END_DECLARE_ENUM()

//! Begins the declaration of a strongly-typed enumeration.
//! See #DECLARE_ENUM.
#define BEGIN_DECLARE_ENUM(name, seq) \
    ENUM__CLASS(name, seq)

//! Ends the declaration of a strongly-typed enumeration.
//! See #DECLARE_ENUM.
#define END_DECLARE_ENUM() \
    }

/*! \} */

//! Decomposes a composite enum value into elementary values.
/*!
 *  Every elementary value is assumed to be power of two.
 */
template <class T>
std::vector<T> DecomposeFlaggedEnum(T value)
{
    auto bits = T::GetDomainValues();
    std::vector<T> result;
    result.reserve(bits.size());
    FOREACH (auto bit, bits) {
        if ((value & bit) != 0) {
            result.push_back(bit);
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

