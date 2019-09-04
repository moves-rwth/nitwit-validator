//
// Created by jan on 11.4.19.
//

#include "automaton.hpp"
//#include "../picoc/picoc.hpp"

#ifndef VERBOSE
void cw_verbose(const string& Format, ...){}
#endif

string baseFileName(const string &s) {
    return s.substr(s.find_last_of("/\\") + 1);
}

// fixme bugs: this function isn't ideal - it will not work with for instance x == "  ;";
vector<string> split(string str, char delimiter) {
    int begin = 0;
    size_t n = count(str.begin(), str.end(), delimiter);
    auto result = vector<string>();
    if (n == 0 && str.empty()) {
        return result;
    } else if (n == 0) {
        result.push_back(str);
        return result;
    }
    result.reserve(n);
    for (size_t d = str.find_first_of(delimiter); d != string::npos; d = str.find_first_of(delimiter, d + 1)) {
        if (0 < d && d + 1 < str.length() && str[d - 1] == '\'' && str[d + 1] == '\'') {
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

        void *Tokens = nullptr;
        if (setjmp(state->pc->AssumptionPicocExitBuf)) {
            cw_verbose("Stopping assumption checker.\n");
            free(Tokens);
            HeapCleanup(state->pc);
            state->pc->HeapStackTop = heapstacktop_before;
            state->pc->HeapMemory = heapmemory_before;
            state->pc->HeapBottom = heapbottom_before;
            state->pc->StackFrame = stackframe;
            return false;
        }
        Tokens = LexAnalyse(state->pc, RegFileName, ass.c_str(), ass.length(), nullptr);
        ParseState Parser{};
        LexInitParser(&Parser, state->pc, ass.c_str(), Tokens, RegFileName, TRUE, FALSE, nullptr);
        int ret = 0;
        Value *value = nullptr;
        if (state->SkipIntrinsic && state->LastNonDetValue != nullptr &&
                (LexGetToken(&Parser, &value, FALSE) == TokenWitnessResult ||
                        (value != nullptr && value->Val->Identifier == TableStrRegister(state->pc, "result"))
                        // hack for VeriAbs - it outputs 'result' instead of '\result'
                )) {
            // handling \result in witnesses
            LexToken token = TokenNone;
            while (token != TokenEOF){
                token = LexGetToken(&Parser, nullptr, FALSE);
                if ((!(token >= TokenIntegerConstant && token <= TokenCharacterConstant) &&
                     token != TokenMinus)) {
                    token = LexGetToken(&Parser, nullptr, TRUE);
                } else break;
            }
            bool positive = true;
            if (LexGetToken(&Parser, nullptr, FALSE) == TokenMinus){
                LexGetToken(&Parser, nullptr, TRUE);
                positive = false;
            }
            LexGetToken(&Parser, &value, TRUE);
            if (value != nullptr) {
                if (!positive){
                    switch (value->Typ->Base){
                        case TypeDouble: value->Val->Double = -value->Val->Double; break;
                        case TypeChar: value->Val->Character = -value->Val->Character; break;
                        case TypeLong: value->Val->LongInteger = -value->Val->LongInteger; break;
                        case TypeUnsignedLong: value->Val->UnsignedLongInteger = -value->Val->UnsignedLongInteger; break;
                        case TypeLongLong: value->Val->LongLongInteger = -value->Val->LongLongInteger; break;
                        case TypeUnsignedLongLong: value->Val->UnsignedLongLongInteger = -value->Val->UnsignedLongLongInteger; break;
                        default: fprintf(stderr, "Type not found in parsing constant from assumption.\n"); break;
                    }
                }
                state->LastNonDetValue->Typ = TypeGetDeterministic(state, state->LastNonDetValue->Typ);
                ExpressionAssign(&Parser, state->LastNonDetValue, value, TRUE, nullptr, 0, TRUE);
//                state->LastNonDetValue->Val = value->Val; // TODO mem leak?
                state->LastNonDetValue = nullptr;
                ret = 1;
            }
        } else if (state->SkipIntrinsic) {
            ret = 0;
        } else {
            ret = AssumptionExpressionParseLongLong(&Parser);
        }
        free(Tokens);
        HeapCleanup(state->pc);
        state->pc->HeapStackTop = heapstacktop_before;
        state->pc->HeapMemory = heapmemory_before;
        state->pc->HeapBottom = heapbottom_before;
        state->pc->StackFrame = stackframe;

        ValueList *Next = Parser.ResolvedNonDetVars;
        for (ValueList *I = Next; I != nullptr; I = Next) {
#ifdef VERBOSE
            if (I->Identifier == nullptr) {
                cw_verbose("Resolved array element\n");
            } else {
                Value *val;
                VariableGet(state->pc, state, I->Identifier, &val);
                if (IS_FP(val)) {
                    double fp = CoerceDouble(val);
                    cw_verbose("Resolved var: %s: ---> %2.20f\n", I->Identifier, fp);
                } else {
                    int i = CoerceLongLong(val);
                    cw_verbose("Resolved var: %s: ---> %d\n", I->Identifier, i);
                }
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

void WitnessAutomaton::try_resolve_variables(ParseState *state) {
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

bool WitnessAutomaton::canTransitionFurther() {
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

#define UNSUCCESSFUL_TRIES_LIMIT 1000000
int UnsuccessfullTries = 0;
void WitnessAutomaton::consumeState(ParseState *state) {
    ++UnsuccessfullTries;
#ifdef ENABLE_TRANSITION_LIMIT
    if (UnsuccessfullTries > UNSUCCESSFUL_TRIES_LIMIT){
        ProgramFail(state, "limit to unsuccessful transitions exceeded");
    }
#endif
    if (state->VerifierErrorCalled && !this->verifier_error_called) {
        this->verifier_error_called = true;
        cw_verbose("__VERIFIER_error has been called!\n");
    }
    if (!canTransitionFurther()) return;

    bool could_go_to_sink = false;
    state->pc->IsInAssumptionMode = TRUE;
    for (const auto &edge: successor_rel.find(current_state->id)->second) {
#ifdef REQUIRE_MATCHING_ORIGINFILENAME
        if (!edge->origin_file.empty() && baseFileName(edge->origin_file) != baseFileName(string(state->FileName))) {
            continue;
        }
#endif
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
        state->pc->IsInAssumptionMode = FALSE;
        UnsuccessfullTries = 0; // reset counter
        return;
    }

    if (could_go_to_sink) {
        cw_verbose("\tTaking edge: %s --> sink\n", current_state->id.c_str());
        current_state = nodes.find("sink")->second;
    }
    state->pc->IsInAssumptionMode = FALSE;
}
