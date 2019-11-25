/* stdio.h library for large systems - small embedded systems use clibrary.c instead */
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB
#ifndef NO_FP

static double M_EValue = 2.7182818284590452354;   /* e */
static double M_LOG2EValue = 1.4426950408889634074;   /* log_2 e */
static double M_LOG10EValue = 0.43429448190325182765;  /* log_10 e */
static double M_LN2Value = 0.69314718055994530942;  /* log_e 2 */
static double M_LN10Value = 2.30258509299404568402;  /* log_e 10 */
static double M_PIValue = 3.14159265358979323846;  /* pi */
static double M_PI_2Value = 1.57079632679489661923;  /* pi/2 */
static double M_PI_4Value = 0.78539816339744830962;  /* pi/4 */
static double M_1_PIValue = 0.31830988618379067154;  /* 1/pi */
static double M_2_PIValue = 0.63661977236758134308;  /* 2/pi */
static double M_2_SQRTPIValue = 1.12837916709551257390;  /* 2/sqrt(pi) */
static double M_SQRT2Value = 1.41421356237309504880;  /* sqrt(2) */
static double M_SQRT1_2Value = 0.70710678118654752440;  /* 1/sqrt(2) */
static double M_NAN = nan("");  /* NAN */
static double M_NAN2 = nan("");  /* NAN */

/* assign a floating point value */
double AssignFP(Value *DestValue, double FromFP) {
    switch (DestValue->Typ->Base) {
        case TypeFloat:
            DestValue->Val->Float = (float) FromFP;
            break;
        default:
            DestValue->Val->Double = FromFP;
            break;
    }
    return FromFP;
}

int AssignIntMath(Value *DestValue, int FromInt) {
    DestValue->Val->Integer = FromInt;
    return FromInt;
}

double CoerceFP(Value *Val) {
    switch (Val->Typ->Base) {
        case TypeFloat:
            return Val->Val->Float;
        default:
            return Val->Val->Double;
    }
}

void MathSin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, sin(CoerceFP(Param[0])));
}

void MathCos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, cos(CoerceFP(Param[0])));
}

void MathTan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, tan(CoerceFP(Param[0])));
}

void MathAsin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, asin(CoerceFP(Param[0])));
}

void MathAcos(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, acos(CoerceFP(Param[0])));
}

void MathAtan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, atan(CoerceFP(Param[0])));
}

void MathAtan2(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, atan2(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathSinh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, sinh(CoerceFP(Param[0])));
}

void MathCosh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, cosh(CoerceFP(Param[0])));
}

void MathTanh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, tanh(CoerceFP(Param[0])));
}

void MathAsinh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, asinh(CoerceFP(Param[0])));
}

void MathAcosh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, acosh(CoerceFP(Param[0])));
}

void MathAtanh(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, tanh(CoerceFP(Param[0])));
}

void MathExp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, exp(CoerceFP(Param[0])));
}

void MathFabs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fabs(CoerceFP(Param[0])));
}

void MathFmod(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fmod(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathFrexp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, frexp(CoerceFP(Param[0]), (int *) Param[1]->Val->Pointer));
}

void MathLdexp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, ldexp(CoerceFP(Param[0]), Param[1]->Val->Integer));
}

void MathLog(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, log(CoerceFP(Param[0])));
}

void MathLog10(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, log10(CoerceFP(Param[0])));
}

void MathModf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, modf(CoerceFP(Param[0]), (double *) Param[1]->Val->Pointer));
}

void MathPow(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, pow(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathSqrt(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, sqrt(CoerceFP(Param[0])));
}

void MathRound(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, round(CoerceFP(Param[0])));
}

void MathCeil(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, ceil(CoerceFP(Param[0])));
}

void MathFloor(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, floor(CoerceFP(Param[0])));
}

void MathExp2(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, exp2(CoerceFP(Param[0])));
}

void MathExpM1(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, expm1(CoerceFP(Param[0])));
}

void MathIlogb(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, ilogb(CoerceFP(Param[0])));
}

void MathLog1p(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, log1p(CoerceFP(Param[0])));
}

void MathLog2(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, log2(CoerceFP(Param[0])));
}

void MathLogb(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, logb(CoerceFP(Param[0])));
}

