/* picoc hash table module. This hash table code is used for both symbol tables
 * and the shared string table. */
 
#include "interpreter.hpp"

/* initialise the shared string system */
void TableInit(Picoc *pc)
{
    TableInitTable(&pc->StringTable, &pc->StringHashTable[0], STRING_TABLE_SIZE, TRUE);
    pc->StrEmpty = TableStrRegister(pc, "");
}

/* hash function for strings */
static unsigned int TableHash(const char *Key, unsigned Len)
{
    unsigned int Hash = Len;
    unsigned int Offset;
    unsigned int Count;
    
    for (Count = 0, Offset = 8; Count < Len; Count++, Offset+=7)
    {
        if (Offset > sizeof(unsigned int) * 8 - 7)
            Offset -= sizeof(unsigned int) * 8 - 6;
            
        Hash ^= *Key++ << Offset;
    }
    
    return Hash;
}

/* initialise a table */
void TableInitTable(struct Table *Tbl, struct TableEntry **HashTable, unsigned Size, int OnHeap)
{
    Tbl->Size = Size;
    Tbl->OnHeap = OnHeap;
    Tbl->HashTable = HashTable;
    memset((void *)HashTable, '\0', sizeof(struct TableEntry *) * Size);
}

/* check a hash table entry for a key */
struct TableEntry *TableSearch(struct Table *Tbl, const char *Key, unsigned *AddAt)
{
    struct TableEntry *Entry;
    int HashValue = ((unsigned long)Key) % Tbl->Size;   /* shared strings have unique addresses so we don't need to hash them */
    
    for (Entry = Tbl->HashTable[HashValue]; Entry != nullptr; Entry = Entry->Next)
    {
        if (Entry->p.v.Key == Key)
            return Entry;   /* found */
    }
    
    *AddAt = HashValue;    /* didn't find it in the chain */
    return nullptr;
}

/* set an identifier to a value. returns FALSE if it already exists. 
 * Key must be a shared string from TableStrRegister() */
int TableSet(Picoc *pc, struct Table *Tbl, char *Key, Value *Val, const char *DeclFileName, unsigned DeclLine, unsigned DeclColumn)
{
    unsigned AddAt;
    struct TableEntry *FoundEntry = TableSearch(Tbl, Key, &AddAt);
    
    if (FoundEntry == nullptr)
    {   /* add it to the table */
        struct TableEntry *NewEntry = static_cast<TableEntry *>(VariableAlloc(pc, nullptr, sizeof(struct TableEntry),
                                                                              Tbl->OnHeap));
        NewEntry->DeclFileName = DeclFileName;
        NewEntry->DeclLine = DeclLine;
        NewEntry->DeclColumn = DeclColumn;
        NewEntry->p.v.Key = Key;
        NewEntry->p.v.Val = Val;
        NewEntry->Next = Tbl->HashTable[AddAt];
        Tbl->HashTable[AddAt] = NewEntry;
        return TRUE;
    }

    return FALSE;
}

/* find a value in a table. returns FALSE if not found. 
 * Key must be a shared string from TableStrRegister() */
int TableGet(struct Table *Tbl, const char *Key, Value **Val, const char **DeclFileName, unsigned int *DeclLine, unsigned int *DeclColumn)
{
    unsigned AddAt;
    struct TableEntry *FoundEntry = TableSearch(Tbl, Key, &AddAt);
    if (FoundEntry == nullptr)
        return FALSE;
    
    *Val = FoundEntry->p.v.Val;
    
    if (DeclFileName != nullptr)
    {
        *DeclFileName = FoundEntry->DeclFileName;
        *DeclLine = FoundEntry->DeclLine;
        *DeclColumn = FoundEntry->DeclColumn;
    }
    
    return TRUE;
}


/* remove an entry from the table */
Value *TableDelete(Picoc *pc, struct Table *Tbl, const char *Key)
{
    struct TableEntry **EntryPtr;
    int HashValue = ((unsigned long)Key) % Tbl->Size;   /* shared strings have unique addresses so we don't need to hash them */
    
    for (EntryPtr = &Tbl->HashTable[HashValue]; *EntryPtr != nullptr; EntryPtr = &(*EntryPtr)->Next)
    {
        if ((*EntryPtr)->p.v.Key == Key)
        {
            struct TableEntry *DeleteEntry = *EntryPtr;
            Value *Val = DeleteEntry->p.v.Val;
            *EntryPtr = DeleteEntry->Next;
            HeapFreeMem(pc, DeleteEntry);

            return Val;
        }
    }

    return nullptr;
}

/* check a hash table entry for an identifier */
static struct TableEntry *TableSearchIdentifier(struct Table *Tbl, const char *Key, unsigned Len, unsigned *AddAt)
{
    struct TableEntry *Entry;
    int HashValue = TableHash(Key, Len) % Tbl->Size;
    
    for (Entry = Tbl->HashTable[HashValue]; Entry != nullptr; Entry = Entry->Next)
    {
        if (strncmp(&Entry->p.Key[0], (char *)Key, Len) == 0 && Entry->p.Key[Len] == '\0')
            return Entry;   /* found */
    }
    
    *AddAt = HashValue;    /* didn't find it in the chain */
    return nullptr;
}

/* set an identifier and return the identifier. share if possible */
char *TableSetIdentifier(Picoc *pc, struct Table *Tbl, const char *Ident, unsigned IdentLen)
{
    unsigned AddAt;
    struct TableEntry *FoundEntry = TableSearchIdentifier(Tbl, Ident, IdentLen, &AddAt);
    
    if (FoundEntry != nullptr)
        return &FoundEntry->p.Key[0];
    else
    {   /* add it to the table - we economise by not allocating the whole structure here */
        auto *NewEntry = static_cast<TableEntry *>(HeapAllocMem(pc, sizeof(struct TableEntry) -
                                                                                 sizeof(TableEntry::TableEntryPayload) +
                                                                                 IdentLen + 1));
        if (NewEntry == nullptr)
            ProgramFailNoParserWithExitCode(pc, 251, "Out of memory");
            
        strncpy((char *)&NewEntry->p.Key[0], (char *)Ident, IdentLen);
        NewEntry->p.Key[IdentLen] = '\0';
        NewEntry->Next = Tbl->HashTable[AddAt];
        Tbl->HashTable[AddAt] = NewEntry;
        return &NewEntry->p.Key[0];
    }
}

/* register a string in the shared string store */
char *TableStrRegister(Picoc *pc, const char *Str, unsigned Len)
{
    /* check if Len was initiliazed */
    if(Len == 0)
        Len = strlen((char *)Str);

    return TableSetIdentifier(pc, &pc->StringTable, Str, Len);
}

/* free all the strings */
void TableStrFree(Picoc *pc)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;
    
    for (Count = 0; Count < pc->StringTable.Size; Count++)
    {
        for (Entry = pc->StringTable.HashTable[Count]; Entry != nullptr; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            HeapFreeMem(pc, Entry);
        }
    }
}
