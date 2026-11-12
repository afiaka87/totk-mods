// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "TextFormat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace botw_abilities::support {
namespace {

constexpr int kMaxDecimals = 15;
constexpr double kWholeLimit = 18446744073709549568.0;  // The largest double below 2^64.
constexpr std::uint64_t kPowers[] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
    100000000ull, 1000000000ull, 10000000000ull, 100000000000ull,
    1000000000000ull, 10000000000000ull, 100000000000000ull, 1000000000000000ull,
};

enum class Size { Int, Char, Short, Long, Wide, Pointer };

struct Spec {
    bool left{}, plus{}, space{}, alternate{}, zero{};
    int width{}, precision = -1;
    Size size = Size::Int;
};

class Output {
public:
    Output(char* out, std::size_t size) : out_(out), room_(size ? size - 1 : 0), open_(size != 0) {}

    void put(char c) {
        if (count_ < room_) out_[count_] = c;
        ++count_;
    }

    void fill(char c, int count) {
        for (; count > 0; --count) put(c);
    }

    void text(const char* value, int count) {
        for (int i = 0; i < count; ++i) put(value[i]);
    }

    int finish() {
        if (open_) out_[std::min(count_, room_)] = '\0';
        return int(count_);
    }

private:
    char* out_;
    std::size_t room_, count_{};
    bool open_;
};

// Writes prefix, zeros, then body inside the field width.
void field(Output& out, const Spec& spec, const char* prefix, int prefixLength, int zeros, const char* body, int length) {
    const int used = prefixLength + zeros + length;
    const int spare = std::max(spec.width - used, 0);
    if (!spec.left) out.fill(' ', spare);
    out.text(prefix, prefixLength);
    out.fill('0', zeros);
    out.text(body, length);
    if (spec.left) out.fill(' ', spare);
}

int writeDigits(char* out, std::uint64_t value, unsigned base = 10, bool upper = false) {
    const char* const alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[24];
    int length = 0;
    do {
        reversed[length++] = alphabet[value % base];
        value /= base;
    } while (value != 0);
    std::reverse_copy(reversed, reversed + length, out);
    return length;
}

// The sign character a number leads with, or zero for none.
char leadingSign(const Spec& spec, bool negative) {
    if (negative) return '-';
    if (spec.plus) return '+';
    return spec.space ? ' ' : '\0';
}

void writeInteger(Output& out, const Spec& spec, std::uint64_t value, bool negative, unsigned base, bool upper) {
    char digits[24];
    const int length = value != 0 || spec.precision != 0 ? writeDigits(digits, value, base, upper) : 0;

    char prefix[3];
    int prefixLength = 0;
    if (const char sign = leadingSign(spec, negative)) prefix[prefixLength++] = sign;
    if (spec.alternate && base == 16 && value != 0) {
        prefix[prefixLength++] = '0';
        prefix[prefixLength++] = upper ? 'X' : 'x';
    }

    int zeros = std::max(spec.precision - length, 0);
    if (spec.alternate && base == 8 && zeros == 0 && (length == 0 || digits[0] != '0')) zeros = 1;
    if (spec.zero && !spec.left && spec.precision < 0) zeros = std::max(zeros, spec.width - prefixLength - length);
    field(out, spec, prefix, prefixLength, zeros, digits, length);
}

// value is finite, not negative, and below kWholeLimit.
int writeFixed(char* out, double value, int decimals, bool alternate) {
    auto whole = std::uint64_t(value);
    const double scaled = (value - double(whole)) * double(kPowers[decimals]);
    auto part = std::uint64_t(scaled);
    const double rest = scaled - double(part);
    if (rest > 0.5 || (rest == 0.5 && ((decimals > 0 ? part : whole) & 1) != 0)) ++part;
    if (part >= kPowers[decimals]) {
        part -= kPowers[decimals];
        ++whole;
    }
    int length = writeDigits(out, whole);
    if (decimals > 0 || alternate) out[length++] = '.';
    if (decimals > 0) {
        char digits[24];
        const int count = writeDigits(digits, part);
        std::fill_n(out + length, decimals - count, '0');
        std::copy(digits, digits + count, out + length + decimals - count);
        length += decimals;
    }
    return length;
}

