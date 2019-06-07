/* picoc data type module. This manages a tree of data types and has facilities
 * for parsing data types. */

#include "interpreter.h"

/* some basic types */
static int PointerAlignBytes;
static int IntAlignBytes;

int TypeIsNonDeterministic(struct ValueType *Typ) {
    return Typ->IsNonDet;
}

struct ValueType* TypeGetDeterministic(struct ParseState * Parser, struct ValueType * Typ) {
    if (!TypeIsNonDeterministic(Typ))
        return Typ;
    else {
        struct ValueType * Base;
        switch (Typ->Base){
            case TypeInt: Base = &Parser->pc->IntType; break;
            case TypeShort: Base = &Parser->pc->ShortType; break;
            case TypeChar: Base = &Parser->pc->CharType; break;
            case TypeLong: Base = &Parser->pc->LongType; break;
            case TypeLongLong: Base = &Parser->pc->LongLongType; break;
            case TypeUnsignedInt: Base = &Parser->pc->UnsignedIntType; break;
            case TypeUnsignedShort: Base = &Parser->pc->UnsignedShortType; break;
            case TypeUnsignedLong: Base = &Parser->pc->UnsignedLongType; break;
            case TypeUnsignedLongLong: Base = &Parser->pc->UnsignedLongLongType; break;
            case TypeUnsignedChar: Base = &Parser->pc->UnsignedCharType; break;
#ifndef NO_FP
            case TypeDouble: Base = &Parser->pc->DoubleType; break;
            case TypeFloat: Base = &Parser->pc->FloatType; break;
#endif
            default:
                fprintf(stderr, "Unsupported non-deterministic type conversion.\n");
                Base = Typ;
        }
        return Base;
    }
}

struct ValueType* TypeGetNonDeterministic(struct ParseState * Parser, struct ValueType * Typ) {
    if (TypeIsNonDeterministic(Typ))
        return Typ;
    else {
        struct ValueType * Base;
        switch (Typ->Base){
            case TypeInt: Base = &Parser->pc->IntNDType; break;
            case TypeShort: Base = &Parser->pc->ShortNDType; break;
            case TypeChar: Base = &Parser->pc->CharNDType; break;
            case TypeLong: Base = &Parser->pc->LongNDType; break;
            case TypeLongLong: Base = &Parser->pc->LongLongNDType; break;
            case TypeUnsignedInt: Base = &Parser->pc->UnsignedIntNDType; break;
            case TypeUnsignedShort: Base = &Parser->pc->UnsignedShortNDType; break;
            case TypeUnsignedLong: Base = &Parser->pc->UnsignedLongNDType; break;
            case TypeUnsignedLongLong: Base = &Parser->pc->UnsignedLongLongNDType; break;
            case TypeUnsignedChar: Base = &Parser->pc->UnsignedCharNDType; break;
            case TypeUnion:
            case TypeStruct: Base = Typ; break; // uninit struct/union should stay deterministic
#ifndef NO_FP
            case TypeDouble: Base = &Parser->pc->DoubleNDType; break;
            case TypeFloat: Base = &Parser->pc->FloatNDType; break;
#endif
            // function ptrs aren't supported to be ND - not even in SV-COMP
            case TypeFunctionPtr: Base = &Parser->pc->FunctionPtrType; break;
            default:
                fprintf(stderr, "Unsupported non-deterministic type conversion.\n");
                Base = Typ;
        }
        return Base;
    }
}



/* add a new type to the set of types we know about */
struct ValueType *TypeAdd(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int Sizeof, int AlignBytes)
{
    struct ValueType *NewType = VariableAlloc(pc, Parser, sizeof(struct ValueType), TRUE);
    NewType->Base = Base;
    NewType->ArraySize = ArraySize;
    NewType->Sizeof = Sizeof;
    NewType->AlignBytes = AlignBytes;
    NewType->Identifier = Identifier;
    NewType->Members = NULL;
    NewType->FromType = ParentType;
    NewType->DerivedTypeList = NULL;
    NewType->OnHeap = TRUE;
    NewType->Next = ParentType->DerivedTypeList;
    ParentType->DerivedTypeList = NewType;
    
    return NewType;
}

