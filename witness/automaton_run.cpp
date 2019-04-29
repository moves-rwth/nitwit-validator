//
// Created by jan on 11.4.19.
//

#include "automaton.hpp"
#include <functional>
#include <algorithm>
#include <iostream>

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


int PicocParseAssumption(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int RunIt,
                         int CleanupSource, int EnableDebugger, ParseState *main_state) {
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
//
//    // select the same scope
//    Parser.ScopeID = main_state->ScopeID;
//    pc->GlobalTable = main_state->pc->GlobalTable;
//    memcpy(pc->GlobalHashTable, main_state->pc->GlobalHashTable, GLOBAL_TABLE_SIZE * sizeof(TableEntry));
//    pc->StringLiteralTable = main_state->pc->StringLiteralTable;
//    memcpy(pc->StringLiteralHashTable, main_state->pc->StringLiteralHashTable,
//           STRING_LITERAL_TABLE_SIZE * sizeof(TableEntry));
//    pc->TopStackFrame = main_state->pc->TopStackFrame;
//    pc->HeapMemory = main_state->pc->HeapMemory;
//    pc->HeapBottom = main_state->pc->HeapBottom;
//    pc->StackFrame = main_state->pc->StackFrame;
//    pc->HeapStackTop = main_state->pc->HeapStackTop;
//    pc->StringTable = main_state->pc->StringTable;
//    memcpy(pc->StringHashTable, main_state->pc->StringHashTable, STRING_TABLE_SIZE * sizeof(TableEntry));

    if (VariableDefined(pc, TableStrRegister(pc, "foo"))) {
        printf("Foo defined\n");
        return true;
    }
    if (VariableDefined(pc, TableStrRegister(pc, "x"))) {
        printf("X defined\n");
        return true;
    }
    int ret = AssumptionExpressionParseInt(&Parser);
    return ret;
}

bool satisfiesAssumptions(ParseState *state, const shared_ptr<Edge> &edge) {
    auto assumptions = split(edge->assumption, ';');
//    edge->print();
    for (const auto &ass: assumptions) {
        Picoc pc;
        PicocInitialise(&pc, 8388608); // stack size of 8 MiB

        if (PicocPlatformSetExitPoint(&pc)) {
            printf("Stopping assumption checker.\n");
            PicocCleanup(&pc);
            return false;
        }
        int res = PicocParseAssumption(&pc, "assumption-1243asdfeqv45q", ass.c_str(), ass.length(),
                                       TRUE, FALSE, FALSE, state);
        PicocCleanup(&pc);
        if (!res) {
            return false;
        }

    }

    return assumptions.empty();
}

void Automaton::consumeState(ParseState *state) {
    if (current_state == nullptr || this->isInIllegalState()) {
        this->illegal_state = true;
        return;
    }

    if (current_state->is_violation || current_state->is_sink) {
        // don't do anything once we have reached violation or sink
        return;
    }
    const auto &succs = successor_rel.find(current_state->id);
    if (succs == successor_rel.end()) { // state isn't sink
        this->illegal_state = true;
        return;
    }

    for (const auto &edge: succs->second) {
        if (baseFileName(edge->origin_file) == baseFileName(string(state->FileName)) &&
            edge->start_line == state->Line) {
            bool takesEdge = true;
            // check control branch
            if (edge->controlCondition != ConditionUndefined || state->LastConditionBranch != ConditionUndefined) {
                if (edge->controlCondition == state->LastConditionBranch) {
                    takesEdge = true;
                } else {
                    continue;
                }
            }

            // check assumption
            if (takesEdge && !edge->assumption.empty() && !satisfiesAssumptions(state, edge)) {
                takesEdge = false;
            }

            if (takesEdge) {
                current_state = nodes.find(edge->target_id)->second;
                printf("\tTaking edge: %s --> %s\n", edge->source_id.c_str(), edge->target_id.c_str());
                return;
            }
        }
    }

}
