// Implementation of the payload parsers.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "parse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace parse {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

bool decimal(const char* payload, size_t len, float& out) {
    if (payload == nullptr || len == 0) return false;

    // Copy into a bounded, null-terminated buffer. strtof needs a terminator
    // and the caller's buffer has none. 31 characters is far more than any
    // reading needs and keeps this off the heap.
    char buf[32];
    if (len >= sizeof(buf)) return false;
    std::memcpy(buf, payload, len);
    buf[len] = '\0';

    // Trim both ends. Leading whitespace strtof would skip anyway; trailing
    // whitespace it would leave in `end` and we would wrongly reject.
    size_t begin = 0;
    size_t stop  = len;
    while (begin < stop && isSpace(buf[begin])) ++begin;
    while (stop > begin && isSpace(buf[stop - 1])) --stop;
    if (begin == stop) return false;
    buf[stop] = '\0';

    // Every byte of the trimmed payload must be one a bare decimal can
    // contain. This rejects three things at once that strtof would otherwise
    // accept: an embedded null byte, C99 hex notation such as "0x1p0", and
    // the words "nan" and "inf".
    for (size_t i = begin; i < stop; ++i) {
        const char c = buf[i];
        const bool permitted = std::isdigit(static_cast<unsigned char>(c)) ||
                               c == '.' || c == '-' || c == '+' ||
                               c == 'e' || c == 'E';
        if (!permitted) return false;
    }

    char*       end = nullptr;
    const float v   = std::strtof(buf + begin, &end);

    if (end == buf + begin) return false;  // nothing consumed
    if (end != buf + stop) return false;   // trailing garbage
    if (!std::isfinite(v)) return false;

    out = v;
    return true;
}

}  // namespace parse