/* given a parent type, get a matching derived type and make one if necessary.
 * Identifier should be registered with the shared string table. */
struct ValueType *TypeGetMatching(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int AllowDuplicates)
{
    int Sizeof;
    int AlignBytes;
    struct ValueType *ThisType = ParentType->DerivedTypeList;
    while (ThisType != NULL && (ThisType->Base != Base || ThisType->ArraySize != ArraySize || ThisType->Identifier != Identifier))
        ThisType = ThisType->Next;
    
    if (ThisType != NULL)
    {
        if (AllowDuplicates)
            return ThisType;
        else
            ProgramFail(Parser, "data type '%s' is already defined", Identifier);
    }
        
    switch (Base)
    {
        case TypePointer:   Sizeof = sizeof(void *); AlignBytes = PointerAlignBytes; break;
        case TypeArray:     Sizeof = ArraySize * ParentType->Sizeof; AlignBytes = ParentType->AlignBytes; break;
        case TypeEnum:      Sizeof = sizeof(int); AlignBytes = IntAlignBytes; break;
        default:            Sizeof = 0; AlignBytes = 0; break;      /* structs and unions will get bigger when we add members to them */
    }

    return TypeAdd(pc, Parser, ParentType, Base, ArraySize, Identifier, Sizeof, AlignBytes);
}

/* stack space used by a value */
int TypeStackSizeValue(struct Value *Val)
{
    if (Val != NULL && Val->ValOnStack)
        return TypeSizeValue(Val, FALSE);
    else
        return 0;
}

/* memory used by a value */
int TypeSizeValue(struct Value *Val, int Compact)
{
    if (IS_INTEGER_NUMERIC(Val) && !Compact)
        return sizeof(ALIGN_TYPE);     /* allow some extra room for type extension */
    else if (Val->Typ->Base != TypeArray)
        return Val->Typ->Sizeof;
    else
        return Val->Typ->FromType->Sizeof * Val->Typ->ArraySize;
}

/* memory used by a variable given its type and array size */
int TypeSize(struct ValueType *Typ, int ArraySize, int Compact)
{
    if (IS_INTEGER_NUMERIC_TYPE(Typ) && !Compact)
        return sizeof(ALIGN_TYPE);     /* allow some extra room for type extension */
    else if (Typ->Base != TypeArray)
        return Typ->Sizeof;
    else
        return Typ->FromType->Sizeof * ArraySize;
}

/* add a base type */
void TypeAddBaseType(Picoc *pc, struct ValueType *TypeNode, enum BaseType Base, int Sizeof, int AlignBytes, int IsNonDet)
{
    TypeNode->Base = Base;
    TypeNode->ArraySize = 0;
    TypeNode->Sizeof = Sizeof;
    TypeNode->AlignBytes = AlignBytes;
    TypeNode->Identifier = pc->StrEmpty;
    TypeNode->Members = NULL;
    TypeNode->FromType = NULL;
    TypeNode->DerivedTypeList = NULL;
    TypeNode->OnHeap = FALSE;
    TypeNode->IsNonDet = IsNonDet;
    TypeNode->Next = pc->UberType.DerivedTypeList;
    pc->UberType.DerivedTypeList = TypeNode;
}

