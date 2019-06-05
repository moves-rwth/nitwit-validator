//
// Created by jan on 11.4.19.
//

#include "automaton.hpp"

#ifndef VERBOSE
void cw_verbose(const string& Format, ...){}
#endif

bool assignValue(Value *DestValue, Value* FromValue) {

    if (IS_FP(DestValue)) {
        DestValue->Val->FP = ExpressionCoerceFP(FromValue);
    } else {
        long FromInt = ExpressionCoerceLongLong(FromValue);
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
            default: return false;
        }
    }
    return true;
}

string baseFileName(const string &s) {
    return s.substr(s.find_last_of("/\\") + 1);
}

// fixme bugs: this function isn't ideal - it will not work with for instance x == "  ;";
vector<string> split(string str, char delimiter) {
    int begin = 0;
    size_t n = count(str.begin(), str.end(), delimiter);
    if (n == 0) {
        return vector<string>();
    }
    auto result = vector<string>();
    result.reserve(n);
    for (size_t d = str.find_first_of(delimiter); d != string::npos; d = str.find_first_of(delimiter, d+1)) {
        if (0 < d && d + 1 < str.length() && str[d-1] == '\'' && str[d+1] == '\''){
            continue;
        }
        string ass = str.substr(begin, d - begin);
        result.push_back(ass);
        begin = d + 1;
    }
    return result;
}

bool satisfiesAssumptionsAndResolve(ParseState *state, const shared_ptr<Edge> &edge) {
    auto assumptions = split(edge->assumption, ';');
    for (const string &ass: assumptions) {
        if (ass.empty()) {
            continue;
        }

        void *heapstacktop_before = state->pc->HeapStackTop;
        unsigned char *heapmemory_before = state->pc->HeapMemory;
        void *heapbottom_before = state->pc->HeapBottom;
        void *stackframe = state->pc->StackFrame;
        HeapInit(state->pc, 1048576); // 1 MB todo don't allocate every time new...
        char *RegFileName = TableStrRegister(state->pc, ("assumption " + ass).c_str());

        state->pc->IsInAssumptionMode = TRUE;
        void *Tokens = nullptr;
        if (setjmp(state->pc->AssumptionPicocExitBuf)) {
            cw_verbose("Stopping assumption checker.\n");
            free(Tokens);
            HeapCleanup(state->pc);
            state->pc->IsInAssumptionMode = FALSE;
            state->pc->HeapStackTop = heapstacktop_before;
            state->pc->HeapMemory = heapmemory_before;
            state->pc->HeapBottom = heapbottom_before;
            state->pc->StackFrame = stackframe;
            return false;
        }
        Tokens = LexAnalyse(state->pc, RegFileName, ass.c_str(), ass.length(), nullptr);
        ParseState Parser{};
        LexInitParser(&Parser, state->pc, ass.c_str(), Tokens, RegFileName, TRUE, FALSE, nullptr);
        int ret = AssumptionExpressionParseLongLong(&Parser);
        free(Tokens);
        HeapCleanup(state->pc);
        state->pc->IsInAssumptionMode = FALSE;
        state->pc->HeapStackTop = heapstacktop_before;
        state->pc->HeapMemory = heapmemory_before;
        state->pc->HeapBottom = heapbottom_before;
        state->pc->StackFrame = stackframe;

        ValueList *Next = Parser.ResolvedNonDetVars;
        for (ValueList *I = Next; I != nullptr; I = Next) {
#ifdef VERBOSE
            Value *val;
            VariableGet(state->pc, state, I->Identifier, &val);
            if (IS_FP(val)) {
                double fp = AssumptionExpressionCoerceFP(val);
                cw_verbose("Resolved var: %s: ---> %2.20f\n", I->Identifier, fp);
            } else {
                int i = AssumptionExpressionCoerceLongLong(val);
                cw_verbose("Resolved var: %s: ---> %d\n", I->Identifier, i);
            }
#endif
            Next = Next->Next;
            free(I);
        }
        if (!ret) {
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
            cw_verbose("Forward resolving of %s.\n", edge->assumption.c_str());
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

        // check assumption
        if (!edge->assumption.empty() && !satisfiesAssumptionsAndResolve(state, edge)) {
            cw_verbose("Unmet assumption %s.\n", edge->assumption.c_str());
            continue;
        } else if (!edge->assumption.empty()) {
            cw_verbose("Assumption %s satisfied.\n", edge->assumption.c_str());
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

        // check control branch
        if (edge->controlCondition != ConditionUndefined || state->LastConditionBranch != ConditionUndefined) {
            if (edge->controlCondition != state->LastConditionBranch) {
                continue;
            }
        }

        if (edge->target_id == "sink") {
            could_go_to_sink = true;
            continue;
            // prefer to follow through to other edges than sink,
            // but if nothing else is possible, take it
        }
        current_state = nodes.find(edge->target_id)->second;
        cw_verbose("\tTaking edge: %s --> %s\n", edge->source_id.c_str(), edge->target_id.c_str());

        return;
    }

    if (could_go_to_sink) {
        cw_verbose("\tTaking edge: %s --> sink\n", current_state->id.c_str());
        current_state = nodes.find("sink")->second;
    }
}
