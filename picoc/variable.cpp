/* picoc variable storage. This provides ways of defining and accessing
 * variables */
 
#include "interpreter.hpp"

/* maximum size of a value to temporarily copy while we create a variable */
#define MAX_TMP_COPY_BUF 1024


/* initialise the variable system */
void VariableInit(Picoc *pc)
{
    TableInitTable(&(pc->GlobalTable), &(pc->GlobalHashTable)[0], GLOBAL_TABLE_SIZE, TRUE);
    TableInitTable(&pc->StringLiteralTable, &pc->StringLiteralHashTable[0], STRING_LITERAL_TABLE_SIZE, TRUE);
    pc->TopStackFrame = nullptr;
}

/* deallocate the contents of a variable */
void VariableFree(Picoc *pc, Value *Val)
{
    if (Val->ValOnHeap || Val->AnyValOnHeap)
    {
        /* free function bodies, don't free function ptrs bodies  */
        if (Val->Typ == &pc->FunctionType && Val->Val->FuncDef.Intrinsic == nullptr && Val->Val->FuncDef.Body.Pos != nullptr)
            HeapFreeMem(pc, (void *)Val->Val->FuncDef.Body.Pos);

        /* free macro bodies */
        if (Val->Typ == &pc->MacroType)
            HeapFreeMem(pc, (void *)Val->Val->MacroDef.Body.Pos);

        /* free the AnyValue */
        if (Val->AnyValOnHeap)
            HeapFreeMem(pc, Val->Val);

    }

    /* free the value */
    if (Val->ValOnHeap)
        HeapFreeMem(pc, Val);
}

/* deallocate the global table and the string literal table */
void VariableTableCleanup(Picoc *pc, struct Table *HashTable)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;

    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != nullptr; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            VariableFree(pc, Entry->p.v.Val);
//            delete Entry->p.v.ValShadows;

            /* free the hash table entry */
            HeapFreeMem(pc, Entry);
        }
    }
}

/* deallocate the global table and the string literal table */
void ShadowTableCleanup(Picoc *pc, struct Table *HashTable)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;

    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != nullptr; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            delete Entry->p.v.ValShadows;
        }
    }
}

void VariableCleanup(Picoc *pc)
{
    VariableTableCleanup(pc, &pc->GlobalTable);
    VariableTableCleanup(pc, &pc->StringLiteralTable);
}

/* allocate some memory, either on the heap or the stack and check if we've run out */
void *VariableAlloc(Picoc *pc, struct ParseState *Parser, int Size, int OnHeap)
{
    void *NewValue;
    
    if (OnHeap)
        NewValue = HeapAllocMem(pc, Size);
    else
        NewValue = HeapAllocStack(pc, Size);
    
    if (NewValue == nullptr)
        ProgramFail(Parser, "out of memory");
    
#ifdef DEBUG_HEAP
    if (!OnHeap)
        printf("pushing %d at 0x%lx\n", Size, (unsigned long)NewValue);
#endif
        
    return NewValue;
}

/* allocate a value either on the heap or the stack using space dependent on what type we want */
Value *
VariableAllocValueAndData(Picoc *pc, struct ParseState *Parser, int DataSize, int IsLValue, Value *LValueFrom,
                          int OnHeap, char *VarIdentifier)
{
    Value *NewValue = static_cast<Value *>(VariableAlloc(pc, Parser, MEM_ALIGN(sizeof(Value)) + DataSize,
                                                                OnHeap));
    NewValue->Val = (union AnyValue *)((char *)NewValue + MEM_ALIGN(sizeof(Value)));
    NewValue->ValOnHeap = OnHeap;
    NewValue->AnyValOnHeap = FALSE;
    NewValue->ValOnStack = !OnHeap;
    NewValue->IsLValue = IsLValue;
    NewValue->LValueFrom = LValueFrom;
    if (Parser) 
        NewValue->ScopeID = Parser->ScopeID;

    NewValue->OutOfScope = 0;
    NewValue->VarIdentifier = VarIdentifier;
    NewValue->ConstQualifier = FALSE;
    NewValue->ShadowedVal = nullptr;
    NewValue->BitField = 0;
    return NewValue;
}

