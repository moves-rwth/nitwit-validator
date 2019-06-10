#include <stdio.h>
#include <math.h>
void __VERIFIER_error(){printf("Error\n");}
double __VERIFIER_nondet_double(void){return 22.822146805041292;}
int __VERIFIER_nondet_int(void){return 0;}
void __VERIFIER_assume(int expression){printf("Assume %d\n", expression);}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }

#define GET_VALUE(Value)\
    ((Value)->Typ->Base == TypeInt ? Value->Val->Integer : (\
    (Value)->Typ->Base == TypeChar ? Value->Val->Character : (\
    (Value)->Typ->Base == TypeShort ? Value->Val->ShortInteger : (\
    (Value)->Typ->Base == TypeLong ? Value->Val->LongInteger : (\
    (Value)->Typ->Base == TypeUnsignedInt ? Value->Val->UnsignedInteger : (\
    (Value)->Typ->Base == TypeUnsignedChar ? Value->Val->UnsignedCharacter : (\
    (Value)->Typ->Base == TypeUnsignedShort ? Value->Val->UnsignedShortInteger : (\
    (Value)->Typ->Base == TypeUnsignedLong ? Value->Val->UnsignedLongInteger : 0 ))))))))


/* values */
enum BaseType
{
    TypeVoid,                   /* no type */
    TypeInt,                    /* integer */
    TypeShort,                  /* short integer */
    TypeChar,                   /* a single character (signed) */
    TypeLong,                   /* long integer */
    TypeLongLong,                   /* long long integer */
    TypeUnsignedInt,            /* unsigned integer */
    TypeUnsignedShort,          /* unsigned short integer */
    TypeUnsignedChar,           /* unsigned 8-bit number */ /* must be before unsigned long */
    TypeUnsignedLong,           /* unsigned long integer */
    TypeUnsignedLongLong,           /* unsigned long long integer */
#ifndef NO_FP
    TypeDouble,                     /* floating point */
    TypeFloat,                     /* floating point */
#endif
    TypeFunction,               /* a function */
    TypeFunctionPtr,               /* a function pointer*/
    TypeMacro,                  /* a macro */
    TypePointer,                /* a pointer */
    TypeArray,                  /* an array of a sub-type */
    TypeStruct,                 /* aggregate type */
    TypeUnion,                  /* merged type */
    TypeEnum,                   /* enumerated integer type */
    TypeGotoLabel,              /* a label we can "goto" */
    Type_Type,                   /* a type for storing types */
};

/* data type */
struct ValueType
{
    enum BaseType Base;             /* what kind of type this is */
    int ArraySize;                  /* the size of an array type */
    int Sizeof;                     /* the storage required */
    int AlignBytes;                 /* the alignment boundary of this type */
    const char *Identifier;         /* the name of a struct or union */
    struct ValueType *FromType;     /* the type we're derived from (or nullptr) */
    struct ValueType *DerivedTypeList;  /* first in a list of types derived from this one */
    struct ValueType *Next;         /* next item in the derived type list */
    struct Table *Members;          /* members of a struct or union */
    int OnHeap;                     /* true if allocated on the heap */
    int StaticQualifier;            /* true if it's a static */
    // jsv
    char IsNonDet;                /* flag for when the variable is non-deterministic */
};

/* function definition */
struct FuncDef
{
    struct ValueType *ReturnType;   /* the return value type */
    int NumParams;                  /* the number of parameters */
    int VarArgs;                    /* has a variable number of arguments after the explicitly specified ones */
    struct ValueType **ParamType;   /* array of parameter types */
    char **ParamName;               /* array of parameter names */
};

/* macro definition */
struct MacroDef
{
    int NumParams;                  /* the number of parameters */
    char **ParamName;               /* array of parameter names */
};

/* values */
union AnyValue
{
    char Character;
    short ShortInteger;
    int Integer;
    long LongInteger;
    long long LongLongInteger;
    unsigned short UnsignedShortInteger;
    unsigned int UnsignedInteger;
    unsigned long UnsignedLongInteger;
    unsigned long UnsignedLongLongInteger;
    unsigned char UnsignedCharacter;
#ifndef NO_FP
    double Double;
    float Float;
#endif
    void *Pointer;                  /* unsafe native pointers */
    char *Identifier;
    char ArrayMem[2];               /* placeholder for where the data starts, doesn't point to it */
    struct ValueType *Typ;
    struct FuncDef FuncDef;
    struct MacroDef MacroDef;
};

struct Value
{
    struct ValueType *Typ;          /* the type of this value */
    union AnyValue *Val;            /* pointer to the AnyValue which holds the actual content */
    struct Value *LValueFrom;       /* if an LValue, this is a Value our LValue is contained within (or nullptr) */
    char ValOnHeap;                 /* this Value is on the heap */
    char ValOnStack;                /* the AnyValue is on the stack along with this Value */
    char AnyValOnHeap;              /* the AnyValue is separately allocated from the Value on the heap */
    char IsLValue;                  /* is modifiable and is allocated somewhere we can usefully modify it */
    int ScopeID;                    /* to know when it goes out of scope */
    char OutOfScope;
    // jsv
    char * VarIdentifier;           /* keeps track of the name of the variable this value belongs to */
    char ConstQualifier;            /* true if it's a const */
};

int main() {
    struct ValueType l, ul;
    l.Base = TypeLong;
    ul.Base = TypeUnsignedLong;
    struct Value t, b;
    t.Typ = &ul;
    b.Typ = &l;
    union AnyValue lv, ulv;
    lv.LongInteger = -1;
    ulv.LongInteger = 4294967295UL;
    t.Val = &ulv;
    b.Val = &lv;

    struct Value * tp = &t, *bp = &b;
    printf("Equal? %d, %d, %d\n", GET_VALUE(tp) == GET_VALUE(bp), GET_VALUE(bp) == GET_VALUE(tp),
            tp->Val->UnsignedLongInteger == bp->Val->LongInteger);

    return (0);
    ERROR: __VERIFIER_error();
    return (-1);
}