/* initialise the type system */
void TypeInit(Picoc *pc)
{
    struct IntAlign { char x; int y; } ia;
    struct ShortAlign { char x; short y; } sa;
    struct CharAlign { char x; char y; } ca;
    struct LongAlign { char x; long y; } la;
    struct LongLongAlign { char x; long long y; } lla;
#ifndef NO_FP
    struct DoubleAlign { char x; double y; } da;
    struct FloatAlign { char x; float y; } fa;
#endif
    struct PointerAlign { char x; void *y; } pa;

    IntAlignBytes = (char *)&ia.y - &ia.x;
    PointerAlignBytes = (char *)&pa.y - &pa.x;
    
    pc->UberType.DerivedTypeList = NULL;
    TypeAddBaseType(pc, &pc->IntType, TypeInt, sizeof(int), IntAlignBytes, FALSE);
    TypeAddBaseType(pc, &pc->ShortType, TypeShort, sizeof(short), (char *) &sa.y - &sa.x, FALSE);
    TypeAddBaseType(pc, &pc->CharType, TypeChar, sizeof(char), (char *) &ca.y - &ca.x, FALSE);
    TypeAddBaseType(pc, &pc->LongType, TypeLong, sizeof(long), (char *) &la.y - &la.x, FALSE);
    TypeAddBaseType(pc, &pc->LongLongType, TypeLongLong, sizeof(long long), (char * ) &lla.y - &lla.x, FALSE);
    TypeAddBaseType(pc, &pc->UnsignedIntType, TypeUnsignedInt, sizeof(unsigned int), IntAlignBytes, FALSE);
    TypeAddBaseType(pc, &pc->UnsignedShortType, TypeUnsignedShort, sizeof(unsigned short), (char *) &sa.y - &sa.x, FALSE);
    TypeAddBaseType(pc, &pc->UnsignedLongType, TypeUnsignedLong, sizeof(unsigned long), (char *) &la.y - &la.x, FALSE);
    TypeAddBaseType(pc, &pc->UnsignedLongLongType, TypeUnsignedLongLong, sizeof(unsigned long long), (char * ) &lla.y - &lla.x, FALSE);
    TypeAddBaseType(pc, &pc->UnsignedCharType, TypeUnsignedChar, sizeof(unsigned char), (char *) &ca.y - &ca.x, FALSE);
    TypeAddBaseType(pc, &pc->VoidType, TypeVoid, 0, 1, FALSE);
    TypeAddBaseType(pc, &pc->FunctionType, TypeFunction, sizeof(int), IntAlignBytes, FALSE);
    TypeAddBaseType(pc, &pc->MacroType, TypeMacro, sizeof(int), IntAlignBytes, FALSE);
    TypeAddBaseType(pc, &pc->GotoLabelType, TypeGotoLabel, 0, 1, FALSE);
    TypeAddBaseType(pc, &pc->FunctionPtrType, TypeFunctionPtr, sizeof(char *), PointerAlignBytes, FALSE);
    TypeAddBaseType(pc, &pc->TypeType, Type_Type, sizeof(double), (char *) &da.y - &da.x, FALSE);  /* must be large enough to cast to a double */

    // NDs
    TypeAddBaseType(pc, &pc->IntNDType, TypeInt, sizeof(int), IntAlignBytes, TRUE);
    TypeAddBaseType(pc, &pc->ShortNDType, TypeShort, sizeof(short), (char *) &sa.y - &sa.x, TRUE);
    TypeAddBaseType(pc, &pc->CharNDType, TypeChar, sizeof(char), (char *) &ca.y - &ca.x, TRUE);
    TypeAddBaseType(pc, &pc->LongNDType, TypeLong, sizeof(long), (char *) &la.y - &la.x, TRUE);
    TypeAddBaseType(pc, &pc->LongLongNDType, TypeLongLong, sizeof(long long), (char *) &lla.y - &lla.x, TRUE);
    TypeAddBaseType(pc, &pc->UnsignedIntNDType, TypeUnsignedInt, sizeof(unsigned int), IntAlignBytes, TRUE);
    TypeAddBaseType(pc, &pc->UnsignedShortNDType, TypeUnsignedShort, sizeof(unsigned short), (char *) &sa.y - &sa.x, TRUE);
    TypeAddBaseType(pc, &pc->UnsignedCharNDType, TypeUnsignedChar, sizeof(unsigned char), (char *) &ca.y - &ca.x, TRUE);
    TypeAddBaseType(pc, &pc->UnsignedLongNDType, TypeUnsignedLong, sizeof(unsigned long), (char *) &la.y - &la.x, TRUE);
    TypeAddBaseType(pc, &pc->UnsignedLongLongNDType, TypeUnsignedLongLong, sizeof(unsigned long long), (char *) &lla.y - &lla.x, TRUE);

#ifndef NO_FP
    TypeAddBaseType(pc, &pc->DoubleType, TypeDouble, sizeof(double), (char *) &da.y - &da.x, FALSE);
    TypeAddBaseType(pc, &pc->FloatType, TypeFloat, sizeof(double), (char *) &fa.y - &fa.x, FALSE);
    // NDs
    TypeAddBaseType(pc, &pc->DoubleNDType, TypeDouble, sizeof(double), (char *) &da.y - &da.x, TRUE);
    TypeAddBaseType(pc, &pc->FloatNDType, TypeFloat, sizeof(double), (char *) &fa.y - &fa.x, TRUE);
#else
    TypeAddBaseType(pc, &pc->TypeType, Type_Type, sizeof(struct ValueType *), PointerAlignBytes);
#endif
    pc->CharArrayType = TypeAdd(pc, NULL, &pc->CharType, TypeArray, 0, pc->StrEmpty, sizeof(char), (char *)&ca.y - &ca.x);
    pc->CharPtrType = TypeAdd(pc, NULL, &pc->CharType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
    pc->CharPtrPtrType = TypeAdd(pc, NULL, pc->CharPtrType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
    pc->VoidPtrType = TypeAdd(pc, NULL, &pc->VoidType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
}

/* deallocate heap-allocated types */
void TypeCleanupNode(Picoc *pc, struct ValueType *Typ)
{
    struct ValueType *SubType;
    struct ValueType *NextSubType;
    
    /* clean up and free all the sub-nodes */
    for (SubType = Typ->DerivedTypeList; SubType != NULL; SubType = NextSubType)
    {
        NextSubType = SubType->Next;
        TypeCleanupNode(pc, SubType);
        if (SubType->OnHeap)
        {
            /* if it's a struct or union deallocate all the member values */
            if (SubType->Members != NULL)
            {
                VariableTableCleanup(pc, SubType->Members);
                HeapFreeMem(pc, SubType->Members);
            }

            /* free this node */
            HeapFreeMem(pc, SubType);
        }
    }
}

void TypeCleanup(Picoc *pc)
{
    TypeCleanupNode(pc, &pc->UberType);
}

/* parse a struct or union declaration */
void TypeParseStruct(struct ParseState *Parser, struct ValueType **Typ, int IsStruct)
{
    struct Value *LexValue;
    struct ValueType *MemberType;
    char *MemberIdentifier;
    char *StructIdentifier;
    struct Value *MemberValue;
    enum LexToken Token;
    int AlignBoundary;
    Picoc *pc = Parser->pc;
    
    Token = LexGetToken(Parser, &LexValue, FALSE);
    if (Token == TokenIdentifier)
    {
        LexGetToken(Parser, &LexValue, TRUE);
        StructIdentifier = LexValue->Val->Identifier;
        Token = LexGetToken(Parser, NULL, FALSE);
    }
    else
    {
        static char TempNameBuf[7] = "^s0000";
        StructIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }

    *Typ = TypeGetMatching(pc, Parser, &Parser->pc->UberType, IsStruct ? TypeStruct : TypeUnion, 0, StructIdentifier, TRUE);
    if (Token == TokenLeftBrace && (*Typ)->Members != NULL){
        fprintf(stderr, "Warning: data type '%s' is already defined. Will skip this.", StructIdentifier);
        while (LexGetToken(Parser, NULL, FALSE) != TokenRightBrace)
            LexGetToken(Parser, NULL, TRUE);
        LexGetToken(Parser, NULL, TRUE);
    }

    Token = LexGetToken(Parser, NULL, FALSE);
    if (Token != TokenLeftBrace)
    { 
        /* use the already defined structure */
#if 0
        if ((*Typ)->Members == NULL)
            ProgramFail(Parser, "structure '%s' isn't defined", LexValue->Val->Identifier);
#endif            
        return;
    }
    
    if (pc->TopStackFrame != NULL)
        ProgramFail(Parser, "struct/union definitions can only be globals");
        
    LexGetToken(Parser, NULL, TRUE);    
    (*Typ)->Members = VariableAlloc(pc, Parser, sizeof(struct Table) + STRUCT_TABLE_SIZE * sizeof(struct TableEntry), TRUE);
    (*Typ)->Members->HashTable = (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table));
    TableInitTable((*Typ)->Members, (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, TRUE);

    int IsConst;
    do {
        TypeParse(Parser, &MemberType, &MemberIdentifier, NULL, &IsConst);

        if (MemberType == NULL || MemberIdentifier == NULL)
            ProgramFail(Parser, "invalid type in struct");
        
        MemberValue = VariableAllocValueAndData(pc, Parser, sizeof(int), FALSE, NULL, TRUE, NULL);
        MemberValue->Typ = MemberType;
        if (IsStruct)
        { 
            /* allocate this member's location in the struct */
            AlignBoundary = MemberValue->Typ->AlignBytes;
            if (((*Typ)->Sizeof & (AlignBoundary-1)) != 0)
                (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary-1));
                
            MemberValue->Val->Integer = (*Typ)->Sizeof;
            (*Typ)->Sizeof += TypeSizeValue(MemberValue, TRUE);
        }
        else
        { 
            /* union members always start at 0, make sure it's big enough to hold the largest member */
            MemberValue->Val->Integer = 0;
            if (MemberValue->Typ->Sizeof > (*Typ)->Sizeof)
                (*Typ)->Sizeof = TypeSizeValue(MemberValue, TRUE);
        }
        MemberValue->ConstQualifier = IsConst;

        /* make sure to align to the size of the largest member's alignment */
        if ((*Typ)->AlignBytes < MemberValue->Typ->AlignBytes)
            (*Typ)->AlignBytes = MemberValue->Typ->AlignBytes;
        
        /* define it */
        if (!TableSet(pc, (*Typ)->Members, MemberIdentifier, MemberValue, Parser->FileName, Parser->Line, Parser->CharacterPos))
            ProgramFail(Parser, "member '%s' already defined", &MemberIdentifier);
            
        if (LexGetToken(Parser, NULL, TRUE) != TokenSemicolon)
            ProgramFail(Parser, "semicolon expected");
                    
    } while (LexGetToken(Parser, NULL, FALSE) != TokenRightBrace);
    
    /* now align the structure to the size of its largest member's alignment */
    AlignBoundary = (*Typ)->AlignBytes;
    if (((*Typ)->Sizeof & (AlignBoundary-1)) != 0)
        (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary-1));
    
    LexGetToken(Parser, NULL, TRUE);
}

/* create a system struct which has no user-visible members */
struct ValueType *TypeCreateOpaqueStruct(Picoc *pc, struct ParseState *Parser, const char *StructName, int Size)
{
    struct ValueType *Typ = TypeGetMatching(pc, Parser, &pc->UberType, TypeStruct, 0, StructName, FALSE);
    
    /* create the (empty) table */
    Typ->Members = VariableAlloc(pc, Parser, sizeof(struct Table) + STRUCT_TABLE_SIZE * sizeof(struct TableEntry), TRUE);
    Typ->Members->HashTable = (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table));
    TableInitTable(Typ->Members, (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, TRUE);
    Typ->Sizeof = Size;
    
    return Typ;
}

/* parse an enum declaration */
void TypeParseEnum(struct ParseState *Parser, struct ValueType **Typ)
{
    struct Value *LexValue;
    struct Value InitValue;
    enum LexToken Token;
    int EnumValue = 0;
    char *EnumIdentifier;
    Picoc *pc = Parser->pc;
    
    Token = LexGetToken(Parser, &LexValue, FALSE);
    if (Token == TokenIdentifier)
    {
        LexGetToken(Parser, &LexValue, TRUE);
        EnumIdentifier = LexValue->Val->Identifier;
        Token = LexGetToken(Parser, NULL, FALSE);
    }
    else
    {
        static char TempNameBuf[7] = "^e0000";
        EnumIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }

    TypeGetMatching(pc, Parser, &pc->UberType, TypeEnum, 0, EnumIdentifier, Token != TokenLeftBrace);
    *Typ = &pc->IntType;
    if (Token != TokenLeftBrace)
    { 
        /* use the already defined enum */
        if ((*Typ)->Members == NULL)
            ProgramFail(Parser, "enum '%s' isn't defined", EnumIdentifier);
            
        return;
    }
    
    if (pc->TopStackFrame != NULL)
        ProgramFail(Parser, "enum definitions can only be globals");
        
    LexGetToken(Parser, NULL, TRUE);    
    (*Typ)->Members = &pc->GlobalTable;
    memset((void *)&InitValue, '\0', sizeof(struct Value));
    InitValue.Typ = &pc->IntType;
    InitValue.Val = (union AnyValue *)&EnumValue;
    do {
        if (LexGetToken(Parser, &LexValue, TRUE) != TokenIdentifier)
            ProgramFail(Parser, "identifier expected");
        
        EnumIdentifier = LexValue->Val->Identifier;
        if (LexGetToken(Parser, NULL, FALSE) == TokenAssign)
        {
            LexGetToken(Parser, NULL, TRUE);
            EnumValue = ExpressionParseLongLong(Parser);
        }
        
        VariableDefine(pc, Parser, EnumIdentifier, &InitValue, NULL, FALSE);
            
        Token = LexGetToken(Parser, NULL, TRUE);
        if (Token != TokenComma && Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
        
        EnumValue++;
                    
    } while (Token == TokenComma);
}

/* parse a type - just the basic type */
int TypeParseFront(struct ParseState *Parser, struct ValueType **Typ, int *IsStatic, int *IsConst)
{
    struct ParseState Before;
    struct Value *LexerValue;
    enum LexToken Token;
    int Unsigned = FALSE;
    struct Value *VarValue;
    int StaticQualifier = FALSE;
    int ConstQualifier = FALSE;
    int LongQualifier = FALSE;
    int LongLongQualifier = FALSE;
    Picoc *pc = Parser->pc;
    *Typ = NULL;

    /* ignore leading type qualifiers */
    ParserCopy(&Before, Parser);
    Token = LexGetToken(Parser, &LexerValue, TRUE);
    while (Token == TokenStaticType || Token == TokenAutoType || Token == TokenRegisterType || Token == TokenExternType
            || Token == TokenConst)
    {
        if (Token == TokenStaticType)
            StaticQualifier = TRUE;
        if (Token == TokenConst)
            ConstQualifier = TRUE;
        Token = LexGetToken(Parser, &LexerValue, TRUE);
    }
    
    if (IsStatic != NULL)
        *IsStatic = StaticQualifier;

    if (Token == TokenLongType){
        if (LexGetToken(Parser, NULL, FALSE) != TokenIdentifier){
            LongQualifier = TRUE;
            Token = LexGetToken(Parser, NULL, TRUE);
            if (LexGetToken(Parser, NULL, FALSE) != TokenIdentifier){
                if (Token == TokenLongType){
                    LongLongQualifier = TRUE;
                    Token = LexGetToken(Parser, NULL, TRUE);
                }
            }
        }
    }
        
    /* handle signed/unsigned with no trailing type */
    if (Token == TokenSignedType || Token == TokenUnsignedType)
    {
        enum LexToken FollowToken = LexGetToken(Parser, &LexerValue, FALSE);
        Unsigned = (Token == TokenUnsignedType);
        
        if (FollowToken != TokenIntType && FollowToken != TokenLongType && FollowToken != TokenShortType && FollowToken != TokenCharType)
        {
            if (!LongLongQualifier) {
                if (Unsigned)
                    *Typ = &pc->UnsignedIntType;
                else
                    *Typ = &pc->IntType;
            } else {
                if (Unsigned)
                    *Typ = &pc->UnsignedLongLongType;
                else
                    *Typ = &pc->LongLongType;
            }
            return TRUE;
        }
        
        Token = LexGetToken(Parser, &LexerValue, TRUE);
    }

    switch (Token)
    {
        case TokenIntType:
            if (LongLongQualifier){
                *Typ = Unsigned ? &pc->UnsignedLongLongType : &pc->LongLongType;
            } else if (LongQualifier) {
                *Typ = Unsigned ? &pc->UnsignedLongType : &pc->LongType;
            } else {
                *Typ = Unsigned ? &pc->UnsignedIntType : &pc->IntType;
            }
            break;
        case TokenShortType: *Typ = Unsigned ? &pc->UnsignedShortType : &pc->ShortType;
            if (LexGetToken(Parser, NULL, FALSE) == TokenIntType)
                LexGetToken(Parser, NULL, TRUE);
            break;
        case TokenCharType: *Typ = Unsigned ? &pc->UnsignedCharType : &pc->CharType; break;
        case TokenLongType:
            if (LexGetToken(Parser, NULL, FALSE) == TokenLongType){
                LexGetToken(Parser, NULL, TRUE);
                *Typ = Unsigned ? &pc->UnsignedLongLongType : &pc->LongLongType;
            } else if (LongQualifier == TRUE){
                *Typ = Unsigned ? &pc->UnsignedLongLongType : &pc->LongLongType;
            } else {
                *Typ = Unsigned ? &pc->UnsignedLongType : &pc->LongType;
            }
            if (LexGetToken(Parser, NULL, FALSE) == TokenIntType)
                LexGetToken(Parser, NULL, TRUE);
            break;
#ifndef NO_FP
        case TokenFloatType: *Typ = &pc->FloatType; break;
        case TokenDoubleType: *Typ = &pc->DoubleType; break;
#endif
        case TokenVoidType: *Typ = &pc->VoidType; break;

        case TokenStructType: case TokenUnionType:
            if (*Typ != NULL)
                ProgramFail(Parser, "bad type declaration");

            TypeParseStruct(Parser, Typ, Token == TokenStructType);
            break;

        case TokenEnumType:
            if (*Typ != NULL)
                ProgramFail(Parser, "bad type declaration");

            TypeParseEnum(Parser, Typ);
            break;

        case TokenIdentifier:
            if (LongQualifier == TRUE){
                *Typ = Unsigned ? &pc->UnsignedLongType : &pc->LongType;
                break;
            }
            /* we already know it's a typedef-defined type because we got here */
            VariableGet(pc, Parser, LexerValue->Val->Identifier, &VarValue);
            *Typ = VarValue->Val->Typ;
            break;
        default: ParserCopy(Parser, &Before); return FALSE;
    }
    if (LexGetToken(Parser, NULL, FALSE) == TokenConst){
        LexGetToken(Parser, NULL, TRUE);
        if (ConstQualifier == TRUE)
            ProgramFail(Parser, "const already specified, consts are still experimental, so this might not be a real error");

        ConstQualifier = TRUE;
    }

    if (IsConst != NULL)
        *IsConst = ConstQualifier;
    return TRUE;
}

/* parse a type - the part at the end after the identifier. eg. array specifications etc. */
struct ValueType *TypeParseBack(struct ParseState *Parser, struct ValueType *FromType)
{
    enum LexToken Token;
    struct ParseState Before;

    ParserCopy(&Before, Parser);
    Token = LexGetToken(Parser, NULL, TRUE);
    if (Token == TokenLeftSquareBracket)
    {
        /* add another array bound */
        if (LexGetToken(Parser, NULL, FALSE) == TokenRightSquareBracket)
        {
            /* an unsized array */
            LexGetToken(Parser, NULL, TRUE);
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), TypeArray, 0, Parser->pc->StrEmpty, TRUE);
        }
        else
        {
            /* get a numeric array size */
            enum RunMode OldMode = Parser->Mode;
            int ArraySize;
            Parser->Mode = RunModeRun;
            ArraySize = ExpressionParseLongLong(Parser);
            Parser->Mode = OldMode;
            
            if (LexGetToken(Parser, NULL, TRUE) != TokenRightSquareBracket)
                ProgramFail(Parser, "']' expected");
            
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), TypeArray, ArraySize, Parser->pc->StrEmpty, TRUE);
        }
    }
    else
    {
        /* the type specification has finished */
        ParserCopy(Parser, &Before);
        return FromType;
    }
}