/* allocate a value given its type */
Value *VariableAllocValueFromType(Picoc *pc, struct ParseState *Parser, struct ValueType *Typ, int IsLValue, Value *LValueFrom, int OnHeap)
{
    int Size = TypeSize(Typ, Typ->ArraySize, FALSE);
    Value *NewValue = VariableAllocValueAndData(pc, Parser, Size, IsLValue, LValueFrom, OnHeap, nullptr);
    assert(Size >= 0 || Typ == &pc->VoidType);
    NewValue->Typ = Typ;
    
    return NewValue;
}

/* allocate a value either on the heap or the stack and copy its value. handles overlapping data */
Value *VariableAllocValueAndCopy(Picoc *pc, struct ParseState *Parser, Value *FromValue, int OnHeap)
{
    struct ValueType *DType = FromValue->Typ;
    Value *NewValue;
    char TmpBuf[MAX_TMP_COPY_BUF];
    int CopySize = TypeSizeValue(FromValue, TRUE);

    if (CopySize > MAX_TMP_COPY_BUF) {
        ProgramFailWithExitCode(Parser, 251, "Out of memory");
    }
    memcpy((void *)&TmpBuf[0], (void *)FromValue->Val, CopySize);
    NewValue = VariableAllocValueAndData(pc, Parser, CopySize, FromValue->IsLValue, FromValue->LValueFrom, OnHeap,
                                         FromValue->VarIdentifier);
    NewValue->Typ = DType;
    memcpy((void *)NewValue->Val, (void *)&TmpBuf[0], CopySize);
    
    return NewValue;
}

/* allocate a value either on the heap or the stack from an existing AnyValue and type */
Value *
VariableAllocValueFromExistingData(struct ParseState *Parser, struct ValueType *Typ, union AnyValue *FromValue,
                                   int IsLValue, Value *LValueFrom, char *VarIdentifier)
{
    Value *NewValue = static_cast<Value *>(VariableAlloc(Parser->pc, Parser, MEM_ALIGN(sizeof(Value)), FALSE));
    NewValue->Typ = Typ;
    NewValue->Val = FromValue;
    NewValue->ValOnHeap = FALSE;
    NewValue->AnyValOnHeap = FALSE;
    NewValue->ValOnStack = FALSE;
    NewValue->IsLValue = IsLValue;
    NewValue->LValueFrom = LValueFrom;
    NewValue->VarIdentifier = VarIdentifier;
    NewValue->ConstQualifier = LValueFrom == nullptr ? FALSE : LValueFrom->ConstQualifier;
    NewValue->ShadowedVal = LValueFrom == nullptr ? FALSE : LValueFrom->ShadowedVal;
    NewValue->BitField = LValueFrom == nullptr ? 0 : LValueFrom->BitField;

    return NewValue;
}

/* allocate a value either on the heap or the stack from an existing Value, sharing the value */
Value *VariableAllocValueShared(struct ParseState *Parser, Value *FromValue)
{
    return VariableAllocValueFromExistingData(Parser, FromValue->Typ, FromValue->Val, FromValue->IsLValue,
                                              FromValue->IsLValue ? FromValue : nullptr,
                                              FromValue->VarIdentifier);
}

/* reallocate a variable so its data has a new size */
void VariableRealloc(struct ParseState *Parser, Value *FromValue, int NewSize)
{
    if (FromValue->AnyValOnHeap)
        HeapFreeMem(Parser->pc, FromValue->Val);
        
    FromValue->Val = static_cast<AnyValue *>(VariableAlloc(Parser->pc, Parser, NewSize, TRUE));
    FromValue->AnyValOnHeap = TRUE;
}

