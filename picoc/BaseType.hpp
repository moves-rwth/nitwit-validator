
#ifndef NITWIT_PICOC_BASETYPE_H_
#define NITWIT_PICOC_BASETYPE_H_

enum class BaseType
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
    TypeDouble,                     /* floating point */
    TypeFloat,                     /* floating point */
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

template<typename T> BaseType resolveTypeToBaseType();

#endif