int TypeParseFunctionPointer(struct ParseState *Parser, struct ValueType *BasicType, struct ValueType **Type,
                             char **Identifier) {
    struct Value *LexValue;
    struct ParseState Before;
    *Identifier = Parser->pc->StrEmpty;
    *Type = BasicType;
    ParserCopy(&Before, Parser);

    while (LexGetToken(Parser, NULL, FALSE) == TokenAsterisk){
        LexGetToken(Parser, NULL, TRUE);
        if (*Type == NULL)
            ProgramFail(Parser, "bad type declaration");
        *Type = TypeGetMatching(Parser->pc, Parser, *Type, TypePointer, 0, Parser->pc->StrEmpty, TRUE);
    }
    enum LexToken Token = LexGetToken(Parser, NULL, TRUE);
    if (Token != TokenOpenBracket) goto ERROR;
    Token = LexGetToken(Parser, NULL, TRUE);
    if (Token != TokenAsterisk) goto ERROR;
    Token = LexGetToken(Parser, &LexValue, TRUE);

    *Type = &Parser->pc->FunctionPtrType;
    while (Token == TokenAsterisk){
        *Type = TypeGetMatching(Parser->pc, Parser, *Type, TypePointer, 0, Parser->pc->StrEmpty, TRUE);
        Token = LexGetToken(Parser, &LexValue, TRUE);
    }

    if (Token != TokenIdentifier)
        // a cast
        *Identifier = Parser->pc->StrEmpty;
    else {
        *Identifier = LexValue->Val->Identifier;
        *Type = TypeParseBack(Parser, *Type);
        Token = LexGetToken(Parser, &LexValue, TRUE);
    }

    if (Token != TokenCloseBracket)
        ProgramFail(Parser, ") expected after function pointer identifier");

    return TRUE;

    ERROR:
        ParserCopy(Parser, &Before);
        *Type = BasicType;
        return FALSE;
}