int VariableScopeBegin(struct ParseState * Parser, int* OldScopeID)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    Picoc * pc = Parser->pc;
    int Count;
    #ifdef VAR_SCOPE_DEBUG
    int FirstPrint = 0;
    #endif
    
    struct Table * HashTable = (pc->TopStackFrame == nullptr) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;

    if (Parser->ScopeID == -1) return -1;

    /* XXX dumb hash, let's hope for no collisions... */
    *OldScopeID = Parser->ScopeID;
    Parser->ScopeID = (int)(intptr_t)(Parser->SourceText) * ((int)(intptr_t)(Parser->Pos) / sizeof(char*));
    /* or maybe a more human-readable hash for debugging? */
    /* Parser->ScopeID = Parser->Line * 0x10000 + Parser->CharacterPos; */
    
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != nullptr; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            if (Entry->p.v.Val->ScopeID != Parser->ScopeID && Entry->p.v.ValShadows != nullptr){ // todo is shadows always initiated?
                auto shadow = Entry->p.v.ValShadows->shadows.find(Parser->ScopeID);
                if (shadow != Entry->p.v.ValShadows->shadows.end()){
                    shadow->second->ShadowedVal = Entry->p.v.Val; // save which value was shadowed
                    Entry->p.v.Val = shadow->second; // take the shadow as the current value
                    Entry->p.v.Val->OutOfScope = FALSE;
                #ifdef VAR_SCOPE_DEBUG
                    printf(">>> shadow back into scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
                #endif
                }
            } else if (Entry->p.v.Val->ScopeID == Parser->ScopeID && Entry->p.v.Val->OutOfScope) {
                Entry->p.v.Val->OutOfScope = FALSE;
                Entry->p.v.Key = (char*)((intptr_t)Entry->p.v.Key & ~1);
                #ifdef VAR_SCOPE_DEBUG
                printf(">>> back into scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
                #endif
            }
        }
    }

    return Parser->ScopeID;
}

void VariableScopeEnd(struct ParseState * Parser, int ScopeID, int PrevScopeID)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    Picoc * pc = Parser->pc;
    int Count;
    #ifdef VAR_SCOPE_DEBUG
    int FirstPrint = 0;
    #endif

    struct Table * HashTable = (pc->TopStackFrame == nullptr) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;

    if (ScopeID == -1) return;

    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != nullptr; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            if (Entry->p.v.Val->ScopeID == ScopeID && !Entry->p.v.Val->OutOfScope)
            {
                #ifdef VAR_SCOPE_DEBUG
                printf(">>> out of scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
                #endif
                Entry->p.v.Val->OutOfScope = TRUE;
                if (Entry->p.v.Val->ShadowedVal == nullptr){
                    Entry->p.v.Key = (char*)((intptr_t)Entry->p.v.Key | 1); /* alter the key so it won't be found by normal searches */
                } else {
                    Value * shadow = Entry->p.v.Val;
                    Entry->p.v.Val = shadow->ShadowedVal;
                    shadow->ShadowedVal = nullptr;
#ifdef VAR_SCOPE_DEBUG
                    printf(">>> shadowed variable back into scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
#endif
                }
            }
        }
    }

    Parser->ScopeID = PrevScopeID;
}

int VariableDefinedAndOutOfScope(Picoc * pc, const char* Ident)
{
    struct TableEntry *Entry;
    int Count;

    struct Table * HashTable = (pc->TopStackFrame == nullptr) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != nullptr; Entry = Entry->Next)
        {
            if (Entry->p.v.Val->OutOfScope && (char*)((intptr_t)Entry->p.v.Key & ~1) == Ident)
                return TRUE;
        }
    }
    return FALSE;
}

