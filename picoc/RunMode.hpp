
#ifndef NITWIT_PICOC_RUNMODE_H_
#define NITWIT_PICOC_RUNMODE_H_

/* whether we're running or skipping code */
enum class RunMode {
    RunModeRun,                 /* we're running code as we parse it */
    RunModeSkip,                /* skipping code, not running */
    RunModeReturn,              /* returning from a function */
    RunModeCaseSearch,          /* searching for a case label */
    RunModeBreak,               /* breaking out of a switch/while/do */
    RunModeContinue,            /* as above but repeat the loop */
    RunModeGoto                 /* searching for a goto label */
};

#endif
