#include <cstdio>
#include <cstring>
#include <iostream>

#include "picoc/picoc.hpp"

#undef min

#include "utils/files.hpp"
#include "witness/witness.hpp"
#include "witness/automaton.hpp"

std::shared_ptr<WitnessAutomaton> wit_aut;

// the values shouldn't conflict with any real program exit value as validation ends before returning for these error codes
// Only if program finishes with value PROGRAM_FINISHED, then it still doesn't matter, because this would mean
// that it did not get validated
int RESULT_UNKNOWN = 4;
int NO_WITNESS_CODE = 240;
int WITNESS_IN_SINK = 241;
int PROGRAM_FINISHED = 242;
int WITNESS_IN_ILLEGAL_STATE = 243;
int IDENTIFIER_UNDEFINED = 244;
int PROGRAM_FINISHED_WITH_VIOLATION_THOUGH_NOT_IN_VIOLATION_STATE = 245;
int ALREADY_DEFINED = 246;
int UNSUPPORTED_NONDET_RESOLUTION_OP = 247;
int ASSERTION_FAILED = 248;
int BAD_FUNCTION_DEF = 249;
int UNVALIDATED_VIOLATION = 250;
int OUT_OF_MEMORY = 251;

void process_resource_usage(double &mem, double &cpu);

void printProgramState(ParseState *ps) {
	std::cout << "--- Line: " << ps->Line << ", Pos: " << ps->CharacterPos;
	if (ps->LastConditionBranch != ConditionUndefined) {
		std::cout << ", Control: " << (ps->LastConditionBranch == ConditionTrue);
	}
	if (ps->EnterFunction != nullptr) {
		std::cout << ", Enter: " << ps->EnterFunction;
	}
	if (ps->ReturnFromFunction != nullptr) {
		std::cout << ", Return: " << ps->ReturnFromFunction;
	}
	std::cout << std::endl;
}

void handleDebugBreakpoint(struct ParseState *ps) {
#ifdef VERBOSE
	printProgramState(ps);
#endif
	if (wit_aut == nullptr) {
		ProgramFailWithExitCode(ps, NO_WITNESS_CODE, "No witness automaton to validate against.");
		return;
	}
	if (wit_aut->isInIllegalState()) {
		ProgramFailWithExitCode(ps, WITNESS_IN_ILLEGAL_STATE, "Witness automaton is in an illegal state.");
		return;
	}
	if (wit_aut->isInSinkState()) {
#ifdef STOP_IN_SINK
		ProgramFailWithExitCode(ps, WITNESS_IN_SINK, "Witness automaton reached sink state without a violation.");
		return;
#endif
	}

	wit_aut->consumeState(ps);

	if (wit_aut->wasVerifierErrorCalled()) {
		PlatformExit(ps->pc, 0);
		return;
	}
}