/* define a variable. Ident must be registered */
Value *
VariableDefine(Picoc *pc, ParseState *Parser, char *Ident, Value *InitValue, ValueType *Typ, int MakeWritable, bool MakeShadow)
{
    Value * AssignValue;
    struct Table * currentTable = (pc->TopStackFrame == nullptr) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;
    
    int ScopeID = Parser ? Parser->ScopeID : -1;
#ifdef VAR_SCOPE_DEBUG
    if (Parser) fprintf(stderr, "def %s %x (%s:%d:%d)\n", Ident, ScopeID, Parser->FileName, Parser->Line, Parser->CharacterPos);
#endif
    
    if (InitValue != nullptr)
        AssignValue = VariableAllocValueAndCopy(pc, Parser, InitValue, pc->TopStackFrame == nullptr);
    else {
        AssignValue = VariableAllocValueFromType(pc, Parser, Typ, MakeWritable, nullptr, pc->TopStackFrame == nullptr);
        // todo non-det for uninit variables
    }
    
    AssignValue->IsLValue = MakeWritable;
    AssignValue->ScopeID = ScopeID;
    AssignValue->OutOfScope = FALSE;

    if (!TableSet(pc, currentTable, Ident, AssignValue, Parser ? ((char *)Parser->FileName) : nullptr, Parser ? Parser->Line : 0, Parser ? Parser->CharacterPos : 0)){
        unsigned AddAt; TableEntry * FoundEntry = TableSearch(currentTable, Ident, &AddAt);
        if (MakeShadow) {
            // shadowing
            if (pc->TopStackFrame == nullptr) {
                // most probably redefinition of an included global variable
                VariableFree(pc, FoundEntry->p.v.Val);
                FoundEntry->p.v.Val = AssignValue;
                return AssignValue;
            }
            if (!FoundEntry->p.v.ValShadows){ // save the current variable
                FoundEntry->p.v.ValShadows = new Shadows();
                FoundEntry->p.v.ValShadows->shadows.emplace(make_pair(FoundEntry->p.v.Val->ScopeID, FoundEntry->p.v.Val));
            }
            FoundEntry->p.v.ValShadows->shadows.emplace(make_pair(ScopeID, AssignValue));
            AssignValue->ShadowedVal = FoundEntry->p.v.Val;
            FoundEntry->p.v.Val = AssignValue;
    #ifdef VAR_SCOPE_DEBUG
            printf(">>> shadow the variable %s\n", Ident);
    #endif
        } else {
            ProgramFailWithExitCode(Parser, 246, "'%s' is already defined", Ident);
        }
    }

    return AssignValue;
}

