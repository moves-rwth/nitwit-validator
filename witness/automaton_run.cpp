//
// Created by jan on 11.4.19.
//

#include "automaton.hpp"

#ifndef VERBOSE
void cw_verbose(const string& Format, ...){}
#endif

void assignValue(Value *DestValue, Value* FromValue) {

    if (IS_FP(DestValue)) {
        DestValue->Val->FP = ExpressionCoerceFP(FromValue);
    } else {
        long FromInt = ExpressionCoerceInteger(FromValue);
        switch (DestValue->Typ->Base)
        {
            case TypeInt:           DestValue->Val->Integer = FromInt; break;
            case TypeShort:         DestValue->Val->ShortInteger = (short)FromInt; break;
            case TypeChar:          DestValue->Val->Character = (char)FromInt; break;
            case TypeLong:          DestValue->Val->LongInteger = (long)FromInt; break;
            case TypeUnsignedInt:   DestValue->Val->UnsignedInteger = (unsigned int)FromInt; break;
            case TypeUnsignedShort: DestValue->Val->UnsignedShortInteger = (unsigned short)FromInt; break;
            case TypeUnsignedLong:  DestValue->Val->UnsignedLongInteger = (unsigned long)FromInt; break;
            case TypeUnsignedChar:  DestValue->Val->UnsignedCharacter = (unsigned char)FromInt; break;
            default: break;
        }
    }
}

string baseFileName(const string &s) {
    return s.substr(s.find_last_of("/\\") + 1);
}

vector<string> split(string str, char delimiter) {
    int begin = 0;
    size_t n = count(str.begin(), str.end(), delimiter);
    if (n == 0) {
        return vector<string>();
    }
    auto result = vector<string>();
    result.reserve(n);
    for (size_t d = str.find_first_of(delimiter); d != string::npos; d = str.find_first_of(delimiter, d + 1)) {
        string ass = str.substr(begin, d);
        result.push_back(ass);
    }
    return result;
}

bool copyStringTable(ParseState *state, Picoc *to, Picoc *from) {
    if (to->StringTable.Size != from->StringTable.Size) {
        fprintf(stderr, "String tables have different size.\n");
        return false;
    }
    // copy defined strings
    for (short s = 0; s < to->StringTable.Size; ++s) {
        if (from->StringTable.HashTable[s] == nullptr) continue;
        for (TableEntry *e = from->StringTable.HashTable[s]; e != nullptr; e = e->Next) {
            TableStrRegister(to, e->p.Key);
        }
    }

    if (from->TopStackFrame != nullptr) {
        if (to->TopStackFrame == nullptr) {
            VariableStackFrameAdd(state, from->TopStackFrame->FuncName, from->TopStackFrame->NumParams);
        }
        if (to->TopStackFrame->LocalTable.Size != from->TopStackFrame->LocalTable.Size) {
            fprintf(stderr, "Local stack frame tables have different size.\n");
            return false;
        }


        // copy defined local variables and values
        for (short s = 0; s < to->TopStackFrame->LocalTable.Size; ++s) {
            if (from->TopStackFrame->LocalTable.HashTable[s] == nullptr) continue;
            for (TableEntry *e = from->TopStackFrame->LocalTable.HashTable[s]; e != nullptr; e = e->Next) {
                if (!VariableDefined(to, TableStrRegister(to, e->p.v.Key))){
                    VariableDefine(to, state, TableStrRegister(to, e->p.v.Key), e->p.v.Val,
                        e->p.v.Val->Typ == &from->FPType ? &to->FPType : e->p.v.Val->Typ, e->p.v.Val->IsLValue);
                }
            }
        }
    }
    // copy defined global variables and values
    for (short s = 0; s < to->GlobalTable.Size; ++s) {
        if (from->GlobalTable.HashTable[s] == nullptr) continue;
        for (TableEntry *e = from->GlobalTable.HashTable[s]; e != nullptr; e = e->Next) {
            if (!VariableDefined(to, TableStrRegister(to, e->p.v.Key))){
                VariableDefine(to, state, TableStrRegister(to, e->p.v.Key), e->p.v.Val,
                           e->p.v.Val->Typ == &from->FPType ? &to->FPType : e->p.v.Val->Typ, e->p.v.Val->IsLValue);
            }
        }
    }

    return true;
}