// Scales value to one leading digit and returns its power of ten.
int normalize(double& value) {
    int exponent = 0;
    if (value == 0) return exponent;
    while (value >= 10) {
        value /= 10;
        ++exponent;
    }
    while (value < 1) {
        value *= 10;
        --exponent;
    }
    return exponent;
}

int writeExponent(char* out, int exponent, bool upper) {
    int length = 0;
    out[length++] = upper ? 'E' : 'e';
    out[length++] = exponent < 0 ? '-' : '+';
    const auto size = std::uint64_t(exponent < 0 ? -exponent : exponent);
    if (size < 10) out[length++] = '0';
    return length + writeDigits(out + length, size);
}

int writeScientific(char* out, double value, int decimals, bool alternate, int& exponent) {
    exponent = normalize(value);
    int length = writeFixed(out, value, decimals, alternate);
    if (out[0] == '1' && out[1] == '0') {
        ++exponent;
        length = writeFixed(out, value / 10, decimals, alternate);
    }
    return length;
}

int trimZeros(char* out, int length) {
    if (std::find(out, out + length, '.') == out + length) return length;
    while (out[length - 1] == '0') --length;
    if (out[length - 1] == '.') --length;
    return length;
}

// value is finite and not negative. out holds at least 64 characters.
int writeGeneral(char* out, double value, int precision, bool alternate, bool upper) {
    const int digits = precision == 0 ? 1 : std::min(precision, kMaxDecimals + 1);
    int exponent = 0;
    int length = writeScientific(out, value, digits - 1, alternate, exponent);
    if (exponent < -4 || exponent >= digits) {
        if (!alternate) length = trimZeros(out, length);
        return length + writeExponent(out + length, exponent, upper);
    }
    length = writeFixed(out, value, std::min(digits - 1 - exponent, kMaxDecimals), alternate);
    return alternate ? length : trimZeros(out, length);
}

void writeFloat(Output& out, Spec spec, double value, char conversion) {
    const bool upper = conversion >= 'A' && conversion <= 'Z';
    const char kind = upper ? char(conversion + ('a' - 'A')) : conversion;
    const bool negative = std::signbit(value);
    const char sign = leadingSign(spec, negative);
    const int signLength = sign != 0 ? 1 : 0;

    if (std::isnan(value) || std::isinf(value)) {
        const char* const word = std::isnan(value) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        field(out, spec, &sign, signLength, 0, word, 3);
        return;
    }

    const double size = negative ? -value : value;
    const int precision = spec.precision < 0 ? 6 : spec.precision;
    const int decimals = std::min(precision, kMaxDecimals);
    char body[64];
    int length = 0;
    if (kind == 'g') {
        length = writeGeneral(body, size, precision, spec.alternate, upper);
    } else if (kind == 'e' || size >= kWholeLimit) {
        int exponent = 0;
        length = writeScientific(body, size, decimals, spec.alternate, exponent);
        length += writeExponent(body + length, exponent, upper);
    } else {
        length = writeFixed(body, size, decimals, spec.alternate);
    }
    const int zeros = spec.zero && !spec.left ? std::max(spec.width - signLength - length, 0) : 0;
    field(out, spec, &sign, signLength, zeros, body, length);
}

void writeString(Output& out, Spec spec, const char* value) {
    if (value == nullptr) value = "(null)";
    int length = 0;
    while ((spec.precision < 0 || length < spec.precision) && value[length] != '\0') ++length;
    field(out, spec, nullptr, 0, 0, value, length);
}

std::int64_t readSigned(std::va_list& args, Size size) {
    switch (size) {
    case Size::Char: return static_cast<signed char>(va_arg(args, int));
    case Size::Short: return short(va_arg(args, int));
    case Size::Long: return va_arg(args, long);
    case Size::Wide: return va_arg(args, long long);
    case Size::Pointer: return std::int64_t(va_arg(args, std::ptrdiff_t));
    case Size::Int: break;
    }
    return va_arg(args, int);
}

