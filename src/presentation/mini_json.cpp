// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/mini_json.hpp"

#include <cctype>
#include <stdexcept>

namespace olduvai::presentation {

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    JsonValue parse() {
        JsonValue v = value();
        skip_ws();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;
    int depth_ = 0;
    // Recursion cap: object()/array() recurse via value(); without a limit a
    // hand-edited file of ~100k '[' characters exhausts the stack — which no
    // catch(...) at the load site can survive.  64 is far beyond any real
    // menus.json nesting.
    static constexpr int kMaxDepth = 64;

    struct DepthGuard {
        int& d;
        explicit DepthGuard(Parser& p) : d(p.depth_) { ++d; }
        ~DepthGuard() { --d; }
    };

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("json: " + msg + " at offset " +
                                 std::to_string(i_));
    }
    void skip_ws() {
        while (i_ < s_.size() &&
               std::isspace(static_cast<unsigned char>(s_[i_])))
            ++i_;
    }
    char peek() { skip_ws(); return i_ < s_.size() ? s_[i_] : '\0'; }
    char get() { skip_ws(); return i_ < s_.size() ? s_[i_++] : '\0'; }
    void expect(char c) { if (get() != c) fail(std::string("expected '") + c + "'"); }

    JsonValue value() {
        if (depth_ >= kMaxDepth) fail("nesting too deep");
        DepthGuard guard(*this);
        switch (peek()) {
            case '{': return object();
            case '[': return array();
            case '"': { JsonValue v; v.type = JsonValue::Str; v.str = string(); return v; }
            case 't': case 'f': return boolean();
            case 'n': return null();
            default: return number();
        }
    }

    JsonValue object() {
        JsonValue v; v.type = JsonValue::Obj;
        expect('{');
        if (peek() == '}') { get(); return v; }
        while (true) {
            const std::string key = string();
            expect(':');
            v.obj[key] = value();
            const char c = get();
            if (c == '}') break;
            if (c != ',') fail("expected ',' or '}' in object");
        }
        return v;
    }

    JsonValue array() {
        JsonValue v; v.type = JsonValue::Arr;
        expect('[');
        if (peek() == ']') { get(); return v; }
        while (true) {
            v.arr.push_back(value());
            const char c = get();
            if (c == ']') break;
            if (c != ',') fail("expected ',' or ']' in array");
        }
        return v;
    }

    std::string string() {
        if (get() != '"') fail("expected string");
        std::string out;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i_ >= s_.size()) fail("bad escape");
                const char e = s_[i_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    JsonValue boolean() {
        JsonValue v; v.type = JsonValue::Bool;
        if (s_.compare(i_, 4, "true") == 0) { v.b = true; i_ += 4; }
        else if (s_.compare(i_, 5, "false") == 0) { v.b = false; i_ += 5; }
        else fail("invalid literal");
        return v;
    }

    JsonValue null() {
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return JsonValue{}; }
        fail("invalid literal");
    }

    JsonValue number() {
        skip_ws();
        const std::size_t start = i_;
        while (i_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[i_])) ||
                s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' ||
                s_[i_] == 'e' || s_[i_] == 'E'))
            ++i_;
        if (i_ == start) fail("invalid value");
        JsonValue v; v.type = JsonValue::Num;
        // stod throws logic_error-family exceptions ("1e999999", "-") that
        // callers catching only runtime_error would miss — rethrow as ours.
        try {
            v.num = std::stod(s_.substr(start, i_ - start));
        } catch (const std::exception&) {
            fail("invalid number");
        }
        return v;
    }
};

}  // namespace

JsonValue parse_json(const std::string& text) { return Parser(text).parse(); }

}  // namespace olduvai::presentation
