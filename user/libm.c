#include "types.h"

typedef union {
    double number;
    uint64_t bits;
} DoubleBits;

static const double nova_pi = 3.14159265358979323846264338327950288;
static const double nova_ln2 = 0.693147180559945309417232121458176568;
static const double nova_inv_ln2 = 1.44269504088896340735992468100189214;

double fabs(double value) {
    DoubleBits bits = {value};
    bits.bits &= 0x7FFFFFFFFFFFFFFFUL;
    return bits.number;
}

double copysign(double magnitude, double sign) {
    DoubleBits output = {magnitude};
    DoubleBits input = {sign};
    output.bits = (output.bits & 0x7FFFFFFFFFFFFFFFUL) |
                  (input.bits & 0x8000000000000000UL);
    return output.number;
}

double sqrt(double value) {
    double result;
    __asm__ volatile ("sqrtsd %1, %0" : "=x"(result) : "x"(value));
    return result;
}

static double x87_round(double value, uint16_t mode) {
    uint16_t old_control;
    uint16_t new_control;
    double result;
    __asm__ volatile ("fnstcw %0" : "=m"(old_control));
    new_control = (uint16_t)((old_control & ~0x0C00U) | mode);
    __asm__ volatile ("fldcw %0" : : "m"(new_control));
    __asm__ volatile ("fldl %1; frndint; fstpl %0" : "=m"(result) : "m"(value) : "st");
    __asm__ volatile ("fldcw %0" : : "m"(old_control));
    return result;
}

double floor(double value) { return x87_round(value, 0x0400); }
double ceil(double value) { return x87_round(value, 0x0800); }
double trunc(double value) { return x87_round(value, 0x0C00); }
double round(double value) {
    return value < 0.0 ? ceil(value - 0.5) : floor(value + 0.5);
}
double rint(double value) { return x87_round(value, 0); }
double nearbyint(double value) { return x87_round(value, 0); }

