/* picoc data type module. This manages a tree of data types and has facilities
 * for parsing data types. */

#include "interpreter.hpp"

#include "CoerceT.hpp"

/* flag if we do not accept zero element structs (Default: 0) */
#define NO_ZERO_STRUCT 0

/* some basic types */
static int PointerAlignBytes;
static int IntAlignBytes;

/* initialize NonDet Array List all nd (1) at start
 * change to d (0) when element is initiliazed (assigned)
 **/
void initNonDetList (ParseState * Parser, ValueType * Type, int ArraySize) {
    NonDetList * head = static_cast<NonDetList *>(VariableAlloc(Parser->pc, Parser, sizeof(NonDetList), TRUE));
    head->IsNonDet = true;

    NonDetList * temp = head;
    for (int i = 1; i < ArraySize; ++i) {
        NonDetList * tail = static_cast<NonDetList *>(VariableAlloc(Parser->pc, Parser, sizeof(NonDetList), TRUE));
        tail->IsNonDet = true;
        tail->Next = nullptr;
        temp->Next = tail;
        temp = temp->Next;
    }

    Type->NDList = head;
    Type->NDListSize = ArraySize;
}

void freeNonDetList(Picoc* pc, NonDetList* list) {
    if (list != nullptr) {
        NonDetList* next = list->Next;
        HeapFreeMem(pc, list);
        freeNonDetList(pc, next);
    }
}

void freeNonDetList(Picoc* pc, ValueType* Type) {
    freeNonDetList(pc, Type->NDList);
}

bool getNonDetListElement(NonDetList * List, int ArrayIndex) {
    if (List == nullptr) {
        std::cerr << "Internal Error: Access to NonDetList that is null!" << std::endl;
        return false;
    }
    struct NonDetList * current = List;
    for (int i=0; i<ArrayIndex; ++i) {
        current = current->Next;
        if (current == nullptr) {
            std::cerr << "Internal Error: Access to NonDetList that is null!" << std::endl;
            return false;
        }
    }
    return current->IsNonDet;
}

void setNonDetListElement(NonDetList * List, int ArrayIndex, bool nonDet) {
    if (List == nullptr) {
        std::cerr << "Internal Error: Access to NonDetList that is null!" << std::endl;
        return;
    }
    struct NonDetList * current = List;
    for (int i = 0; i < ArrayIndex; ++i) {
        current = current->Next;
        if (current == nullptr) {
            std::cerr << "Internal Error: Access to NonDetList that is null!" << std::endl;
            return;
        }
    }
    current->IsNonDet = nonDet;
}

bool TypeIsNonDeterministic(struct ValueType *Typ) {
    return Typ->IsNonDet;
}

int TypeIsUnsigned(struct ValueType * Typ) {
    switch (Typ->Base){
        case BaseType::TypeUnsignedInt: return true;
        case BaseType::TypeUnsignedShort: return true;
        case BaseType::TypeUnsignedLong: return true;
        case BaseType::TypeUnsignedLongLong: return true;
        case BaseType::TypeUnsignedChar: return true;
        default: return false;
    }
}