int validate(const char *source_filename, const char *error_function_name) {

	Picoc pc;
	PicocInitialise(&pc, 104857600); // stack size of 100 MiB
	pc.VerifierErrorFuncName = error_function_name;

	// the interpreter will jump here after finding a violation
	if (PicocPlatformSetExitPoint(&pc)) {
		cw_verbose("===============Finished=================\n");
		cw_verbose("Stopping the interpreter.\n");
		int ret = pc.PicocExitValue;
		PicocCleanup(&pc);
		return ret;
	}

	cw_verbose("============Start simulation============\n");
	// include all standard libraries and extern functions used by verifiers
	// like stdio, stdlib, special error, assume, nondet functions

#ifndef NO_HEADER_INCLUDE
	PicocIncludeAllSystemHeaders(&pc);
#endif

	char *source = readFile(source_filename);
	if (!source) {
		return 255;
	}
	PicocParse(&pc, source_filename, source, strlen(source), TRUE, FALSE, TRUE, TRUE, handleDebugBreakpoint);

	Value *MainFuncValue = nullptr;
	VariableGet(&pc, nullptr, TableStrRegister(&pc, "main"), &MainFuncValue);


	if (MainFuncValue->Typ->Base != TypeFunction) {
		ProgramFailNoParser(&pc, "main is not a function - can't call it");
	}

	PicocCallMain(&pc, nullptr, 0, nullptr);
	cw_verbose("===============Finished=================\n\n");
	cw_verbose("Program finished. Exit value: %d\n", pc.PicocExitValue);

	PicocCleanup(&pc);
	return PROGRAM_FINISHED;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		std::cout << "Usage: <nitwit> witness.graphml source-file.c errorFunctionName" << std::endl;
		return 3;
	}

	auto doc = parseGraphmlWitness(argv[1]);
	if (doc == nullptr) {
		return 2;
	}
	wit_aut = WitnessAutomaton::automatonFromWitness(doc);

	// check if witness automaton was successfully constructed
	if (wit_aut && !wit_aut->isInIllegalState()) {
		cw_verbose("Witness automaton reconstructed\n");
	} else {
		std::cout << "Reconstructing the witness automaton failed." << std::endl;
		return 2;
	}

	// check if witness automaton has type violation witness
	if (!wit_aut->getData().witness_type.empty() && wit_aut->getData().witness_type != "violation_witness") {
		std::cout << "UNKNOWN: NITWIT expects a violation witness yet a different type was specified: " << wit_aut->getData().witness_type << "." << std::endl;
		return RESULT_UNKNOWN;
	}

	int exit_value = validate(argv[2], argv[3]);
	std::cout << "Witness in violation state: " << (wit_aut->isInViolationState() ? "yes" : "no") << std::endl;
	std::cout << "Error function \"" << argv[3] << "\" called during execution: " << (wit_aut->wasVerifierErrorCalled() ? "yes" : "no") << std::endl;

	// check whether we finished in a violation state and if __VERIFIER_error was called
	if ((!wit_aut->isInViolationState() || !wit_aut->wasVerifierErrorCalled()) &&
		(exit_value >= NO_WITNESS_CODE && exit_value <= ALREADY_DEFINED)) {
		cw_verbose("WitnessAutomaton finished in state %s, with error code %d.\n",
				   wit_aut->getCurrentState()->id.c_str(),
				   exit_value);
		std::cout << "FAILED: Wasn't able to validate the witness.";

		// check whether we finished in a violation state
		if (wit_aut->isInViolationState()) {
			std::cout << " #*# Witness violation state reached";
			exit_value = UNVALIDATED_VIOLATION;
		} else {
			std::cout << " #*# Witness violation state NOT reached";
		}

		// check whether we finished in a state where __VERIFIER_error was called
		if (wit_aut->wasVerifierErrorCalled()) {
			std::cout << ", __VERIFIER_error was called.";
		} else {
			std::cout << ", __VERIFIER_error was never called.";
		}
		std::cout << std::endl;
	} else if (wit_aut->isInViolationState() && !wit_aut->wasVerifierErrorCalled()) {
		std::cout << " #*# FAILED: __VERIFIER_error was never called, even though witness IS in violation state." << std::endl;
		exit_value = UNVALIDATED_VIOLATION;
	} else if (wit_aut->wasVerifierErrorCalled()) {
		std::cout << std::endl;
		if (wit_aut->isInViolationState()) {
			std::cout << "VALIDATED: The state: " << wit_aut->getCurrentState()->id << " has been reached. It is a violation state." << std::endl;
			exit_value = 0;
		} else {
#ifdef STRICT_VALIDATION
			std::cout << "FAILED: __VERIFIER_error was called and the state: " << wit_aut->getCurrentState()->id << " has been reached. However, it is NOT a violation state." << std::endl;
#else
			std::cout << "VALIDATED: The state: " << wit_aut->getCurrentState()->id << " has been reached. However, it is NOT a violation state." << std::endl;
#endif
			exit_value = PROGRAM_FINISHED_WITH_VIOLATION_THOUGH_NOT_IN_VIOLATION_STATE;
		}
	} else {
		std::cout <<  "UNKNOWN: A different error occurred, probably a parsing error or program exited." << std::endl;
		exit_value = RESULT_UNKNOWN;
	}
#ifdef VERBOSE
	double mem, cpu;
	process_resource_usage(mem, cpu);
	std::cerr << " ##VM_PEAK## " << mem << std::endl;
	std::cerr << " ##CPU_TIME## " << cpu << std::endl;
#endif
	std::cout  << "Return Code: " << exit_value << std::endl;
	return exit_value;
}


#include <sys/resource.h>

/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 * See: https://stackoverflow.com/questions/669438/how-to-get-memory-usage-at-runtime-using-c
 *
 * mem => MB
 * cpu => sec
 */
void process_resource_usage(double &mem, double &cpu) {
	/* BSD, Linux, and OSX -------------------------------------- */
	rusage rusage{};
	getrusage(RUSAGE_SELF, &rusage);
	mem = (rusage.ru_maxrss * 1000L) / (double) 1000000;
	cpu = rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec +
		  (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec) / (double) 1000000;
}
