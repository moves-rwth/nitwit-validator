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

int validate(const char *source_filename, const char *error_function_name, bool& error_function_was_called) {
	error_function_was_called = false;

	Picoc pc;
	PicocInitialise(&pc, 104857600); // stack size of 100 MiB
	pc.VerifierErrorFuncName = error_function_name;
	pc.VerifierErrorFunctionWasCalled = false;

	// the interpreter will jump here after finding a violation
	if (PicocPlatformSetExitPoint(&pc)) {
		cw_verbose("===============Finished=================\n");
		cw_verbose("Stopping the interpreter.\n");
		int ret = pc.PicocExitValue;
		error_function_was_called = pc.VerifierErrorFunctionWasCalled;
		PicocCleanup(&pc);
		return ret;
	}

	cw_verbose("============Start simulation============\n");
	// include all standard libraries and extern functions used by verifiers
	// like stdio, stdlib, special error, assume, nondet functions

#ifndef NO_HEADER_INCLUDE
	PicocIncludeAllSystemHeaders(&pc);
#endif

	bool error = true;
	std::string const sourceString = readFile(source_filename, error);
	if (error) {
		return 255;
	}
	char* source = static_cast<char*>(malloc(sourceString.length() + 1));
	strcpy(source, sourceString.c_str());
	int const sourceLength = static_cast<int>(strlen(source));

	PicocParse(&pc, source_filename, source, sourceLength, TRUE, FALSE, TRUE, TRUE, handleDebugBreakpoint);

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

	bool errorFunctionWasCalled = false;
	int exit_value = validate(argv[2], argv[3], errorFunctionWasCalled);
	errorFunctionWasCalled = errorFunctionWasCalled || wit_aut->wasVerifierErrorCalled();

	std::cout << "Witness in violation state: " << (wit_aut->isInViolationState() ? "yes" : "no") << std::endl;
	std::cout << "Error function \"" << argv[3] << "\" called during execution: " << (errorFunctionWasCalled ? "yes" : "no") << std::endl;

	// check whether we finished in a violation state and if __VERIFIER_error was called
	if ((!wit_aut->isInViolationState() || !errorFunctionWasCalled) &&
		(exit_value >= NO_WITNESS_CODE && exit_value <= ALREADY_DEFINED)) {
		cw_verbose("WitnessAutomaton finished in state %s, with error code %d.\n",
				   wit_aut->getCurrentState()->id.c_str(),
				   exit_value);
		std::cout << "FAILED: Wasn't able to validate the witness." << std::endl;

		// check whether we finished in a violation state
		if (wit_aut->isInViolationState()) {
			std::cout << " #*# Witness violation state reached";
			exit_value = UNVALIDATED_VIOLATION;
		} else {
			std::cout << " #*# Witness violation state NOT reached";
		}

		// check whether we finished in a state where __VERIFIER_error was called
		if (errorFunctionWasCalled) {
			std::cout << ", error function '" << argv[3] << "' was called.";
		} else {
			std::cout << ", error function '" << argv[3] << "' was never called.";
		}
		std::cout << std::endl;
	} else if (wit_aut->isInViolationState() && !errorFunctionWasCalled) {
		std::cout << " #*# FAILED: The error function '" << argv[3] << "' was never called, even though the witness IS in a violation state." << std::endl;
		exit_value = UNVALIDATED_VIOLATION;
	} else if (errorFunctionWasCalled) {
		std::cout << std::endl;
		if (wit_aut->isInViolationState()) {
			std::cout << "VALIDATED: The state '" << wit_aut->getCurrentState()->id << "' has been reached. The state is a violation state." << std::endl;
			exit_value = 0;
		} else {
#ifdef STRICT_VALIDATION
			std::cout << "FAILED: The error function '" << argv[3] << "' was called and the state '" << wit_aut->getCurrentState()->id << "' has been reached. However, this state is NOT a violation state. (strict mode)" << std::endl;
#else
			std::cout << "VALIDATED: The error function '" << argv[3] << "' was called and the state '" << wit_aut->getCurrentState()->id << "' has been reached. However, this state is NOT a violation state. (non-strict mode)" << std::endl;
#endif
			exit_value = PROGRAM_FINISHED_WITH_VIOLATION_THOUGH_NOT_IN_VIOLATION_STATE;
		}
	} else {
		std::cout <<  "UNKNOWN: An unhandled error/termination occurred, probably a parsing error or program exited." << std::endl;
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

#ifdef UNIX_HOST
#include <sys/resource.h>
#elif defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#define RUSAGE_SELF 0
#include <Winsock2.h>
struct rusage
{
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
};

// Adapted from https://github.com/postgres/postgres/blob/7559d8ebfa11d98728e816f6b655582ce41150f3/src/port/getrusage.c
int getrusage(int who, struct rusage* rusage) {
	FILETIME    starttime;
	FILETIME    exittime;
	FILETIME    kerneltime;
	FILETIME    usertime;
	ULARGE_INTEGER li;

	if (who != RUSAGE_SELF)
	{
		/* Only RUSAGE_SELF is supported in this implementation for now */
		errno = EINVAL;
		return -1;
	}

	if (rusage == (struct rusage*)NULL)
	{
		errno = EFAULT;
		return -1;
	}
	memset(rusage, 0, sizeof(struct rusage));
	if (GetProcessTimes(GetCurrentProcess(),
		&starttime, &exittime, &kerneltime, &usertime) == 0)
	{
		return -1;
	}

	/* Convert FILETIMEs (0.1 us) to struct timeval */
	memcpy(&li, &kerneltime, sizeof(FILETIME));
	li.QuadPart /= 10L;         /* Convert to microseconds */
	rusage->ru_stime.tv_sec = li.QuadPart / 1000000L;
	rusage->ru_stime.tv_usec = li.QuadPart % 1000000L;

	memcpy(&li, &usertime, sizeof(FILETIME));
	li.QuadPart /= 10L;         /* Convert to microseconds */
	rusage->ru_utime.tv_sec = li.QuadPart / 1000000L;
	rusage->ru_utime.tv_usec = li.QuadPart % 1000000L;

	return 0;
}
#else
#error This feature is not implemented for your architecture!
#endif

/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 * See: https://stackoverflow.com/questions/669438/how-to-get-memory-usage-at-runtime-using-c
 *
 * mem => MB
 * cpu => sec
 */
void process_resource_usage(double& mem, double& cpu) {
	/* BSD, Linux, and OSX -------------------------------------- */
	rusage rusage{};
	getrusage(RUSAGE_SELF, &rusage);
	mem = (rusage.ru_maxrss * 1000L) / (double) 1000000;
	cpu = rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec +
		  (rusage.ru_utime.tv_usec + rusage.ru_stime.tv_usec) / (double) 1000000;
}

