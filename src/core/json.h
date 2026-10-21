// ============================================================
//  Minimal JSON DOM - fast enough for multi-megabyte WolvenKit
//  exports, no dependencies.
//
//  Numbers keep their original text, so 64-bit values such as a
//  chunkMask of 18446744073709551615 survive without passing
//  through a double.
// ============================================================
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cp77 {
namespace json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Type type = Type::Null;
    bool b = false;
    std::string str;                                   // String: text, Number: raw literal
    std::vector<Value> arr;                            // Array
    std::vector<std::pair<std::string, Value>> obj;    // Object (order kept)

    bool isNull() const   { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const  { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }

    // Member lookup: exact match first, then case-insensitive.
    // Returns a shared null value when missing - chains never crash.
    const Value& operator[](const char* key) const;
    const Value& operator[](const std::string& key) const { return (*this)[key.c_str()]; }
    const Value& at(size_t i) const;
    bool has(const char* key) const;
    size_t size() const { return type == Type::Array ? arr.size() : (type == Type::Object ? obj.size() : 0); }

    double   asDouble(double def = 0.0) const;
    int64_t  asInt(int64_t def = 0) const;
    uint64_t asUInt64(uint64_t def = 0) const;        // number OR numeric string
    bool     asBool(bool def = false) const;
    const std::string& asString() const;               // empty unless String
};

const Value& NullValue();

// Parse; on failure returns false and fills `error` with the position.
bool Parse(const std::string& text, Value& out, std::string* error = nullptr);
bool ParseFile(const std::string& path, Value& out, std::string* error = nullptr);

// Serialise (compact) - used for tests and for the settings dump.
std::string Dump(const Value& v);

} // namespace json
} // namespace cp77
