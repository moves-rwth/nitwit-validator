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

bool satisfiesAssumptions(ParseState *state, const shared_ptr<Edge> &edge) {
    auto assumptions = split(edge->assumption, ';');

    for (const auto &ass: assumptions) {

        long res = ExpressionParseInt(state);
        if (!res) {
            return false;
        }
    }

    return true;
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