void CopyValue(ParseState *ToState, Value *FromValue, const char *Identifier, bool IsGlobal) {
    char *registeredIdent = TableStrRegister(ToState->pc, Identifier);
    // todo could top stack be null?
    Table *table = IsGlobal ? &ToState->pc->GlobalTable : &ToState->pc->TopStackFrame->LocalTable;

    int addAt;
    TableEntry *entry = TableSearch(table, registeredIdent, &addAt);
//todo
//    if (entry->p.v.Val->IsNonDet != nullptr)
//        *entry->p.v.Val->IsNonDet = false;
    assignValue(entry->p.v.Val, FromValue);
}

int PicocParseAssumptionAndResolve(Picoc *pc, const char *FileName, const char *Source,
                                   int SourceLen, int RunIt, int CleanupSource,
                                   int EnableDebugger, ParseState *main_state, bool IsGlobal) {
    struct ParseState Parser{};
    struct CleanupTokenNode *NewCleanupNode;
    char *RegFileName = TableStrRegister(pc, FileName);

    void *Tokens = LexAnalyse(pc, RegFileName, Source, SourceLen, nullptr);

    /* allocate a cleanup node so we can clean up the tokens later */
    {
        NewCleanupNode = static_cast<CleanupTokenNode *>(HeapAllocMem(pc, sizeof(struct CleanupTokenNode)));
        if (NewCleanupNode == nullptr) {
            ProgramFailNoParser(pc, "out of memory");
            return 0;
        }
        NewCleanupNode->Tokens = Tokens;
        if (CleanupSource)
            NewCleanupNode->SourceText = Source;
        else
            NewCleanupNode->SourceText = nullptr;

        NewCleanupNode->Next = pc->CleanupTokenList;
        pc->CleanupTokenList = NewCleanupNode;
    }

    /* do the parsing */
    LexInitParser(&Parser, pc, Source, Tokens, RegFileName, RunIt, EnableDebugger, nullptr);

    copyStringTable(&Parser, pc, main_state->pc);

    int ret = AssumptionExpressionParseInt(&Parser);

    ValueList *Next = Parser.ResolvedNonDetVars;
    for (ValueList *I = Next; I != nullptr; I = Next) {
        Value *val, *val_old;
        VariableGet(pc, &Parser, I->Identifier, &val);
        VariableGet(main_state->pc, main_state, TableStrRegister(main_state->pc, I->Identifier), &val_old);
        CopyValue(main_state, val, I->Identifier, IsGlobal);
#ifdef VERBOSE
        if (IS_FP(val_old)) {
            double fp_old = AssumptionExpressionCoerceFP(val_old);
            cw_verbose("Resolved var: %s: %f --> ", I->Identifier, fp_old);
            VariableGet(main_state->pc, main_state, TableStrRegister(main_state->pc, I->Identifier), &val_old);
            fp_old = AssumptionExpressionCoerceFP(val_old);
            cw_verbose("%2.20f\n", fp_old);
        } else {
            int i_old = AssumptionExpressionCoerceInteger(val_old);
            cw_verbose("Resolved var: %s: %d --> ", I->Identifier, i_old);
            VariableGet(main_state->pc, main_state, TableStrRegister(main_state->pc, I->Identifier), &val_old);
            i_old = AssumptionExpressionCoerceInteger(val_old);
            cw_verbose("%d\n", i_old);
        }
#endif
        Next = Next->Next;
        free(I);
    }

    return ret;
}