std::uint64_t readUnsigned(std::va_list& args, Size size) {
    switch (size) {
    case Size::Char: return static_cast<unsigned char>(va_arg(args, unsigned));
    case Size::Short: return static_cast<unsigned short>(va_arg(args, unsigned));
    case Size::Long: return va_arg(args, unsigned long);
    case Size::Wide: return va_arg(args, unsigned long long);
    case Size::Pointer: return std::uint64_t(va_arg(args, std::size_t));
    case Size::Int: break;
    }
    return va_arg(args, unsigned);
}

bool readFlag(Spec& spec, char c) {
    switch (c) {
    case '-': spec.left = true; return true;
    case '+': spec.plus = true; return true;
    case ' ': spec.space = true; return true;
    case '#': spec.alternate = true; return true;
    case '0': spec.zero = true; return true;
    default: return false;
    }
}

int readNumber(const char*& format, std::va_list& args) {
    if (*format == '*') {
        ++format;
        return va_arg(args, int);
    }
    int value = 0;
    for (; *format >= '0' && *format <= '9'; ++format) value = value * 10 + (*format - '0');
    return value;
}

Size readSize(const char*& format) {
    switch (*format) {
    case 'h':
        ++format;
        if (*format != 'h') return Size::Short;
        ++format;
        return Size::Char;
    case 'l':
        ++format;
        if (*format != 'l') return Size::Long;
        ++format;
        return Size::Wide;
    case 'j': ++format; return Size::Wide;
    case 'z':
    case 't': ++format; return Size::Pointer;
    default: return Size::Int;
    }
}

Spec readSpec(const char*& format, std::va_list& args) {
    Spec spec;
    while (readFlag(spec, *format)) ++format;
    spec.width = readNumber(format, args);
    if (spec.width < 0) {
        spec.left = true;
        spec.width = -spec.width;
    }
    if (*format == '.') {
        ++format;
        spec.precision = readNumber(format, args);
    }
    spec.size = readSize(format);
    return spec;
}

void writeSigned(Output& out, const Spec& spec, std::int64_t value) {
    const auto size = value < 0 ? 0 - std::uint64_t(value) : std::uint64_t(value);
    writeInteger(out, spec, size, value < 0, 10, false);
}

void writePointer(Output& out, Spec spec, std::uint64_t value) {
    spec.alternate = true;
    if (value != 0) writeInteger(out, spec, value, false, 16, false);
    else field(out, spec, "0x", 2, 0, "0", 1);
}

// False for an unknown conversion, which reads no argument.
bool writeConversion(Output& out, const Spec& spec, char conversion, std::va_list& args) {
    switch (conversion) {
    case '%': out.put('%'); return true;
    case 'd':
    case 'i': writeSigned(out, spec, readSigned(args, spec.size)); return true;
    case 'u': writeInteger(out, spec, readUnsigned(args, spec.size), false, 10, false); return true;
    case 'o': writeInteger(out, spec, readUnsigned(args, spec.size), false, 8, false); return true;
    case 'x': writeInteger(out, spec, readUnsigned(args, spec.size), false, 16, false); return true;
    case 'X': writeInteger(out, spec, readUnsigned(args, spec.size), false, 16, true); return true;
    case 'p': writePointer(out, spec, reinterpret_cast<std::uintptr_t>(va_arg(args, void*))); return true;
    case 'c': {
        const char value = char(va_arg(args, int));
        field(out, spec, nullptr, 0, 0, &value, 1);
        return true;
    }
    case 's': writeString(out, spec, va_arg(args, const char*)); return true;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G': writeFloat(out, spec, va_arg(args, double), conversion); return true;
    default: return false;
    }
}

}

int formatText(char* destination, std::size_t size, const char* format, std::va_list args) {
    Output out(destination, size);
    while (*format != '\0') {
        if (*format != '%') {
            out.put(*format++);
            continue;
        }
        const char* const start = format++;
        const Spec spec = readSpec(format, args);
        const char conversion = *format;
        if (conversion == '\0') {
            out.text(start, int(format - start));
            break;
        }
        ++format;
        if (!writeConversion(out, spec, conversion, args)) out.text(start, int(format - start));
    }
    return out.finish();
}

}