struct ValueType* TypeGetDeterministic(struct ParseState * Parser, struct ValueType * Typ) {
    if (!TypeIsNonDeterministic(Typ))
        return Typ;
    else {
        struct ValueType * Base;
        switch (Typ->Base){
            case BaseType::TypeInt: Base = &Parser->pc->IntType; break;
            case BaseType::TypeShort: Base = &Parser->pc->ShortType; break;
            case BaseType::TypeChar: Base = &Parser->pc->CharType; break;
            case BaseType::TypeLong: Base = &Parser->pc->LongType; break;
            case BaseType::TypeLongLong: Base = &Parser->pc->LongLongType; break;
            case BaseType::TypeUnsignedInt: Base = &Parser->pc->UnsignedIntType; break;
            case BaseType::TypeUnsignedShort: Base = &Parser->pc->UnsignedShortType; break;
            case BaseType::TypeUnsignedLong: Base = &Parser->pc->UnsignedLongType; break;
            case BaseType::TypeUnsignedLongLong: Base = &Parser->pc->UnsignedLongLongType; break;
            case BaseType::TypeUnsignedChar: Base = &Parser->pc->UnsignedCharType; break;
            case BaseType::TypeDouble: Base = &Parser->pc->DoubleType; break;
            case BaseType::TypeFloat: Base = &Parser->pc->FloatType; break;
            default:
                fprintf(stderr, "Unsupported non-deterministic type conversion from type %s.\n", getType(Typ));
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
        bool nondet = true;
        switch (Typ->Base){
            case BaseType::TypeInt: Base = &Parser->pc->IntNDType; break;
            case BaseType::TypeShort: Base = &Parser->pc->ShortNDType; break;
            case BaseType::TypeChar: Base = &Parser->pc->CharNDType; break;
            case BaseType::TypeLong: Base = &Parser->pc->LongNDType; break;
            case BaseType::TypeLongLong: Base = &Parser->pc->LongLongNDType; break;
            case BaseType::TypeUnsignedInt: Base = &Parser->pc->UnsignedIntNDType; break;
            case BaseType::TypeUnsignedShort: Base = &Parser->pc->UnsignedShortNDType; break;
            case BaseType::TypeUnsignedLong: Base = &Parser->pc->UnsignedLongNDType; break;
            case BaseType::TypeUnsignedLongLong: Base = &Parser->pc->UnsignedLongLongNDType; break;
            case BaseType::TypeUnsignedChar: Base = &Parser->pc->UnsignedCharNDType; break;
            case BaseType::TypeUnion:
            case BaseType::TypeStruct: Base = Typ; break; // uninit struct/union should stay deterministic
            case BaseType::TypeDouble: Base = &Parser->pc->DoubleNDType; break;
            case BaseType::TypeFloat: Base = &Parser->pc->FloatNDType; break;
            // TODO: array from type is deterministic -> nd in single values is missing
            case BaseType::TypeArray:
                Base = TypeGetMatching(Parser->pc, Parser,
                         TypeGetDeterministic(Parser, Typ->FromType),
                         Typ->Base, Typ->ArraySize, Typ->Identifier, TRUE, &nondet);
                if (Base->NDList == NULL) {
                    initNonDetList(Parser, Base, Base->ArraySize);
                }
                break;
            case BaseType::TypePointer: //Base = Parser->pc->VoidPtrType; break;
                Base = TypeGetMatching(Parser->pc, Parser,
                                TypeGetNonDeterministic(Parser, Typ->FromType),
                                Typ->Base, Typ->ArraySize, Typ->Identifier, TRUE, &nondet); break;

            case BaseType::TypeFunctionPtr: Base = &Parser->pc->FunctionPtrType; break;
            // function ptrs aren't supported to be ND - not even in SV-COMP
            default:
                fprintf(stderr, "Unsupported deterministic type conversion from type %s.\n", getType(Typ));
                Base = Typ;
        }
        return Base;
    }
}

/* add a new type to the set of types we know about */
struct ValueType *TypeAdd(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int Sizeof, int AlignBytes)
{
    auto *NewType = static_cast<ValueType *>(VariableAlloc(pc, Parser, MEM_ALIGN(sizeof(struct ValueType)), TRUE));
    NewType->Base = Base;
    NewType->ArraySize = ArraySize;
    NewType->Sizeof = Sizeof;
    NewType->AlignBytes = AlignBytes;
    NewType->Identifier = Identifier;
    NewType->Members = nullptr;
    NewType->FromType = ParentType;
    NewType->DerivedTypeList = nullptr;
    NewType->OnHeap = TRUE;
    NewType->IsNonDet = false;
    NewType->NDList = nullptr;
    NewType->NDListSize = 0;
    NewType->Next = ParentType->DerivedTypeList;
    ParentType->DerivedTypeList = NewType;
    