bool satisfiesAssumptionsAndResolve(ParseState *state, const shared_ptr<Edge> &edge) {
    // check scope
    bool IsGlobalScope = true;
    if (!edge->assumption_scope.empty()) {
        IsGlobalScope = false;
        if (state->pc->TopStackFrame == nullptr) { // not in a function at all
            return false;
        }
        if (strcmp(state->pc->TopStackFrame->FuncName, edge->assumption_scope.c_str()) != 0) { // not in scope
            return false;
        }
    }
    auto assumptions = split(edge->assumption, ';');
    for (const auto &ass: assumptions) {

        Picoc pc;
        PicocInitialise(&pc, 8388608); // stack size of 8 MiB

        if (PicocPlatformSetExitPoint(&pc)) {
            cw_verbose("Stopping assumption checker.\n");
            PicocCleanup(&pc);
            return false;
        }
        int res = PicocParseAssumptionAndResolve(&pc, "assumption-1243asdfeqv45q", ass.c_str(), ass.length(),
                                                 TRUE, FALSE, FALSE, state, IsGlobalScope);

        PicocCleanup(&pc);
        if (!res) {
            return false;
        }
    }

    return true;
}


void Automaton::try_resolve_variables(ParseState *state) {
    if (!canTransitionFurther()) return;
    vector<shared_ptr<Edge>> possible_edges;
    for (const auto &edge: successor_rel.find(current_state->id)->second) {
        if (!edge->assumption.empty())
            possible_edges.push_back(edge);
    }
    if (possible_edges.size() > 1) {
        cw_verbose("Non-deterministic choice encountered - there are more than 1 forward assumptions at %s. Skipping "
                   "forward assumption resolution.", current_state->id.c_str());

    } else {
        for (const auto &edge: possible_edges) {
            satisfiesAssumptionsAndResolve(state, edge);
        }
    }
}

bool Automaton::canTransitionFurther() {
    if (current_state == nullptr || this->isInIllegalState()) {
        this->illegal_state = true;
        return false;
    }
    if (current_state->is_violation || current_state->is_sink) {
        // don't do anything once we have reached violation or sink
        return false;
    }
    const auto &succs = successor_rel.find(current_state->id);
    if (succs == successor_rel.end()) { // state isn't sink
        this->illegal_state = true;
        return false;
    }
    return true;
}

void Automaton::consumeState(ParseState *state) {
    if (state->VerifierErrorCalled && !this->verifier_error_called) {
        this->verifier_error_called = true;
        cw_verbose("__VERIFIER_error has been called!\n");
    }
    if (!canTransitionFurther()) return;

    bool could_go_to_sink = false;
    for (const auto &edge: successor_rel.find(current_state->id)->second) {
        if (!edge->origin_file.empty() && baseFileName(edge->origin_file) != baseFileName(string(state->FileName))) {
            continue;
        }
        if (!(edge->start_line <= state->Line && state->Line <= edge->end_line)) {
            if (!(edge->start_line == 0 && edge->end_line == 0)) continue;
        }
        // check control branch
        if (edge->controlCondition != ConditionUndefined || state->LastConditionBranch != ConditionUndefined) {
            if (edge->controlCondition != state->LastConditionBranch) {
                continue;
            }
        }
        // check enter function
        if (!edge->enter_function.empty() && edge->enter_function != "main") {
            if (state->EnterFunction == nullptr ||
                strcmp(state->EnterFunction, edge->enter_function.c_str()) != 0) {
                continue;
            }
        }

        // check return function
        if (!edge->return_from_function.empty() && edge->return_from_function != "main") {
            if (state->ReturnFromFunction == nullptr ||
                strcmp(state->ReturnFromFunction, edge->return_from_function.c_str()) != 0) {
                continue;
            }
        }

        // check assumption
        if (!edge->assumption.empty() && !satisfiesAssumptionsAndResolve(state, edge)) {
            cw_verbose("Unmet assumption %s.\n", edge->assumption.c_str());
            continue;
        } else if (!edge->assumption.empty()) {
            cw_verbose("Assumption %s satisfied.\n", edge->assumption.c_str());
        }

        if (edge->target_id == "sink") {
            could_go_to_sink = true;
            continue;
            // prefer to follow through to other edges than sink,
            // but if nothing else is possible, take it
        }
        current_state = nodes.find(edge->target_id)->second;
        cw_verbose("\tTaking edge: %s --> %s\n", edge->source_id.c_str(), edge->target_id.c_str());
        try_resolve_variables(state);

        return;
    }

    if (could_go_to_sink) {
        current_state = nodes.find("sink")->second;
    }
}
