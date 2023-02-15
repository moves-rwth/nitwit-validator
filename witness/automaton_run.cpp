//
// Created by jan on 11.4.19.
//

#include "automaton.hpp"
//#include "../picoc/picoc.hpp"

#include "../picoc/CoerceT.hpp"

std::string baseFileName(const std::string& s) {
	return s.substr(s.find_last_of("/\\") + 1);
}

// fixme bugs: this function isn't ideal - it will not work with for instance x == "  ;";
std::vector<std::string> split(std::string const& str, char delimiter) {
	int begin = 0;
	std::size_t n = std::count(str.begin(), str.end(), delimiter);
	auto result = std::vector<std::string>();
	if (n == 0 && str.empty()) {
		return result;
	} else if (n == 0) {
		result.push_back(str);
		return result;
	}
	result.reserve(n);
	for (std::size_t d = str.find_first_of(delimiter); d != std::string::npos; d = str.find_first_of(delimiter, d + 1)) {
		if (0 < d && d + 1 < str.length() && str[d - 1] == '\'' && str[d + 1] == '\'') {
			continue;
		}
		std::string ass = str.substr(begin, d - begin);
		result.push_back(ass);
		begin = d + 1;
	}
	return result;
}

bool satisfiesAssumptionsAndResolve(ParseState *state, const std::shared_ptr<Edge>& edge) {
	auto assumptions = split(edge->assumption, ';');

	for (std::string const& ass: assumptions) {
		if (ass.empty()) {
			continue;
		}
#ifdef VERBOSE
		std::cout << "Working on assumption '" << ass << "'..." << std::endl;
#endif

		void *heapstacktop_before = state->pc->HeapStackTop;
		unsigned char *heapmemory_before = state->pc->HeapMemory;
		void *heapbottom_before = state->pc->HeapBottom;
		void *stackframe = state->pc->StackFrame;
		HeapInit(state->pc, 1048576); // 1 MB
		char* RegFileName = nitwit::table::TableStrRegister(state->pc, ("assumption " + ass).c_str());
		char* ResultString = nitwit::table::TableStrRegister(state->pc, "result");
		char* NaNString = nitwit::table::TableStrRegister(state->pc, "nan");

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

		// initialize tokens and parser for assumption checking
		Tokens = nitwit::lex::LexAnalyse(state->pc, RegFileName, ass.c_str(), ass.length(), nullptr);
		ParseState Parser{};
		nitwit::lex::LexInitParser(&Parser, state->pc, ass.c_str(), Tokens, RegFileName, TRUE, FALSE, nullptr);
		int ret = 0;
		Value *value = nullptr;

		if (state->SkipIntrinsic && state->LastNonDetValue != nullptr &&
			(nitwit::lex::LexGetToken(&Parser, &value, false) == TokenWitnessResult ||
			 (value != nullptr && strcmp(value->Val->Identifier, ResultString) == 0)
					// hack for VeriAbs - it outputs 'result' instead of '\result'
			)) {
			// handling \result in witnesses
			LexToken token = TokenNone;
			while (token != TokenEOF) {
				token = nitwit::lex::LexGetToken(&Parser, nullptr, false);
				if ((!(token >= TokenIntegerConstant && token <= TokenCharacterConstant) &&
					 token != TokenMinus)) {
					token = nitwit::lex::LexGetToken(&Parser, nullptr, true);
				} else {
					break;
				}
			}
			bool positive = true;
			if (nitwit::lex::LexGetToken(&Parser, nullptr, false) == TokenMinus) {
				nitwit::lex::LexGetToken(&Parser, nullptr, true);
				positive = false;
			}
			token = nitwit::lex::LexGetToken(&Parser, &value, true);
			if (value != nullptr) {
				// In case we read a "NaN", we need to fix this here
				if (token == TokenIdentifier && (value->Val != nullptr) && (value->Val->Identifier != nullptr) && (strcmpi(value->Val->Identifier, NaNString) == 0)) {
					value->Typ = &(Parser.pc->DoubleType);
					value->Val->Double = std::numeric_limits<double>::quiet_NaN();
				}

				if (!positive) {
					switch (value->Typ->Base) {
						case BaseType::TypeDouble:
							value->Val->Double = -value->Val->Double;
							break;
						case BaseType::TypeChar:
							value->Val->Character = -value->Val->Character;
							break;
						case BaseType::TypeLong:
							value->Val->LongInteger = -value->Val->LongInteger;
							break;
						case BaseType::TypeUnsignedLong:
							value->Val->UnsignedLongInteger = -value->Val->UnsignedLongInteger;
							break;
						case BaseType::TypeLongLong:
							value->Val->LongLongInteger = -value->Val->LongLongInteger;
							break;
						case BaseType::TypeUnsignedLongLong:
							value->Val->UnsignedLongLongInteger = -value->Val->UnsignedLongLongInteger;
							break;
						default:
							fprintf(stderr, "Type not found in parsing constant from assumption.\n");
							break;
					}
				}
				state->LastNonDetValue->Typ = TypeGetDeterministic(state, state->LastNonDetValue->Typ);
				nitwit::assumptions::ExpressionAssign(&Parser, state->LastNonDetValue, value, TRUE, nullptr, 0, TRUE);
				state->LastNonDetValue = nullptr;
				ret = 1;
			}
		} else if (state->SkipIntrinsic) {
			ret = 0;
		} else {
			ret = nitwit::assumptions::ExpressionParseLongLong(&Parser);
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
					double fp = CoerceT<double>(val);
					cw_verbose("Resolved var in run: %s: ---> %.17g\n", I->Identifier, fp);
				} else {
					long long i = CoerceT<long long>(val);
					cw_verbose("Resolved var in run: %s: ---> %lli\n", I->Identifier, i);
				}
				cw_verbose("Variable %s is NonDet: %s\n", I->Identifier, TypeIsNonDeterministic(val->Typ) ? "y" : "n");
			}
#endif
			Next = Next->Next;
			free(I);
		}