/* parse a type - the part which is repeated with each identifier in a declaration list */
void
TypeParseIdentPart(struct ParseState *Parser, struct ValueType *BasicTyp, struct ValueType **Typ, char **Identifier,
                   int *IsConst)
{
    struct ParseState Before;
    enum LexToken Token;
    struct Value *LexValue;
    int Done = FALSE;
    *Typ = BasicTyp;
    *Identifier = Parser->pc->StrEmpty;
    
    while (!Done)
    {
        ParserCopy(&Before, Parser);
        Token = LexGetToken(Parser, &LexValue, TRUE);
        switch (Token)
        {
            case TokenOpenBracket:
                if (*Typ != NULL)
                    ProgramFail(Parser, "bad type declaration");

                TypeParse(Parser, Typ, Identifier, NULL, IsConst);
                if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                    ProgramFail(Parser, "')' expected");
                break;
            case TokenAsterisk:
                if (*Typ == NULL)
                    ProgramFail(Parser, "bad type declaration");

                *Typ = TypeGetMatching(Parser->pc, Parser, *Typ, TypePointer, 0, Parser->pc->StrEmpty, TRUE);
                break;
            
            case TokenIdentifier:
                if (*Typ == NULL || *Identifier != Parser->pc->StrEmpty)
                    ProgramFail(Parser, "bad type declaration");
                
                *Identifier = LexValue->Val->Identifier;
                Done = TRUE;
                break;
                
            default: ParserCopy(Parser, &Before); Done = TRUE; break;
        }
    }
    
    if (*Typ == NULL)
        ProgramFail(Parser, "bad type declaration");

    if (*Identifier != Parser->pc->StrEmpty)
    { 
        /* parse stuff after the identifier */
        *Typ = TypeParseBack(Parser, *Typ);
    }
}

/* parse a type - a complete declaration including identifier */
void TypeParse(struct ParseState *Parser, struct ValueType **Typ, char **Identifier, int *IsStatic, int *IsConst)
{
    struct ValueType *BasicType;

    TypeParseFront(Parser, &BasicType, IsStatic, IsConst);

    if (!TypeParseFunctionPointer(Parser, BasicType, Typ, Identifier)){
        TypeParseIdentPart(Parser, BasicType, Typ, Identifier, IsConst);
    } else {
        struct Value * FuncValue = ParseFunctionDefinition(Parser, BasicType, *Identifier, TRUE);
        if (FuncValue != NULL) VariableFree(Parser->pc, FuncValue);

    }

}

/* check if a type has been fully defined - otherwise it's just a forward declaration */
int TypeIsForwardDeclared(struct ParseState *Parser, struct ValueType *Typ)
{
    if (Typ->Base == TypeArray)
        return TypeIsForwardDeclared(Parser, Typ->FromType);
    
    if ( (Typ->Base == TypeStruct || Typ->Base == TypeUnion) && Typ->Members == NULL)
        return TRUE;
        
    return FALSE;
}