void MathScalbn(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, scalbn(CoerceFP(Param[0]), Param[1]->Val->LongInteger));
}

void MathScalbnln(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, scalbln(CoerceFP(Param[0]), Param[1]->Val->LongInteger));
}

void MathCbrt(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, cbrt(CoerceFP(Param[0])));
}

void MathHypot(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, hypot(CoerceFP(Param[0]), CoerceFP(Param[1])));
}


void MathTrunc(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, trunc(CoerceFP(Param[0])));
}

void MathRint(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, rint(CoerceFP(Param[0])));
}

void MathNearbyint(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, nearbyint(CoerceFP(Param[0])));
}

void MathRemainder(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, remainder(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathCopysign(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, copysign(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathNan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, nan((const char *) Param[0]->Val->Pointer));
}

void MathNextafter(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, nextafter(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathNexttoward(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, nexttoward(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathFdim(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fdim(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathFmax(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fmax(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathFmin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fmin(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathAbs(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, abs(CoerceFP(Param[0])));
}

void MathFma(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignFP(ReturnValue, fma(CoerceFP(Param[0]), CoerceFP(Param[1]), CoerceFP(Param[2])));
}

void MathFpclassify(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, fpclassify(CoerceFP(Param[0])));
}

void MathSignbit(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, signbit(CoerceFP(Param[0])));
}

void MathIsfinite(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isfinite(CoerceFP(Param[0])));
}

void MathIsinf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isinf(CoerceFP(Param[0])));
}

void MathIsnormal(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isnormal(CoerceFP(Param[0])));
}

void MathIsnan(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isnan(CoerceFP(Param[0])));
}

void MathIsgreater(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isgreater(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathIsgreaterequal(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isgreaterequal(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathIsless(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isless(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathIslessequal(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, islessequal(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathIslessgreater(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, islessgreater(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathIsunordered(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    AssignIntMath(ReturnValue, isunordered(CoerceFP(Param[0]), CoerceFP(Param[1])));
}

void MathLround(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    ReturnValue->Val->LongInteger = (long) round(CoerceFP(Param[0]));
}

void MathLLround(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    ReturnValue->Val->LongLongInteger = (long long) round(CoerceFP(Param[0]));
}

void MathLrint(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    ReturnValue->Val->LongInteger = (long) round(CoerceFP(Param[0]));
}

void MathLLrint(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs) {
    ReturnValue->Val->LongLongInteger = (long long) round(CoerceFP(Param[0]));
}

/* all math.h functions */
//  http://www.cplusplus.com/reference/cmath/
struct LibraryFunction MathFunctions[] =
        {
                // trigonometric
//                {MathCos,            "float cos(float);"},
//                {MathSin,            "float sin(float);"},
//                {MathTan,            "float tan(float);"},
//                {MathAcos,           "float acos(float);"},
//                {MathAsin,           "float asin(float);"},
//                {MathAtan,           "float atan(float);"},
//                {MathAtan2,          "float atan2(float);"},
//
//                // hyperbolic
//                {MathCosh,           "float cosh(float);"},
//                {MathSinh,           "float sinh(float);"},
//                {MathTanh,           "float tanh(float);"},
//                {MathAcosh,          "float acosh(float);"},
//                {MathAsinh,          "float asinh(float);"},
//                {MathAtanh,          "float atanh(float);"},
//
//                // exp, log
//                {MathExp,            "float exp(float);"},
//                {MathFrexp,          "float frexp(float, int *);"},
//                {MathLdexp,          "float ldexp(float, int);"},
//                {MathLog,            "float log(float);"},
//                {MathLog10,          "float log10(float);"},
////                {MathModf,       "double modf(double, double *);"},
//                {MathExp2,           "float exp2(float);"},
//                {MathExpM1,          "float expm1(float);"},
//                {MathIlogb,          "float ilogb(float);"},
//                {MathLog1p,          "float log1p(float);"},
//                {MathLog2,           "float log2(float);"},
//                {MathLogb,           "float logb(float);"},
//                {MathScalbn,         "float scalbn(float, long int);"},
//                {MathScalbnln,       "float scalbnln(float, long int);"},
//
//                // power
//                {MathPow,            "float pow(float,float);"},
//                {MathSqrt,           "float sqrt(float);"},
//                {MathCbrt,           "float cbrt(float);"},
//                {MathHypot,          "float hypot(float, float);"},
//
//                // error + gamma
//                // todo
//
//                // round, remainder
//                {MathCeil,           "float ceil(float);"},
//                {MathFloor,          "float floor(float);"},
//                {MathFmod,           "float fmod(float, float);"},
//                {MathRound,          "float round(float);"},
//                {MathLround,         "long lround(float);"},
//                {MathLLround,        "long long llround(float);"},
//                {MathRint,           "float rint(float);"},
//                {MathLrint,          "long lrint(float);"},
//                {MathLLrint,         "long long llrint(float);"},
//                {MathTrunc,          "float trunc(float);"},
//                {MathNearbyint,      "float nearbyint(float);"},
//                {MathRemainder,      "float remainder(float, float);"},
//
//                // floats
//                {MathCopysign,       "float copysign(float, float);"},
//                {MathNan,            "float nan(char *);"},
//                {MathNextafter,      "float nextafter(float, float);"},
//                {MathNexttoward,     "float nexttoward(float, float);"},
//
//                // min, max, difference
//                {MathFdim,           "float fdim(float, float);"},
//                {MathFmax,           "float fmin(float, float);"},
//                {MathFmin,           "float fmin(float, float);"},
//
//                // other
//                {MathFabs,           "float fabs(float);"},
//                {MathAbs,            "float abs(float);"},
//                {MathFma,            "float fma(float, float, float);"},
//
//                // MACROS
//                // classification
//                {MathFpclassify,     "int fpclassify(float);"},
//                {MathIsfinite,       "int isfinite(float);"},
//                {MathIsinf,          "int isinf(float);"},
//                {MathIsnan,          "int isnan(float);"},
//                {MathIsnormal,       "int isnormal(float);"},
//                {MathSignbit,        "int signbit(float);"},
//
//                // comparison
//                {MathIsgreater,      "int isgreater(float, float);"},
//                {MathIsgreaterequal, "int isgreaterequal(float, float);"},
//                {MathIsless,         "int isless(float, float);"},
//                {MathIslessequal,    "int islessequal(float, float);"},
//                {MathIslessgreater,  "int islessgreater(float, float);"},
//                {MathIsunordered,    "int isunordered(float, float);"},
//
                /////////////////////// unfortunately, sigsegvs happen when both float and double versions are added as functions //////////////////////////////
                // trigonometric
                {MathCos,            "double cos(double);"},
                {MathSin,            "double sin(double);"},
                {MathTan,            "double tan(double);"},
                {MathAcos,           "double acos(double);"},
                {MathAsin,           "double asin(double);"},
                {MathAtan,           "double atan(double);"},
                {MathAtan2,          "double atan2(double);"},

                // hyperbolic
                {MathCosh,           "double cosh(double);"},
                {MathSinh,           "double sinh(double);"},
                {MathTanh,           "double tanh(double);"},
                {MathAcosh,          "double acosh(double);"},
                {MathAsinh,          "double asinh(double);"},
                {MathAtanh,          "double atanh(double);"},

                // exp, log
                {MathExp,            "double exp(double);"},
                {MathFrexp,          "double frexp(double, int *);"},
                {MathLdexp,          "double ldexp(double, int);"},
                {MathLog,            "double log(double);"},
                {MathLog10,          "double log10(double);"},
                {MathModf,           "double modf(double, double *);"},
                {MathExp2,           "double exp2(double);"},
                {MathExpM1,          "double expm1(double);"},
                {MathIlogb,          "double ilogb(double);"},
                {MathLog1p,          "double log1p(double);"},
                {MathLog2,           "double log2(double);"},
                {MathLogb,           "double logb(double);"},
                {MathScalbn,         "double scalbn(double, long int);"},
                {MathScalbnln,       "double scalbnln(double, long int);"},

                // power
                {MathPow,            "double pow(double,double);"},
                {MathSqrt,           "double sqrt(double);"},
                {MathCbrt,           "double cbrt(double);"},
                {MathHypot,          "double hypot(double, double);"},

                // error + gamma
                // todo

                // round, remainder
                {MathCeil,           "double ceil(double);"},
                {MathFloor,          "double floor(double);"},
                {MathFmod,           "double fmod(double, double);"},
                {MathRound,          "double round(double);"},
                {MathLround,         "long lround(double);"},
                {MathLLround,        "long long llround(double);"},
                {MathRint,           "double rint(double);"},
                {MathLrint,          "long lrint(double);"},
                {MathLLrint,         "long long llrint(double);"},
                {MathTrunc,          "double trunc(double);"},
                {MathNearbyint,      "double nearbyint(double);"},
                {MathRemainder,      "double remainder(double, double);"},

                // doubles
                {MathCopysign,       "double copysign(double, double);"},
                {MathNan,            "double nan(char *);"},
                {MathNextafter,      "double nextafter(double, double);"},
                {MathNexttoward,     "double nexttoward(double, double);"},

                // min, max, difference
                {MathFdim,           "double fdim(double, double);"},
                {MathFmax,           "double fmax(double, double);"},
                {MathFmin,           "double fmin(double, double);"},

                // other
                {MathFabs,           "double fabs(double);"},
//                {MathAbs,            "double abs(double);"}, // todo SIGSEGV
                {MathFma,            "double fma(double, double, double);"},

                // MACROS
                // classification
                {MathFpclassify,     "int fpclassify(double);"},
                {MathIsfinite,       "int isfinite(double);"},
                {MathIsinf,          "int isinf(double);"},
                {MathIsnan,          "int isnan(double);"},
                {MathIsnormal,       "int isnormal(double);"},
                {MathSignbit,        "int signbit(double);"},

                // comparison
                {MathIsgreater,      "int isgreater(double, double);"},
                {MathIsgreaterequal, "int isgreaterequal(double, double);"},
                {MathIsless,         "int isless(double, double);"},
                {MathIslessequal,    "int islessequal(double, double);"},
                {MathIslessgreater,  "int islessgreater(double, double);"},
                {MathIsunordered,    "int isunordered(double, double);"},

                // end
                {nullptr,            nullptr}
        };

/* creates various system-dependent definitions */
void MathSetupFunc(Picoc *pc) {
    VariableDefinePlatformVar(pc, nullptr, "M_E", &pc->DoubleType, (union AnyValue *) &M_EValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_LOG2E", &pc->DoubleType, (union AnyValue *) &M_LOG2EValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_LOG10E", &pc->DoubleType, (union AnyValue *) &M_LOG10EValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_LN2", &pc->DoubleType, (union AnyValue *) &M_LN2Value, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_LN10", &pc->DoubleType, (union AnyValue *) &M_LN10Value, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_PI", &pc->DoubleType, (union AnyValue *) &M_PIValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_PI_2", &pc->DoubleType, (union AnyValue *) &M_PI_2Value, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_PI_4", &pc->DoubleType, (union AnyValue *) &M_PI_4Value, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_1_PI", &pc->DoubleType, (union AnyValue *) &M_1_PIValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_2_PI", &pc->DoubleType, (union AnyValue *) &M_2_PIValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_2_SQRTPI", &pc->DoubleType, (union AnyValue *) &M_2_SQRTPIValue, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_SQRT2", &pc->DoubleType, (union AnyValue *) &M_SQRT2Value, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "M_SQRT1_2", &pc->DoubleType, (union AnyValue *) &M_SQRT1_2Value, FALSE);

    VariableDefinePlatformVar(pc, nullptr, "NaN", &pc->DoubleType, (union AnyValue *) &M_NAN, FALSE);
    VariableDefinePlatformVar(pc, nullptr, "NAN", &pc->DoubleType, (union AnyValue *) &M_NAN2, FALSE);

    // todo - add the following if some programs require it
    // -- http://www.cplusplus.com/reference/cmath/math_errhandling/
    // -- http://www.cplusplus.com/reference/cmath/INFINITY/
    // -- http://www.cplusplus.com/reference/cmath/HUGE_VAL/
    // -- http://www.cplusplus.com/reference/cmath/HUGE_VALF/
    // -- http://www.cplusplus.com/reference/cmath/HUGE_VALL/
}

#endif /* !NO_FP */
#endif /* !BUILTIN_MINI_STDLIB */