/* define a variable. Ident must be registered. If it's a redefinition from the same declaration don't throw an error */
Value *VariableDefineButIgnoreIdentical(struct ParseState *Parser, char *Ident, struct ValueType *Typ, int IsStatic, int *FirstVisit)
{
    Picoc *pc = Parser->pc;
    Value *ExistingValue;
    const char *DeclFileName;
    unsigned DeclLine;
    unsigned DeclColumn;
    
    /* is the type a forward declaration? */
    if (TypeIsForwardDeclared(Parser, Typ))
        ProgramFailWithExitCode(Parser, 244,"type '%t' isn't defined", Typ);

    if (IsStatic)
    {
        char MangledName[LINEBUFFER_MAX];
        char *MNPos = &MangledName[0];
        char *MNEnd = &MangledName[LINEBUFFER_MAX-1];
        const char *RegisteredMangledName;
        
        /* make the mangled static name (avoiding using sprintf() to minimise library impact) */
        memset((void *)&MangledName, '\0', sizeof(MangledName));
        *MNPos++ = '/';
        strncpy(MNPos, (char *)Parser->FileName, MNEnd - MNPos);
        MNPos += strlen(MNPos);
        
        if (pc->TopStackFrame != nullptr)
        {
            /* we're inside a function */
            if (MNEnd - MNPos > 0) *MNPos++ = '/';
            strncpy(MNPos, (char *)pc->TopStackFrame->FuncName, MNEnd - MNPos);
            MNPos += strlen(MNPos);
        }
            
        if (MNEnd - MNPos > 0) *MNPos++ = '/';
        strncpy(MNPos, Ident, MNEnd - MNPos);
        RegisteredMangledName = TableStrRegister(pc, MangledName);
        
        /* is this static already defined? */
        if (!TableGet(&pc->GlobalTable, RegisteredMangledName, &ExistingValue, &DeclFileName, &DeclLine, &DeclColumn))
        {
            // todo : what about non-det for static values from type?
            /* define the mangled-named static variable store in the global scope */
            ExistingValue = VariableAllocValueFromType(Parser->pc, Parser, Typ, TRUE, nullptr, TRUE);
            TableSet(pc, &pc->GlobalTable, (char *)RegisteredMangledName, ExistingValue, (char *)Parser->FileName, Parser->Line, Parser->CharacterPos);
            *FirstVisit = TRUE;
        }

        /* static variable exists in the global scope - now make a mirroring variable in our own scope with the short name */
        VariableDefinePlatformVar(Parser->pc, Parser, Ident, ExistingValue->Typ, ExistingValue->Val, TRUE);
        return ExistingValue;
    }
    else
    {
        int DidExist = TableGet((pc->TopStackFrame == nullptr) ? &pc->GlobalTable : &pc->TopStackFrame->LocalTable,
                                Ident, &ExistingValue, &DeclFileName, &DeclLine, &DeclColumn);
        if (Parser->Line != 0 && DidExist && DeclFileName == Parser->FileName && DeclLine == Parser->Line && DeclColumn == Parser->CharacterPos) {
            return ExistingValue;
        } else {
            return VariableDefine(Parser->pc, Parser, Ident, nullptr, Typ, TRUE, (Parser->Line != 0 && DidExist));
        }
    }
}

/* check if a variable with a given name is defined. Ident must be registered */
int VariableDefined(Picoc *pc, const char *Ident)
{
    Value *FoundValue;
    
    if (pc->TopStackFrame == nullptr || !TableGet(&pc->TopStackFrame->LocalTable, Ident, &FoundValue, nullptr, nullptr, nullptr))
    {
        if (!TableGet(&pc->GlobalTable, Ident, &FoundValue, nullptr, nullptr, nullptr))
            return FALSE;
    }

    return TRUE;
}

/* get the value of a variable. must be defined. Ident must be registered */
void VariableGet(Picoc *pc, struct ParseState *Parser, const char *Ident, Value **LVal)
{
    if (pc->TopStackFrame == nullptr || !TableGet(&pc->TopStackFrame->LocalTable, Ident, LVal, nullptr, nullptr, nullptr))
    {
        if (!TableGet(&pc->GlobalTable, Ident, LVal, nullptr, nullptr, nullptr))
        {
            if (VariableDefinedAndOutOfScope(pc, Ident))
                ProgramFail(Parser, "'%s' is out of scope", Ident);
            else
                ProgramFailWithExitCode(Parser, 244,"'%s' is undefined", Ident);
        }
    }
}

/* define a global variable shared with a platform global. Ident will be registered */
void VariableDefinePlatformVar(Picoc *pc, struct ParseState *Parser, const char *Ident, struct ValueType *Typ, union AnyValue *FromValue, int IsWritable)
{
    Value *SomeValue = VariableAllocValueAndData(pc, nullptr, 0, IsWritable, nullptr, TRUE, nullptr);
    SomeValue->Typ = Typ;
    SomeValue->Val = FromValue;
    
    if (!TableSet(pc, (pc->TopStackFrame == nullptr) ? &pc->GlobalTable : &pc->TopStackFrame->LocalTable, TableStrRegister(pc, Ident), SomeValue, Parser ? Parser->FileName : nullptr, Parser ? Parser->Line : 0, Parser ? Parser->CharacterPos : 0))
        ProgramFailWithExitCode(Parser, 246, "'%s' is already defined", Ident);
}

