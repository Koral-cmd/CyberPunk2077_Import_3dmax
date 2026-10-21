#include "json.h"
#include "util.h"

#include <cstdlib>
#include <cstring>

namespace cp77 {
namespace json {

const Value& NullValue() {
    static const Value v;
    return v;
}

static bool KeyIEq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

const Value& Value::operator[](const char* key) const {
    if (type != Type::Object) return NullValue();
    for (const auto& kv : obj) if (kv.first == key) return kv.second;
    for (const auto& kv : obj) if (KeyIEq(kv.first, key)) return kv.second;
    return NullValue();
}
bool Value::has(const char* key) const { return &(*this)[key] != &NullValue(); }
const Value& Value::at(size_t i) const {
    if (type != Type::Array || i >= arr.size()) return NullValue();
    return arr[i];
}
double Value::asDouble(double def) const {
    if (type == Type::Number || type == Type::String) {
        double d;
        return ParseDouble(str, d) ? d : def;
    }
    if (type == Type::Bool) return b ? 1.0 : 0.0;
    return def;
}
int64_t Value::asInt(int64_t def) const {
    if (type == Type::Number || type == Type::String) {
        if (str.empty()) return def;
        if (str.find_first_of(".eE") != std::string::npos) return static_cast<int64_t>(asDouble(static_cast<double>(def)));
        char* end = nullptr;
        const long long v = std::strtoll(str.c_str(), &end, 10);
        return (end && end != str.c_str()) ? static_cast<int64_t>(v) : def;
    }
    if (type == Type::Bool) return b ? 1 : 0;
    return def;
}
uint64_t Value::asUInt64(uint64_t def) const {
    if (type == Type::Number || type == Type::String) {
        std::string s = Trim(str);
        if (s.empty()) return def;
        if (s[0] == '-') return static_cast<uint64_t>(asInt(0));
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
        return (end && end != s.c_str()) ? static_cast<uint64_t>(v) : def;
    }
    return def;
}
bool Value::asBool(bool def) const {
    if (type == Type::Bool) return b;
    if (type == Type::Number) return asDouble() != 0.0;
    if (type == Type::String) return IEquals(str, "true") || str == "1";
    return def;
}
const std::string& Value::asString() const {
    static const std::string empty;
    return type == Type::String ? str : empty;
}

// ---- parser ---------------------------------------------------------
namespace {
struct Parser {
    const char* p;
    const char* begin;
    const char* end;
    std::string err;
    int depth = 0;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool fail(const char* what) {
        if (err.empty()) err = Format("%s at byte %zu", what, static_cast<size_t>(p - begin));
        return false;
    }
    static void put(std::string& s, uint32_t cp) {
        if (cp < 0x80) s.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { s.push_back(static_cast<char>(0xC0 | (cp >> 6))); s.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(uint32_t& v) {
        if (end - p < 4) return fail("short \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        ++p;   // opening quote
        out.clear();
        const char* run = p;
        while (p < end) {
            const char c = *p;
            if (c == '"') { out.append(run, p); ++p; return true; }
            if (c == '\\') {
                out.append(run, p);
                ++p;
                if (p >= end) return fail("unterminated escape");
                const char e = *p++;
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put(out, cp);
                    break;
                }
                default: return fail("unknown escape");
                }
                run = p;
                continue;
            }
            ++p;
        }
        return fail("unterminated string");
    }
    bool value(Value& v) {
        ws();
        if (p >= end) return fail("unexpected end");
        if (++depth > 512) return fail("nesting too deep");
        bool ok = true;
        const char c = *p;
        if (c == '{') {
            v.type = Type::Object;
            ++p;
            ws();
            if (p < end && *p == '}') { ++p; --depth; return true; }
            for (;;) {
                ws();
                if (p >= end || *p != '"') { ok = fail("expected key"); break; }
                v.obj.emplace_back();
                if (!string(v.obj.back().first)) { ok = false; break; }
                ws();
                if (p >= end || *p != ':') { ok = fail("expected ':'"); break; }
                ++p;
                if (!value(v.obj.back().second)) { ok = false; break; }
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                ok = fail("expected ',' or '}'");
                break;
            }
        } else if (c == '[') {
            v.type = Type::Array;
            ++p;
            ws();
            if (p < end && *p == ']') { ++p; --depth; return true; }
            for (;;) {
                v.arr.emplace_back();
                if (!value(v.arr.back())) { ok = false; break; }
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                ok = fail("expected ',' or ']'");
                break;
            }
        } else if (c == '"') {
            v.type = Type::String;
            ok = string(v.str);
        } else if (c == 't' && end - p >= 4 && std::memcmp(p, "true", 4) == 0) {
            v.type = Type::Bool; v.b = true; p += 4;
        } else if (c == 'f' && end - p >= 5 && std::memcmp(p, "false", 5) == 0) {
            v.type = Type::Bool; v.b = false; p += 5;
        } else if (c == 'n' && end - p >= 4 && std::memcmp(p, "null", 4) == 0) {
            v.type = Type::Null; p += 4;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const char* s = p;
            ++p;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
            v.type = Type::Number;
            v.str.assign(s, p);
        } else if (c == 'N' && end - p >= 3 && std::memcmp(p, "NaN", 3) == 0) {
            v.type = Type::Number; v.str = "0"; p += 3;          // .NET writes NaN for broken floats
        } else {
            ok = fail("unexpected character");
        }
        --depth;
        return ok;
    }
};

void DumpTo(const Value& v, std::string& o) {
    switch (v.type) {
    case Type::Null: o += "null"; break;
    case Type::Bool: o += v.b ? "true" : "false"; break;
    case Type::Number: o += v.str; break;
    case Type::String:
        o += '"';
        for (char c : v.str) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\t') o += "\\t";
            else if (static_cast<unsigned char>(c) < 0x20) o += Format("\\u%04x", c);
            else o += c;
        }
        o += '"';
        break;
    case Type::Array:
        o += '[';
        for (size_t i = 0; i < v.arr.size(); ++i) { if (i) o += ','; DumpTo(v.arr[i], o); }
        o += ']';
        break;
    case Type::Object:
        o += '{';
        for (size_t i = 0; i < v.obj.size(); ++i) {
            if (i) o += ',';
            Value k; k.type = Type::String; k.str = v.obj[i].first;
            DumpTo(k, o);
            o += ':';
            DumpTo(v.obj[i].second, o);
        }
        o += '}';
        break;
    }
}
} // namespace

bool Parse(const std::string& text, Value& out, std::string* error) {
    out = Value();
    Parser ps;
    ps.begin = text.data();
    ps.p = text.data();
    ps.end = text.data() + text.size();
    // UTF-8 byte order mark (File.WriteAllText does not write one, but be safe)
    if (ps.end - ps.p >= 3 && static_cast<unsigned char>(ps.p[0]) == 0xEF &&
        static_cast<unsigned char>(ps.p[1]) == 0xBB && static_cast<unsigned char>(ps.p[2]) == 0xBF) ps.p += 3;
    if (!ps.value(out)) { if (error) *error = ps.err; return false; }
    ps.ws();
    if (ps.p != ps.end) { if (error) *error = Format("trailing data at byte %zu", static_cast<size_t>(ps.p - ps.begin)); return false; }
    return true;
}

bool ParseFile(const std::string& path, Value& out, std::string* error) {
    std::string text;
    if (!ReadFile(path, text)) { if (error) *error = "cannot read " + path; return false; }
    return Parse(text, out, error);
}

std::string Dump(const Value& v) {
    std::string o;
    DumpTo(v, o);
    return o;
}

} // namespace json
} // namespace cp77