double sin(double value) {
    double result;
    __asm__ volatile ("fldl %1; fsin; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}

double cos(double value) {
    double result;
    __asm__ volatile ("fldl %1; fcos; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}

double tan(double value) {
    double result;
    __asm__ volatile ("fldl %1; fptan; fstp %%st(0); fstpl %0"
                      : "=m"(result) : "m"(value) : "st");
    return result;
}

double atan2(double y, double x) {
    double result;
    __asm__ volatile ("fldl %1; fldl %2; fpatan; fstpl %0"
                      : "=m"(result) : "m"(y), "m"(x) : "st");
    return result;
}

double atan(double value) { return atan2(value, 1.0); }
double asin(double value) { return atan2(value, sqrt(1.0 - value * value)); }
double acos(double value) { return nova_pi * 0.5 - asin(value); }

double log(double value) {
    double result;
    __asm__ volatile ("fldln2; fldl %1; fyl2x; fstpl %0"
                      : "=m"(result) : "m"(value) : "st");
    return result;
}

double log2(double value) {
    double result;
    __asm__ volatile ("fld1; fldl %1; fyl2x; fstpl %0"
                      : "=m"(result) : "m"(value) : "st");
    return result;
}

double log10(double value) {
    double result;
    __asm__ volatile ("fldlg2; fldl %1; fyl2x; fstpl %0"
                      : "=m"(result) : "m"(value) : "st");
    return result;
}

double log1p(double value) {
    if (fabs(value) < 0.000001) {
        double square = value * value;
        return value - square * 0.5 + square * value / 3.0;
    }
    return log(1.0 + value);
}

double scalbn(double value, int exponent) {
    double result;
    __asm__ volatile ("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0"
                      : "=m"(result) : "m"(value), "m"(exponent) : "st");
    return result;
}

double ldexp(double value, int exponent) { return scalbn(value, exponent); }

double exp(double value) {
    if (value > 709.782712893384) {
        DoubleBits infinity = {.bits = 0x7FF0000000000000UL};
        return infinity.number;
    }
    if (value < -745.133219101941) return 0.0;
    int exponent = (int)(value * nova_inv_ln2 + (value >= 0.0 ? 0.5 : -0.5));
    double reduced = value - exponent * nova_ln2;
    double term = 1.0;
    double sum = 1.0;
    for (int index = 1; index <= 14; ++index) {
        term *= reduced / index;
        sum += term;
    }
    return scalbn(sum, exponent);
}

double exp2(double value) { return exp(value * nova_ln2); }
double expm1(double value) {
    if (fabs(value) < 0.000001) return value + value * value * 0.5;
    return exp(value) - 1.0;
}

double pow(double base, double exponent) {
    if (exponent == 0.0) return 1.0;
    if (base == 0.0) return exponent > 0.0 ? 0.0 : 1.0 / base;
    if (base < 0.0) {
        int64_t integer = (int64_t)exponent;
        if ((double)integer != exponent) {
            DoubleBits nan = {.bits = 0x7FF8000000000000UL};
            return nan.number;
        }
        double magnitude = exp(exponent * log(-base));
        return (integer & 1) ? -magnitude : magnitude;
    }
    return exp(exponent * log(base));
}

static double x87_remainder(double left, double right, bool nearest) {
    double result;
    if (nearest) {
        __asm__ volatile (
            "fldl %2; fldl %1; 1: fprem1; fnstsw %%ax; testb $4, %%ah; jnz 1b; "
            "fstpl %0; fstp %%st(0)"
            : "=m"(result) : "m"(left), "m"(right) : "ax", "st");
    } else {
        __asm__ volatile (
            "fldl %2; fldl %1; 1: fprem; fnstsw %%ax; testb $4, %%ah; jnz 1b; "
            "fstpl %0; fstp %%st(0)"
            : "=m"(result) : "m"(left), "m"(right) : "ax", "st");
    }
    return result;
}

double fmod(double left, double right) { return x87_remainder(left, right, false); }
double remainder(double left, double right) { return x87_remainder(left, right, true); }
double hypot(double left, double right) {
    left = fabs(left);
    right = fabs(right);
    if (left < right) {
        double swap = left;
        left = right;
        right = swap;
    }
    if (left == 0.0) return 0.0;
    double ratio = right / left;
    return left * sqrt(1.0 + ratio * ratio);
}

double sinh(double value) { return (exp(value) - exp(-value)) * 0.5; }
double cosh(double value) { return (exp(value) + exp(-value)) * 0.5; }
double tanh(double value) {
    double positive = exp(value);
    double negative = exp(-value);
    return (positive - negative) / (positive + negative);
}

double cbrt(double value) {
    if (value < 0.0) return -pow(-value, 1.0 / 3.0);
    return pow(value, 1.0 / 3.0);
}

double modf(double value, double *integer) {
    *integer = trunc(value);
    return value - *integer;
}

double frexp(double value, int *exponent) {
    DoubleBits bits = {value};
    uint64_t raw_exponent = (bits.bits >> 52) & 0x7FFU;
    if (!raw_exponent || raw_exponent == 0x7FFU) {
        *exponent = 0;
        return value;
    }
    *exponent = (int)raw_exponent - 1022;
    bits.bits = (bits.bits & 0x800FFFFFFFFFFFFFUL) | (1022UL << 52);
    return bits.number;
}

double nextafter(double from, double to) {
    if (from == to) return to;
    DoubleBits bits = {from};
    if ((bits.bits & 0x7FFFFFFFFFFFFFFFUL) == 0) {
        bits.bits = (DoubleBits){to}.bits & 0x8000000000000000UL;
        bits.bits |= 1;
        return bits.number;
    }
    if ((from < to) == (from > 0.0)) ++bits.bits;
    else --bits.bits;
    return bits.number;
}

int __isnan(double value) {
    DoubleBits bits = {value};
    return (bits.bits & 0x7FFFFFFFFFFFFFFFUL) > 0x7FF0000000000000UL;
}
int __isinf(double value) {
    DoubleBits bits = {value};
    return (bits.bits & 0x7FFFFFFFFFFFFFFFUL) == 0x7FF0000000000000UL;
}
int __finite(double value) {
    DoubleBits bits = {value};
    return (bits.bits & 0x7FF0000000000000UL) != 0x7FF0000000000000UL;
}
int isnan(double value) { return __isnan(value); }
int isinf(double value) { return __isinf(value); }
int finite(double value) { return __finite(value); }

float sqrtf(float value) { return (float)sqrt(value); }
float sinf(float value) { return (float)sin(value); }
float cosf(float value) { return (float)cos(value); }
float tanf(float value) { return (float)tan(value); }
float asinf(float value) { return (float)asin(value); }
float acosf(float value) { return (float)acos(value); }
float atanf(float value) { return (float)atan(value); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float logf(float value) { return (float)log(value); }
float log2f(float value) { return (float)log2(value); }
float log10f(float value) { return (float)log10(value); }
float expf(float value) { return (float)exp(value); }
float exp2f(float value) { return (float)exp2(value); }
float powf(float base, float exponent) { return (float)pow(base, exponent); }
float floorf(float value) { return (float)floor(value); }
float ceilf(float value) { return (float)ceil(value); }
float truncf(float value) { return (float)trunc(value); }
float roundf(float value) { return (float)round(value); }
float fabsf(float value) { return value < 0.0f ? -value : value; }
float fmodf(float left, float right) { return (float)fmod(left, right); }
float hypotf(float left, float right) { return (float)hypot(left, right); }