/* free and/or pop the top value off the stack. Var must be the top value on the stack! */
void VariableStackPop(struct ParseState *Parser, Value *Var)
{
    int Success;
    
#ifdef DEBUG_HEAP
    if (Var->ValOnStack)
        printf("popping %ld at 0x%lx\n", (unsigned long)(sizeof(Value) + TypeSizeValue(Var, FALSE)), (unsigned long)Var);
#endif
        
    if (Var->ValOnHeap)
    { 
        if (Var->Val != nullptr)
            HeapFreeMem(Parser->pc, Var->Val);
            
        Success = HeapPopStack(Parser->pc, Var, MEM_ALIGN(sizeof(Value)));                       /* free from heap */
    }
    else if (Var->ValOnStack)
        Success = HeapPopStack(Parser->pc, Var, MEM_ALIGN(sizeof(Value)) + TypeSizeValue(Var, FALSE));  /* free from stack */
    else
        Success = HeapPopStack(Parser->pc, Var, MEM_ALIGN(sizeof(Value)));                       /* value isn't our problem */
        
    if (!Success)
        ProgramFail(Parser, "stack underrun");
}

/* add a stack frame when doing a function call */
void VariableStackFrameAdd(struct ParseState *Parser, const char *FuncName, int NumParams)
{
    struct StackFrame *NewFrame;
    
    HeapPushStackFrame(Parser->pc);
    NewFrame = static_cast<StackFrame *>(HeapAllocStack(Parser->pc, sizeof(struct StackFrame) +
                                                                    sizeof(Value *) * NumParams));
    if (NewFrame == nullptr)
        ProgramFail(Parser, "out of memory");
        
    ParserCopy(&NewFrame->ReturnParser, Parser);
    NewFrame->FuncName = FuncName;
    NewFrame->Parameter = static_cast<Value **>((NumParams > 0) ? ((void *) ((char *) NewFrame +
                                                                             sizeof(struct StackFrame))) : nullptr);
    TableInitTable(&NewFrame->LocalTable, &NewFrame->LocalHashTable[0], LOCAL_TABLE_SIZE, FALSE);
    NewFrame->PreviousStackFrame = Parser->pc->TopStackFrame;
    Parser->pc->TopStackFrame = NewFrame;
}

/* remove a stack frame */
void VariableStackFramePop(struct ParseState *Parser)
{
    if (Parser->pc->TopStackFrame == nullptr)
        ProgramFail(Parser, "stack is empty - can't go back");
        
    ParserCopy(Parser, &Parser->pc->TopStackFrame->ReturnParser);
    ShadowTableCleanup(Parser->pc, &Parser->pc->TopStackFrame->LocalTable);
    Parser->pc->TopStackFrame = Parser->pc->TopStackFrame->PreviousStackFrame;
    HeapPopStackFrame(Parser->pc);
}

/* get a string literal. assumes that Ident is already registered. nullptr if not found */
Value *VariableStringLiteralGet(Picoc *pc, char *Ident)
{
    Value *LVal = nullptr;

    if (TableGet(&pc->StringLiteralTable, Ident, &LVal, nullptr, nullptr, nullptr))
        return LVal;
    else
        return nullptr;
}

/* define a string literal. assumes that Ident is already registered */
void VariableStringLiteralDefine(Picoc *pc, char *Ident, Value *Val)
{
    TableSet(pc, &pc->StringLiteralTable, Ident, Val, nullptr, 0, 0);
}

/* check a pointer for validity and dereference it for use */
void *VariableDereferencePointer(struct ParseState *Parser, Value *PointerValue, Value **DerefVal, int *DerefOffset, struct ValueType **DerefType, int *DerefIsLValue)
{
    if (DerefVal != nullptr)
        *DerefVal = nullptr;
        
    if (DerefType != nullptr)
        *DerefType = PointerValue->Typ->FromType;
        
    if (DerefOffset != nullptr)
        *DerefOffset = 0;
        
    if (DerefIsLValue != nullptr)
        *DerefIsLValue = TRUE;

    return PointerValue->Val->Pointer;
}