    return NewType;
}

/* given a parent type, get a matching derived type and make one if necessary.
 * Identifier should be registered with the shared string table. */
ValueType *TypeGetMatching(Picoc *pc, ParseState *Parser, ValueType *ParentType, BaseType Base, int ArraySize,
                           const char *Identifier, int AllowDuplicates, bool* Nondet)
{
    int Sizeof;
    int AlignBytes;
    struct ValueType *ThisType = ParentType->DerivedTypeList;
    while (ThisType != nullptr && (ThisType->Base != Base || ThisType->ArraySize != ArraySize ||
                        ThisType->Identifier != Identifier || (Nondet != nullptr && ThisType->IsNonDet != *Nondet)))
        ThisType = ThisType->Next;
    
    if (ThisType != nullptr)
    {
        if (AllowDuplicates)
            return ThisType;
        else
            ProgramFail(Parser, "data type '%s' is already defined", Identifier);
    }
        
    switch (Base)
    {
        case BaseType::TypePointer:   Sizeof = sizeof(void *); AlignBytes = PointerAlignBytes; break;
        case BaseType::TypeArray:     Sizeof = ArraySize * ParentType->Sizeof; AlignBytes = ParentType->AlignBytes; break;
        case BaseType::TypeEnum:      Sizeof = sizeof(int); AlignBytes = IntAlignBytes; break;
        default:            Sizeof = 0; AlignBytes = 0; break;      /* structs and unions will get bigger when we add members to them */
    }

    return TypeAdd(pc, Parser, ParentType, Base, ArraySize, Identifier, Sizeof, AlignBytes);
}

/* stack space used by a value */
int TypeStackSizeValue(Value *Val)
{
    if (Val != nullptr && Val->ValOnStack)
        return TypeSizeValue(Val, FALSE);
    else
        return 0;
}

/* memory used by a value */
int TypeSizeValue(Value *Val, int Compact) {
    if (IS_INTEGER_NUMERIC(Val) && !Compact) {
        return sizeof(ALIGN_TYPE);     /* allow some extra room for type extension */
    } else if (Val->Typ->Base == BaseType::Type_Type) {
        return sizeof(void*);
    } else if (Val->Typ->Base != BaseType::TypeArray) {
        return Val->Typ->Sizeof;
    } else {
        return Val->Typ->FromType->Sizeof * Val->Typ->ArraySize;
    }
}

/* memory used by a variable given its type and array size */
int TypeSize(struct ValueType *Typ, int ArraySize, int Compact)
{
    if (IS_INTEGER_NUMERIC_TYPE(Typ) && !Compact)
        return sizeof(ALIGN_TYPE);     /* allow some extra room for type extension */
    else if (Typ->Base != BaseType::TypeArray)
        return Typ->Sizeof;
    else
        return Typ->FromType->Sizeof * ArraySize;
}

/* add a base type */
void TypeAddBaseType(Picoc *pc, struct ValueType *TypeNode, enum BaseType Base, int Sizeof, int AlignBytes, bool IsNonDet)
{
    TypeNode->Base = Base;
    TypeNode->ArraySize = 0;
    TypeNode->Sizeof = Sizeof;
    TypeNode->AlignBytes = AlignBytes;
    TypeNode->Identifier = pc->StrEmpty;
    TypeNode->Members = nullptr;
    TypeNode->FromType = nullptr;
    TypeNode->DerivedTypeList = nullptr;
    TypeNode->OnHeap = FALSE;
    TypeNode->IsNonDet = IsNonDet;
    TypeNode->NDList = nullptr;
    TypeNode->NDListSize = 0;
    TypeNode->Next = pc->UberType.DerivedTypeList;
    pc->UberType.DerivedTypeList = TypeNode;
}

/* initialise the type system */
void TypeInit(Picoc *pc)
{
    struct IntAlign { char x; int y; } ia{};
    struct ShortAlign { char x; short y; } sa{};
    struct CharAlign { char x; char y; } ca{};
    struct LongAlign { char x; long y; } la{};
    struct LongLongAlign { char x; long long y; } lla{};
    struct DoubleAlign { char x; double y; } da{};
    struct FloatAlign { char x; float y; } fa{};
    struct PointerAlign { char x; void *y; } pa{};

    IntAlignBytes = (char *)&ia.y - &ia.x;
    PointerAlignBytes = (char *)&pa.y - &pa.x;
    
    pc->UberType.DerivedTypeList = nullptr;
    TypeAddBaseType(pc, &pc->IntType, BaseType::TypeInt, sizeof(int), IntAlignBytes, false);
    TypeAddBaseType(pc, &pc->ShortType, BaseType::TypeShort, sizeof(short), (char *) &sa.y - &sa.x, false);
    TypeAddBaseType(pc, &pc->CharType, BaseType::TypeChar, sizeof(char), (char *) &ca.y - &ca.x, false);
    TypeAddBaseType(pc, &pc->LongType, BaseType::TypeLong, sizeof(long), (char *) &la.y - &la.x, false);
    TypeAddBaseType(pc, &pc->LongLongType, BaseType::TypeLongLong, sizeof(long long), (char * ) &lla.y - &lla.x, false);
    TypeAddBaseType(pc, &pc->UnsignedIntType, BaseType::TypeUnsignedInt, sizeof(unsigned int), IntAlignBytes, false);
    TypeAddBaseType(pc, &pc->UnsignedShortType, BaseType::TypeUnsignedShort, sizeof(unsigned short), (char *) &sa.y - &sa.x, false);
    TypeAddBaseType(pc, &pc->UnsignedLongType, BaseType::TypeUnsignedLong, sizeof(unsigned long), (char *) &la.y - &la.x, false);
    TypeAddBaseType(pc, &pc->UnsignedLongLongType, BaseType::TypeUnsignedLongLong, sizeof(unsigned long long), (char * ) &lla.y - &lla.x, false);
    TypeAddBaseType(pc, &pc->UnsignedCharType, BaseType::TypeUnsignedChar, sizeof(unsigned char), (char *) &ca.y - &ca.x, false);
    TypeAddBaseType(pc, &pc->VoidType, BaseType::TypeVoid, 0, 1, false);
    TypeAddBaseType(pc, &pc->FunctionType, BaseType::TypeFunction, sizeof(int), IntAlignBytes, false);
    TypeAddBaseType(pc, &pc->MacroType, BaseType::TypeMacro, sizeof(int), IntAlignBytes, false);
    TypeAddBaseType(pc, &pc->GotoLabelType, BaseType::TypeGotoLabel, 0, 1, false);
    TypeAddBaseType(pc, &pc->FunctionPtrType, BaseType::TypeFunctionPtr, sizeof(char *), PointerAlignBytes, false);
    TypeAddBaseType(pc, &pc->TypeType, BaseType::Type_Type, sizeof(double), (char *) &da.y - &da.x, false);  /* must be large enough to cast to a double */
    TypeAddBaseType(pc, &pc->StructType, BaseType::TypeStruct, sizeof(int), IntAlignBytes, false);

    // NDs
    TypeAddBaseType(pc, &pc->IntNDType, BaseType::TypeInt, sizeof(int), IntAlignBytes, true);
    TypeAddBaseType(pc, &pc->ShortNDType, BaseType::TypeShort, sizeof(short), (char *) &sa.y - &sa.x, true);
    TypeAddBaseType(pc, &pc->CharNDType, BaseType::TypeChar, sizeof(char), (char *) &ca.y - &ca.x, true);
    TypeAddBaseType(pc, &pc->LongNDType, BaseType::TypeLong, sizeof(long), (char *) &la.y - &la.x, true);
    TypeAddBaseType(pc, &pc->LongLongNDType, BaseType::TypeLongLong, sizeof(long long), (char *) &lla.y - &lla.x, true);
    TypeAddBaseType(pc, &pc->UnsignedIntNDType, BaseType::TypeUnsignedInt, sizeof(unsigned int), IntAlignBytes, true);
    TypeAddBaseType(pc, &pc->UnsignedShortNDType, BaseType::TypeUnsignedShort, sizeof(unsigned short), (char *) &sa.y - &sa.x, true);
    TypeAddBaseType(pc, &pc->UnsignedCharNDType, BaseType::TypeUnsignedChar, sizeof(unsigned char), (char *) &ca.y - &ca.x, true);
    TypeAddBaseType(pc, &pc->UnsignedLongNDType, BaseType::TypeUnsignedLong, sizeof(unsigned long), (char *) &la.y - &la.x, true);
    TypeAddBaseType(pc, &pc->UnsignedLongLongNDType, BaseType::TypeUnsignedLongLong, sizeof(unsigned long long), (char *) &lla.y - &lla.x, true);

    TypeAddBaseType(pc, &pc->DoubleType, BaseType::TypeDouble, sizeof(double), (char *) &da.y - &da.x, false);
    TypeAddBaseType(pc, &pc->FloatType, BaseType::TypeFloat, sizeof(float), (char *) &fa.y - &fa.x, false);
    // NDs
    TypeAddBaseType(pc, &pc->DoubleNDType, BaseType::TypeDouble, sizeof(double), (char *) &da.y - &da.x, true);
    TypeAddBaseType(pc, &pc->FloatNDType, BaseType::TypeFloat, sizeof(float), (char *) &fa.y - &fa.x, true);
    pc->CharArrayType = TypeAdd(pc, nullptr, &pc->CharType, BaseType::TypeArray, 0, pc->StrEmpty, sizeof(char), (char *)&ca.y - &ca.x);
    pc->CharPtrType = TypeAdd(pc, nullptr, &pc->CharType, BaseType::TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
    pc->CharPtrPtrType = TypeAdd(pc, nullptr, pc->CharPtrType, BaseType::TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);

    pc->StructPtrType = TypeAdd(pc, nullptr, &pc->StructType, BaseType::TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);

    pc->VoidPtrType = TypeAdd(pc, nullptr, &pc->VoidType, BaseType::TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
}

/* deallocate heap-allocated types */
void TypeCleanupNode(Picoc *pc, struct ValueType *Typ) {
    struct ValueType *SubType;
    struct ValueType *NextSubType;

    if (Typ != nullptr)
        SubType = Typ->DerivedTypeList;
    else
        SubType = nullptr;

    if (Typ != nullptr) {
        freeNonDetList(pc, Typ->NDList);
    }

    // special case substruct element
    if (SubType != nullptr && (SubType->Next != nullptr && SubType->Next->Base == BaseType::TypeStruct)) {

        //SubType = SubType->Next;
        for (SubType = SubType->Next; SubType != nullptr; SubType = NextSubType) {
            NextSubType = SubType->Next;
            TypeCleanupNode(pc, SubType);
            if (SubType->OnHeap) {
                /* if it's a struct or union deallocate all the member values */
                if (SubType->Members != nullptr) {
                    VariableTableCleanup(pc, SubType->Members);
                    HeapFreeMem(pc, SubType->Members);
                    delete SubType->MemberOrder;
                }

                /* free this node */
                HeapFreeMem(pc, SubType);
            }
        }

        // clean highest SubType; lower members are already freed
        SubType = Typ->DerivedTypeList;

        TypeCleanupNode(pc, SubType);
        if (SubType->OnHeap) {
            /* free all member references */
            if (SubType->Members != nullptr) {
                HeapFreeMem(pc, SubType->Members);
                delete SubType->MemberOrder;
            }

            /* free this node */
            HeapFreeMem(pc, SubType);
        }
    } else {
        for (SubType = Typ->DerivedTypeList; SubType != nullptr; SubType = NextSubType) {
            NextSubType = SubType->Next;
            TypeCleanupNode(pc, SubType);
            if (SubType->OnHeap) {
                /* if it's a struct or union deallocate all the member values */
                if (SubType->Members != nullptr) {
                    VariableTableCleanup(pc, SubType->Members);
                    HeapFreeMem(pc, SubType->Members);
                    delete SubType->MemberOrder;
                }

                /* free this node */
                HeapFreeMem(pc, SubType);
            }
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
    Value *LexValue;
    struct ValueType *MemberType;
    char *MemberIdentifier;
    char *StructIdentifier;
    Value *MemberValue;
    enum LexToken Token;
    int AlignBoundary;
    bool ZeroTypeStruct = FALSE;
    Picoc *pc = Parser->pc;

    Token = nitwit::lex::LexGetToken(Parser, &LexValue, false); // get name of struct
    if (Token == TokenIdentifier)
    {
        nitwit::lex::LexGetToken(Parser, &LexValue, true);
        StructIdentifier = LexValue->Val->Identifier;
        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    }
    else
    {
        static char TempNameBuf[7] = "^s0000";
        StructIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }
    // create or fetch struct/union Type in PicoC type system
    *Typ = TypeGetMatching(pc, Parser, &Parser->pc->UberType, IsStruct ? BaseType::TypeStruct : BaseType::TypeUnion, 0, StructIdentifier,
                           TRUE, nullptr);

    if (Token == TokenLeftBrace) {
        ParseState OldParser = *Parser;
        nitwit::lex::LexGetToken(&(OldParser), nullptr, true);
        LexToken ZeroToken = nitwit::lex::LexGetToken(&(OldParser), nullptr, false);
        if (ZeroToken == TokenRightBrace) {
            ZeroTypeStruct = TRUE;
        } else {
            ZeroTypeStruct = FALSE;
        }
    }

    if (Token == TokenLeftBrace && (*Typ)->Members != nullptr){ // consume the definition if struct already defined
        fprintf(stderr, "Warning: data type '%s' is already defined. Will skip this.", StructIdentifier);
        while (nitwit::lex::LexGetToken(Parser, nullptr, false) != TokenRightBrace) {
            nitwit::lex::LexGetToken(Parser, nullptr, true);
        }
        nitwit::lex::LexGetToken(Parser, nullptr, true);
    }

    Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    if (Token != TokenLeftBrace)
    { 
        /* use the already defined structure */
#if 0
        if ((*Typ)->Members == nullptr)
            ProgramFail(Parser, "structure '%s' isn't defined", LexValue->Val->Identifier);
#endif            
        return;
    }
    
    if (pc->TopStackFrame != nullptr)
        ProgramFail(Parser, "struct/union definitions can only be globals");
    // allocate structs member table
    nitwit::lex::LexGetToken(Parser, nullptr, true);
    (*Typ)->Members = static_cast<Table *>(VariableAlloc(pc, Parser, sizeof(struct Table) +
                                                                     STRUCT_TABLE_SIZE * sizeof(struct TableEntry),
                                                         TRUE));
    (*Typ)->Members->HashTable = (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table));
    nitwit::table::TableInitTable((*Typ)->Members, (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, true);

    // do the parsing of members if not zero element struct
    if(!NO_ZERO_STRUCT && !ZeroTypeStruct) {
        int IsConst;
        char ParseOnlyIdent = FALSE;
        //    char IsBitField = FALSE;
        ValueType *BasicType = nullptr;
        do {
            if (ParseOnlyIdent) { // continue with the same type
                TypeParseIdentPart(Parser, BasicType, &MemberType, &MemberIdentifier, &IsConst);
                ParseOnlyIdent = FALSE;
            } else {
                BasicType = TypeParse(Parser, &MemberType, &MemberIdentifier, nullptr, &IsConst, 0);
            }

            if (MemberType == nullptr || MemberIdentifier == nullptr)
                ProgramFail(Parser, "invalid type in struct");

            MemberValue = VariableAllocValueAndData(pc, Parser, sizeof(int), FALSE, nullptr, TRUE, nullptr);
            MemberValue->Typ = MemberType;
            LexToken NextToken = nitwit::lex::LexGetToken(Parser, nullptr, true);
            if (NextToken == TokenColon) { // it is a bit field!
                if (!IS_INTEGER_NUMERIC(MemberValue)) {
                    ProgramFail(Parser, "only integral types allowed in bit fields");
                }
                Value *bitlen = nullptr; // get bit field length from constant
                nitwit::lex::LexGetToken(Parser, &bitlen, true); // number
                if (IS_INTEGER_NUMERIC_TYPE(bitlen->Typ)) {
                    long long length = CoerceT<long long>(bitlen);
                    if (length < 0 || 8 * MemberValue->Typ->Sizeof < length) {
                        ProgramFail(Parser, "wrong size n: n > 0 and n <= sizeof");
                    }
                    MemberValue->BitField = length;
                    if (IS_UNSIGNED(MemberValue)) {
                        MemberValue->Typ = &Parser->pc->UnsignedLongLongType;
                    } else {
                        MemberValue->Typ = &Parser->pc->LongLongType;
                    }
                } else {
                    ProgramFail(Parser, "positive integer expected");
                }
                NextToken = nitwit::lex::LexGetToken(Parser, nullptr, true); // semicolon
            }
            if (IsStruct) {
                /* allocate this member's location in the struct */
                AlignBoundary = MemberValue->Typ->AlignBytes;
                if (((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
                    (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));

                MemberValue->Val->Integer = (*Typ)->Sizeof;
                (*Typ)->Sizeof += TypeSizeValue(MemberValue, TRUE);
            } else {
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
            if (!nitwit::table::TableSet(pc, (*Typ)->Members, MemberIdentifier, MemberValue, Parser->FileName, Parser->Line,
                          Parser->CharacterPos))
                ProgramFail(Parser, "member '%s' already defined", &MemberIdentifier);
            // add member id to list (is backwards, needs reverse)
            (*Typ)->MemberOrder = new ValueList((*Typ)->MemberOrder, MemberIdentifier);


            if (NextToken == TokenComma) {
                ParseOnlyIdent = TRUE;
            } else if (NextToken != TokenSemicolon) {
                ProgramFail(Parser, "semicolon expected");
            }


        } while (nitwit::lex::LexGetToken(Parser, nullptr, false) != TokenRightBrace);

        /* now align the structure to the size of its largest member's alignment */
        AlignBoundary = (*Typ)->AlignBytes;
        if (((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
            (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));

        // reverse member order
        ValueList *elem = (*Typ)->MemberOrder, *prev = nullptr, *tmp;
        while (elem) {
            tmp = elem->Next;
            elem->Next = prev;
            prev = elem;
            elem = tmp;
        }
        (*Typ)->MemberOrder = prev;
    }
    nitwit::lex::LexGetToken(Parser, nullptr, true);
}

/* create a system struct which has no user-visible members */
struct ValueType *TypeCreateOpaqueStruct(Picoc *pc, struct ParseState *Parser, const char *StructName, int Size)
{
    struct ValueType *Typ = TypeGetMatching(pc, Parser, &pc->UberType, BaseType::TypeStruct, 0, StructName, FALSE, nullptr);
    
    /* create the (empty) table */
    Typ->Members = static_cast<Table *>(VariableAlloc(pc, Parser, sizeof(struct Table) +
                                                                  STRUCT_TABLE_SIZE * sizeof(struct TableEntry), TRUE));
    Typ->Members->HashTable = (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table));
    nitwit::table::TableInitTable(Typ->Members, (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, true);
    Typ->Sizeof = Size;
    
    return Typ;
}

/* parse an enum declaration */
void TypeParseEnum(struct ParseState *Parser, struct ValueType **Typ)
{
    Value *LexValue;
    Value InitValue{};
    LexToken Token;
    int EnumValue = 0;
    char *EnumIdentifier;
    Picoc *pc = Parser->pc;
    
    Token = nitwit::lex::LexGetToken(Parser, &LexValue, false);
    if (Token == TokenIdentifier)
    {
        nitwit::lex::LexGetToken(Parser, &LexValue, true);
        EnumIdentifier = LexValue->Val->Identifier;
        Token = nitwit::lex::LexGetToken(Parser, nullptr, false);
    }
    else
    {
        static char TempNameBuf[7] = "^e0000";
        EnumIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }

    TypeGetMatching(pc, Parser, &pc->UberType, BaseType::TypeEnum, 0, EnumIdentifier, Token != TokenLeftBrace, nullptr);
    *Typ = &pc->IntType;
    if (Token != TokenLeftBrace)
    { 
        /* use the already defined enum */
        if ((*Typ)->Members == nullptr)
            ProgramFail(Parser, "enum '%s' isn't defined", EnumIdentifier);
            
        return;
    }
    
    if (pc->TopStackFrame != nullptr)
        ProgramFail(Parser, "enum definitions can only be globals");
        
    nitwit::lex::LexGetToken(Parser, nullptr, true);
    (*Typ)->Members = &pc->GlobalTable;
    memset((void *)&InitValue, '\0', sizeof(Value));
    InitValue.Typ = &pc->IntType;
    InitValue.Val = (union AnyValue *)&EnumValue;
    do {
        if (nitwit::lex::LexGetToken(Parser, &LexValue, true) != TokenIdentifier)
            ProgramFail(Parser, "identifier expected");
        
        EnumIdentifier = LexValue->Val->Identifier;
        if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenAssign)
        {
            nitwit::lex::LexGetToken(Parser, nullptr, true);
            EnumValue = nitwit::expressions::ExpressionParseLongLong(Parser);
        }

        VariableDefine(pc, Parser, EnumIdentifier, &InitValue, nullptr, FALSE, false);
            
        Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
        if (Token != TokenComma && Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
        
        EnumValue++;
                    
    } while (Token == TokenComma);
}

/* parse a type - just the basic type */
int TypeParseFront(struct ParseState *Parser, struct ValueType **Typ, int *IsStatic, int *IsConst)
{
    ParseState Before{};
    Value *LexerValue;
    LexToken Token;
    int Unsigned = FALSE;
    Value *VarValue;
    int StaticQualifier = FALSE;
    int ConstQualifier = FALSE;
    int LongQualifier = FALSE;
    int LongLongQualifier = FALSE;
    Picoc *pc = Parser->pc;
    *Typ = nullptr;

    /* ignore leading type qualifiers */
    nitwit::parse::ParserCopy(&Before, Parser);
    Token = nitwit::lex::LexGetToken(Parser, &LexerValue, true);
    while (Token == TokenStaticType || Token == TokenAutoType || Token == TokenRegisterType || Token == TokenExternType
            || Token == TokenConst)
    {
        if (Token == TokenStaticType)
            StaticQualifier = TRUE;
        if (Token == TokenConst)
            ConstQualifier = TRUE;
        Token = nitwit::lex::LexGetToken(Parser, &LexerValue, true);
    }
    
    if (IsStatic != nullptr)
        *IsStatic = StaticQualifier;

    if (Token == TokenLongType){
        LexToken FollowToken = nitwit::lex::LexGetToken(Parser, nullptr, false);
        if (FollowToken != TokenIdentifier
                && (FollowToken == TokenSignedType || FollowToken == TokenUnsignedType ||
                    FollowToken == TokenIntType || FollowToken == TokenLongType)){
            LongQualifier = TRUE;
            Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
            FollowToken = nitwit::lex::LexGetToken(Parser, nullptr, false);
            if (FollowToken != TokenIdentifier
                && (FollowToken == TokenSignedType || FollowToken == TokenUnsignedType ||
                    FollowToken == TokenIntType || FollowToken == TokenLongType)){
                if (Token == TokenLongType){
                    LongLongQualifier = TRUE;
                    Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
                }
            }
        }
    }
        
    /* handle signed/unsigned with no trailing type */
    if (Token == TokenSignedType || Token == TokenUnsignedType)
    {
        LexToken FollowToken = nitwit::lex::LexGetToken(Parser, &LexerValue, false);
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
        
        Token = nitwit::lex::LexGetToken(Parser, &LexerValue, true);
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
            if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenIntType)
                nitwit::lex::LexGetToken(Parser, nullptr, true);
            break;
        case TokenCharType: 
            *Typ = Unsigned ? &pc->UnsignedCharType : &pc->CharType;
            break;
        case TokenLongType:
            if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenLongType){
                nitwit::lex::LexGetToken(Parser, nullptr, true);
                *Typ = Unsigned ? &pc->UnsignedLongLongType : &pc->LongLongType;
            } else if (LongQualifier == TRUE){
                *Typ = Unsigned ? &pc->UnsignedLongLongType : &pc->LongLongType;
            } else {
                *Typ = Unsigned ? &pc->UnsignedLongType : &pc->LongType;
            }
            if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenIntType)
                nitwit::lex::LexGetToken(Parser, nullptr, true);
            break;
        case TokenFloatType: *Typ = &pc->FloatType; break;
        case TokenDoubleType: *Typ = &pc->DoubleType; break;
        case TokenVoidType: *Typ = &pc->VoidType; break;

        case TokenStructType: case TokenUnionType:
            if (*Typ != nullptr)
                ProgramFail(Parser, "bad type declaration");

            TypeParseStruct(Parser, Typ, Token == TokenStructType);
            break;

        case TokenEnumType:
            if (*Typ != nullptr)
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
        default: 
            nitwit::parse::ParserCopy(Parser, &Before);
            return FALSE;
    }

    if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenConst) {
        nitwit::lex::LexGetToken(Parser, nullptr, true);
        if (ConstQualifier == TRUE)
            ProgramFail(Parser, "const already specified, consts are still experimental, so this might not be a real error");

        ConstQualifier = TRUE;
    }

    if (IsConst != nullptr)
        *IsConst = ConstQualifier;
    return TRUE;
}

/* parse a type - the part at the end after the identifier. eg. array specifications etc. */
struct ValueType *TypeParseBack(struct ParseState *Parser, struct ValueType *FromType)
{
    LexToken Token;
    ParseState Before{};

    nitwit::parse::ParserCopy(&Before, Parser);
    Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
    if (Token == TokenLeftSquareBracket)
    {
        /* add another array bound */
        if (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenRightSquareBracket)
        {
            /* an unsized array */
            nitwit::lex::LexGetToken(Parser, nullptr, true);
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), BaseType::TypeArray, 0,
                                   Parser->pc->StrEmpty, TRUE, nullptr);
        }
        else
        {
            /* get a numeric array size, dont resolve size when not running */
//            enum RunMode OldMode = Parser->Mode;
            int ArraySize;
//            Parser->Mode = RunModeRun;
            ArraySize = nitwit::expressions::ExpressionParseLongLong(Parser);
//            Parser->Mode = OldMode;
            
            if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenRightSquareBracket)
                ProgramFail(Parser, "']' expected");
            
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), BaseType::TypeArray, ArraySize,
                                   Parser->pc->StrEmpty, TRUE, nullptr);
        }
    }
    else
    {
        /* the type specification has finished */
        nitwit::parse::ParserCopy(Parser, &Before);
        return FromType;
    }
}

int TypeParseFunctionPointer(ParseState *Parser, ValueType *BasicType, ValueType **Type, char **Identifier, bool IsArgument) {
    Value *LexValue;
    ParseState Before{};
    *Identifier = Parser->pc->StrEmpty;
    *Type = BasicType;
    nitwit::parse::ParserCopy(&Before, Parser);
    bool BracketsAsterisk = true;

    while (nitwit::lex::LexGetToken(Parser, nullptr, false) == TokenAsterisk){
        nitwit::lex::LexGetToken(Parser, nullptr, true);
        if (*Type == nullptr)
            ProgramFail(Parser, "bad type declaration");
        *Type = TypeGetMatching(Parser->pc, Parser, *Type, BaseType::TypePointer, 0, Parser->pc->StrEmpty, TRUE, nullptr);
    }
    LexToken Token;
    Token = nitwit::lex::LexGetToken(Parser, &LexValue, true);
    if (Token == TokenOpenBracket){
        Token = nitwit::lex::LexGetToken(Parser, nullptr, true);
        if (Token != TokenAsterisk) goto ERROR;
        Token = nitwit::lex::LexGetToken(Parser, &LexValue, true);

        *Type = &Parser->pc->FunctionPtrType;
        while (Token == TokenAsterisk){
            *Type = TypeGetMatching(Parser->pc, Parser, *Type, BaseType::TypePointer, 0, Parser->pc->StrEmpty, TRUE, nullptr);
            Token = nitwit::lex::LexGetToken(Parser, &LexValue, true);
        }
    } else if (IsArgument) {
        BracketsAsterisk = false;
    } else goto ERROR;

    if (Token == TokenCloseBracket && !IsArgument)
        // a cast
        *Identifier = Parser->pc->StrEmpty;
    else if (Token == TokenIdentifier) {
        *Identifier = LexValue->Val->Identifier;
        *Type = TypeParseBack(Parser, *Type);
        Token = nitwit::lex::LexGetToken(Parser, nullptr, BracketsAsterisk);
    } else goto ERROR;

    if (BracketsAsterisk) {
        if (Token != TokenCloseBracket)
            ProgramFail(Parser, ") expected after function pointer identifier");
    } else {
        if (Token != TokenOpenBracket)
            goto ERROR;
        else
            *Type = &Parser->pc->FunctionPtrType;
    }

    return TRUE;

    ERROR:
        nitwit::parse::ParserCopy(Parser, &Before);
        *Type = BasicType;
        return FALSE;
}

/* parse a type - the part which is repeated with each identifier in a declaration list */
void
TypeParseIdentPart(struct ParseState *Parser, struct ValueType *BasicTyp, struct ValueType **Typ, char **Identifier,
                   int *IsConst)
{
    ParseState Before{};
    LexToken Token;
    Value *LexValue;
    int Done = FALSE;
    *Typ = BasicTyp;
    *Identifier = Parser->pc->StrEmpty;
    
    while (!Done)
    {
        nitwit::parse::ParserCopy(&Before, Parser);
        Token = nitwit::lex::LexGetToken(Parser, &LexValue, true);
        switch (Token)
        {
            case TokenOpenBracket:
                if (*Typ != nullptr)
                    ProgramFail(Parser, "bad type declaration");

                TypeParse(Parser, Typ, Identifier, nullptr, IsConst, 0);
                if (nitwit::lex::LexGetToken(Parser, nullptr, true) != TokenCloseBracket)
                    ProgramFail(Parser, "')' expected");
                break;
            case TokenAsterisk:
                if (*Typ == nullptr)
                    ProgramFail(Parser, "bad type declaration");

                *Typ = TypeGetMatching(Parser->pc, Parser, *Typ, BaseType::TypePointer, 0, Parser->pc->StrEmpty, TRUE, nullptr);
                break;
            
            case TokenIdentifier:
                if (*Typ == nullptr || *Identifier != Parser->pc->StrEmpty)
                    ProgramFail(Parser, "bad type declaration");
                
                *Identifier = LexValue->Val->Identifier;
                Done = TRUE;
                break;
                
            default: nitwit::parse::ParserCopy(Parser, &Before); Done = TRUE; break;
        }
    }
    
    if (*Typ == nullptr)
        ProgramFail(Parser, "bad type declaration");

    if (*Identifier != Parser->pc->StrEmpty)
    { 
        /* parse stuff after the identifier */
        *Typ = TypeParseBack(Parser, *Typ);
    }
}

/* parse a type - a complete declaration including identifier */
ValueType *TypeParse(struct ParseState *Parser, struct ValueType **Typ, char **Identifier, int *IsStatic, int *IsConst,
                     bool IsArgument)
{
    struct ValueType *BasicType;

    TypeParseFront(Parser, &BasicType, IsStatic, IsConst);

    if (!TypeParseFunctionPointer(Parser, BasicType, Typ, Identifier, IsArgument)) {
        TypeParseIdentPart(Parser, BasicType, Typ, Identifier, IsConst);
    } else {
        Value * FuncValue = nitwit::parse::ParseFunctionDefinition(Parser, BasicType, *Identifier, true);
        if (FuncValue != nullptr) VariableFree(Parser->pc, FuncValue);
    }
    return BasicType;
}

/* check if a type has been fully defined - otherwise it's just a forward declaration */
int TypeIsForwardDeclared(struct ParseState *Parser, struct ValueType *Typ)
{
    if (Typ->Base == BaseType::TypeArray)
        return TypeIsForwardDeclared(Parser, Typ->FromType);
    
    if ( (Typ->Base == BaseType::TypeStruct || Typ->Base == BaseType::TypeUnion) && Typ->Members == nullptr)
        return TRUE;
        
    return FALSE;
}