#ifdef VERBOSE
		std::cout << "Work on assumption '" << ass << "' done." << std::endl;
#endif

		if (!ret) {
			return false;
		}
	}

	return true;
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
	const auto& succs = successor_rel.find(current_state->id);
	if (succs == successor_rel.end()) { // state isn't sink
		this->illegal_state = true;
		return false;
	}
	return true;
}

#if !defined(UNSUCCESSFUL_TRIES_LIMIT) || (UNSUCCESSFUL_TRIES_LIMIT <= 0)
#error The macro UNSUCCESSFUL_TRIES_LIMIT macro is not defined or <= 0!
#endif

std::size_t WitnessAutomaton::getUnsuccessfulTries() const {
	return unsuccessfulTries;
}

bool WitnessAutomaton::consumeState(ParseState *state, bool isMultiLineDeclaration, std::size_t const& endLine, bool isInitialCheck) {
	static std::size_t lastLineUsed = 0;
	static bool lastLineUsedValid = false;

	if (isInitialCheck) {
		lastLineUsedValid = false;
	}

	cw_verbose("Consuming state using witness automaton.\n");
	++unsuccessfulTries;
#ifdef ENABLE_TRANSITION_LIMIT
	if (unsuccessfulTries > UNSUCCESSFUL_TRIES_LIMIT) {
		ProgramFail(state, "limit to unsuccessful transitions exceeded");
	}
#endif
	if (state->pc->VerifierErrorFunctionWasCalled && !this->verifier_error_called) {
		this->verifier_error_called = true;
		cw_verbose("Error function has been called!\n");
	}
	if (!canTransitionFurther()) {
		cw_verbose("Can not transition further.\n");
		return false;
	}

	bool could_go_to_sink = false;
	state->pc->IsInAssumptionMode = TRUE;
	for (auto const& edge: successor_rel.find(current_state->id)->second) {
#ifdef REQUIRE_MATCHING_ORIGINFILENAME
		if (!edge->origin_file.empty() && baseFileName(edge->origin_file) != baseFileName(string(state->FileName))) {
			continue;
		}
#endif
		if (!((edge->start_line <= state->Line && (state->Line <= edge->end_line)) || (isMultiLineDeclaration && state->Line <= edge->start_line && edge->end_line <= endLine))) {
			if (!(edge->start_line == 0 && edge->end_line == 0)) {
#ifdef DEBUG_WITNESS_EDGES
				cw_verbose("Edge line constraints do not match, !((start_line = %zu <= %zu and %zu <= %zu = end_line) or (isMultiLineDeclaration = %i and state->Line = %zu <= %zu = start_line && end_line = %zu <= %zu = endLine)) for assumption '%s'.\n", edge->start_line, state->Line, state->Line, edge->end_line, isMultiLineDeclaration ? 1 : 0, state->Line, edge->start_line, edge->end_line, endLine, edge->assumption.c_str());
#endif
				continue;
			}
		}

		// Check that we were not working on the same line
		if (lastLineUsedValid && (edge->start_line == edge->end_line) && (edge->start_line == lastLineUsed)) {
			continue;
		}
		lastLineUsedValid = false;
#ifdef DEBUG_WITNESS_EDGES
		

		cw_verbose("Edge line constraints do match, (start_line = %zu <= %zu and %zu <= %zu = end_line) or (isMultiLineDeclaration = %i and state->Line = %zu <= %zu = start_line && end_line = %zu <= %zu = endLine) for assumption '%s'.\n", edge->start_line, state->Line, state->Line, edge->end_line, isMultiLineDeclaration ? 1 : 0, state->Line, edge->start_line, edge->end_line, endLine, edge->assumption.c_str());
#endif

		// check assumption
		cw_verbose("About to check assumption '%s'.\n", edge->assumption.c_str());
		if (!edge->assumption.empty() && !satisfiesAssumptionsAndResolve(state, edge)) {
			cw_verbose("Unmet assumption '%s'.\n", edge->assumption.c_str());
			continue;
		} else if (!edge->assumption.empty()) {
			cw_verbose("Assumption '%s' satisfied.\n", edge->assumption.c_str());
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
		unsuccessfulTries = 0; // reset counter

		lastLineUsedValid = (edge->start_line == edge->end_line);
		lastLineUsed = edge->start_line;

		return true;
	}

	if (could_go_to_sink) {
		cw_verbose("\tTaking edge: %s --> sink\n", current_state->id.c_str());
		current_state = nodes.find("sink")->second;
		state->pc->IsInAssumptionMode = FALSE;
		return true;
	}
	state->pc->IsInAssumptionMode = FALSE;
	return false;
}
