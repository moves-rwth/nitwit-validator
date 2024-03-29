/* picoc heap memory allocation. Uses your system's own malloc() allocator */
 
/* stack grows up from the bottom and heap grows down from the top of heap space */
#include "interpreter.hpp"

#ifdef DEBUG_HEAP
void ShowBigList(Picoc *pc)
{
    struct AllocNode *LPos;
    
    printf("Heap: bottom=%p %p-0x%lx, big freelist=", (void*)pc->HeapBottom, (void*)&(pc->HeapMemory)[0], (long)-1);
    for (LPos = pc->FreeListBig; LPos != nullptr; LPos = LPos->NextFree)
        printf("0x%lx:%d ", (long)LPos, LPos->Size);
    
    printf("\n");
}
#endif

/* initialise the stack and heap storage */
void HeapInit(Picoc* pc, int StackOrHeapSize) {
    int Count;
    int AlignOffset = 0;
    
    pc->HeapMemory = static_cast<unsigned char *>(malloc(StackOrHeapSize));
    if (pc->HeapMemory == nullptr) {
        std::cerr << "Failed to allocate " << StackOrHeapSize << " bytes of initial heap memory." << std::endl;
        throw;
    }

    pc->HeapBottom = nullptr;                     /* the bottom of the (downward-growing) heap */
    pc->StackFrame = nullptr;                     /* the current stack frame */
    pc->HeapStackTop = nullptr;                          /* the top of the stack */

    while (((unsigned long)&pc->HeapMemory[AlignOffset] & (sizeof(ALIGN_TYPE)-1)) != 0)
        AlignOffset++;
        
    pc->StackFrame = &(pc->HeapMemory)[AlignOffset];
    pc->HeapStackTop = &(pc->HeapMemory)[AlignOffset];
    *(void **)(pc->StackFrame) = nullptr;
    pc->HeapBottom = &(pc->HeapMemory)[StackOrHeapSize-sizeof(ALIGN_TYPE)+AlignOffset];
    pc->FreeListBig = nullptr;
    for (Count = 0; Count < FREELIST_BUCKETS; Count++)
        pc->FreeListBucket[Count] = nullptr;
}

void HeapCleanup(Picoc *pc) {
    free(pc->HeapMemory);
}

/* allocate some space on the stack, in the current stack frame
 * clears memory. can return nullptr if out of stack space */
void* HeapAllocStack(Picoc* pc, int Size) {
    char *NewMem = static_cast<char *>(pc->HeapStackTop);
    char *NewTop = (char *)pc->HeapStackTop + MEM_ALIGN(Size);
#ifdef DEBUG_HEAP
    printf("HeapAllocStack(%ld) at %p\n", (unsigned long)MEM_ALIGN(Size), (void*)pc->HeapStackTop);
#endif
    if (NewTop > (char*)pc->HeapBottom || NewTop < NewMem) {
        return nullptr;
    }

    pc->HeapStackTop = (void *)NewTop;
    memset((void *)NewMem, '\0', Size);
    return NewMem;
}

/* allocate some space on the stack, in the current stack frame */
void HeapUnpopStack(Picoc* pc, int Size) {
#ifdef DEBUG_HEAP
    printf("HeapUnpopStack(%ld) at %p\n", (unsigned long)MEM_ALIGN(Size), (void*)pc->HeapStackTop);
#endif
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop + MEM_ALIGN(Size));
}

/* free some space at the top of the stack */
int HeapPopStack(Picoc* pc, void* Addr, int Size) {
    int ToLose = MEM_ALIGN(Size);
    if (ToLose > ((char *)pc->HeapStackTop - (char *)&(pc->HeapMemory)[0]))
        return FALSE;
    
#ifdef DEBUG_HEAP
    printf("HeapPopStack(%p, %ld) back to %p\n", (void*)Addr, (unsigned long)MEM_ALIGN(Size), (void*)pc->HeapStackTop - ToLose);
#endif
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop - ToLose);
    assert(Addr == nullptr || pc->HeapStackTop == Addr);
    
    return TRUE;
}

/* push a new stack frame on to the stack */
void HeapPushStackFrame(Picoc* pc) {
#ifdef DEBUG_HEAP
    printf("Adding stack frame at %p\n", (void*)pc->HeapStackTop);
#endif
    *(void **)pc->HeapStackTop = pc->StackFrame;
    pc->StackFrame = pc->HeapStackTop;
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop + MEM_ALIGN(sizeof(ALIGN_TYPE)));
}

/* pop the current stack frame, freeing all memory in the frame. can return nullptr */
int HeapPopStackFrame(Picoc* pc) {
    if (*(void **)pc->StackFrame != nullptr)
    {
        pc->HeapStackTop = pc->StackFrame;
        pc->StackFrame = *(void **)pc->StackFrame;
#ifdef DEBUG_HEAP
        printf("Popping stack frame back to %p\n", (void*)pc->HeapStackTop);
#endif
        return TRUE;
    }
    else
        return FALSE;
}

/* allocate some dynamically allocated memory. memory is cleared. can return nullptr if out of memory */
void* HeapAllocMem(Picoc* pc, int Size) {
    return calloc(Size, 1);
}

/* free some dynamically allocated memory */
void HeapFreeMem(Picoc* pc, void* Mem) {
    free(Mem);
}

