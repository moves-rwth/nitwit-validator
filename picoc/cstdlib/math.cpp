/* stdio.h library for large systems - small embedded systems use clibrary.c instead */
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB
#ifndef NO_FP

static double M_EValue =        2.7182818284590452354;   /* e */
static double M_LOG2EValue =    1.4426950408889634074;   /* log_2 e */
static double M_LOG10EValue =   0.43429448190325182765;  /* log_10 e */
static double M_LN2Value =      0.69314718055994530942;  /* log_e 2 */
static double M_LN10Value =     2.30258509299404568402;  /* log_e 10 */
static double M_PIValue =       3.14159265358979323846;  /* pi */
static double M_PI_2Value =     1.57079632679489661923;  /* pi/2 */
static double M_PI_4Value =     0.78539816339744830962;  /* pi/4 */
static double M_1_PIValue =     0.31830988618379067154;  /* 1/pi */
static double M_2_PIValue =     0.63661977236758134308;  /* 2/pi */
static double M_2_SQRTPIValue = 1.12837916709551257390;  /* 2/sqrt(pi) */
static double M_SQRT2Value =    1.41421356237309504880;  /* sqrt(2) */
static double M_SQRT1_2Value =  0.70710678118654752440;  /* 1/sqrt(2) */

/* assign a floating point value */
double AssignFP(struct Value *DestValue, double FromFP)
{
    switch (DestValue->Typ->Base) {
        case TypeFloat:
            DestValue->Val->Float = (float) FromFP; break;
        default:
            DestValue->Val->Double = FromFP; break;
    }
    return FromFP;
}
double CoerceFP(struct Value* Val) {
    switch (Val->Typ->Base){
        case TypeFloat:
            return Val->Val->Float;
        default:
            return Val->Val->Double;
    }
}

void MathSin(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, sin(CoerceFP(Param[0])));
}

void MathCos(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, cos(CoerceFP(Param[0])));
}

void MathTan(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, tan(CoerceFP(Param[0])));
}

void MathAsin(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, asin(CoerceFP(Param[0])));
}

void MathAcos(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, acos(CoerceFP(Param[0])));
}

void MathAtan(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, atan(CoerceFP(Param[0])));
}

void MathAtan2(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, atan2(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathSinh(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, sinh(CoerceFP(Param[0])));
}

void MathCosh(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, cosh(CoerceFP(Param[0])));
}

void MathTanh(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, tanh(CoerceFP(Param[0])));
}

void MathExp(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, exp(CoerceFP(Param[0])));
}

void MathFabs(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, fabs(CoerceFP(Param[0])));
}

void MathFmod(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, fmod(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathFrexp(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, frexp(CoerceFP(Param[0]), Param[1]->Val->Pointer));
}

void MathLdexp(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, ldexp(CoerceFP(Param[0]), Param[1]->Val->Integer));
}

void MathLog(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, log(CoerceFP(Param[0])));
}

void MathLog10(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, log10(CoerceFP(Param[0])));
}

void MathModf(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, modf(CoerceFP(Param[0]), Param[0]->Val->Pointer));
}

void MathPow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, pow(CoerceFP(Param[0]), Param[1]->Val->Double));
}

void MathSqrt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, sqrt(CoerceFP(Param[0])));
}

void MathRound(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* this awkward definition of "round()" due to it being inconsistently
     * declared in math.h */
    AssignFP(ReturnValue, ceil(CoerceFP(Param[0]) - 0.5));
}

void MathCeil(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, ceil(CoerceFP(Param[0])));
}

void MathFloor(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    AssignFP(ReturnValue, floor(CoerceFP(Param[0])));
}

/* all math.h functions */
struct LibraryFunction MathFunctions[] =
{
    { MathAcos,         "float acos(float);" },
    { MathAsin,         "float asin(float);" },
    { MathAtan,         "float atan(float);" },
    { MathAtan2,        "float atan2(float, float);" },
    { MathCeil,         "float ceil(float);" },
    { MathCos,          "float cos(float);" },
    { MathCosh,         "float cosh(float);" },
    { MathExp,          "float exp(float);" },
    { MathFabs,         "float fabs(float);" },
    { MathFloor,        "float floor(float);" },
    { MathFmod,         "float fmod(float, float);" },
    { MathFrexp,        "float frexp(float, int *);" },
    { MathLdexp,        "float ldexp(float, int);" },
    { MathLog,          "float log(float);" },
    { MathLog10,        "float log10(float);" },
    { MathModf,         "float modf(float, float *);" },
    { MathPow,          "float pow(float,float);" },
    { MathRound,        "float round(float);" },
    { MathSin,          "float sin(float);" },
    { MathSinh,         "float sinh(float);" },
    { MathSqrt,         "float sqrt(float);" },
    { MathTan,          "float tan(float);" },
    { MathTanh,         "float tanh(float);" },
    { NULL,             NULL }
};

/* creates various system-dependent definitions */
void MathSetupFunc(Picoc *pc)
{
    VariableDefinePlatformVar(pc, NULL, "M_E", &pc->DoubleType, (union AnyValue *)&M_EValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_LOG2E", &pc->DoubleType, (union AnyValue *)&M_LOG2EValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_LOG10E", &pc->DoubleType, (union AnyValue *)&M_LOG10EValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_LN2", &pc->DoubleType, (union AnyValue *)&M_LN2Value, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_LN10", &pc->DoubleType, (union AnyValue *)&M_LN10Value, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_PI", &pc->DoubleType, (union AnyValue *)&M_PIValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_PI_2", &pc->DoubleType, (union AnyValue *)&M_PI_2Value, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_PI_4", &pc->DoubleType, (union AnyValue *)&M_PI_4Value, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_1_PI", &pc->DoubleType, (union AnyValue *)&M_1_PIValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_2_PI", &pc->DoubleType, (union AnyValue *)&M_2_PIValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_2_SQRTPI", &pc->DoubleType, (union AnyValue *)&M_2_SQRTPIValue, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_SQRT2", &pc->DoubleType, (union AnyValue *)&M_SQRT2Value, FALSE);
    VariableDefinePlatformVar(pc, NULL, "M_SQRT1_2", &pc->DoubleType, (union AnyValue *)&M_SQRT1_2Value, FALSE);
}

#endif /* !NO_FP */
#endif /* !BUILTIN_MINI_STDLIB */
