/* stdlib.h library for large systems - small embedded systems use clibrary.c instead */
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

static int ZeroValue = 0;

void UnistdAccess(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = access((const char*)Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void UnistdAlarm(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = alarm(Param[0]->Val->Integer);
}

void UnistdChdir(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = chdir((const char*)Param[0]->Val->Pointer);
}

void UnistdChroot(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = chroot((const char*)Param[0]->Val->Pointer);
}

void UnistdChown(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = chown((const char*)Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void UnistdClose(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = close(Param[0]->Val->Integer);
}

void UnistdConfstr(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = confstr(Param[0]->Val->Integer, (char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void UnistdCtermid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ctermid((char*)Param[0]->Val->Pointer);
}

#if 0
void UnistdCuserid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = cuserid(Param[0]->Val->Pointer);
}
#endif

void UnistdDup(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = dup(Param[0]->Val->Integer);
}

void UnistdDup2(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = dup2(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void Unistd_Exit(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    _exit(Param[0]->Val->Integer);
}

void UnistdFchown(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fchown(Param[0]->Val->Integer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void UnistdFchdir(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fchdir(Param[0]->Val->Integer);
}

void UnistdFdatasync(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
#ifndef F_FULLSYNC
    ReturnValue->Val->Integer = fdatasync(Param[0]->Val->Integer);
#else
    /* Mac OS X equivalent */
    ReturnValue->Val->Integer = fcntl(Param[0]->Val->Integer, F_FULLFSYNC);
#endif
}

void UnistdFork(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fork();
}

void UnistdFpathconf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fpathconf(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdFsync(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = fsync(Param[0]->Val->Integer);
}

void UnistdFtruncate(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ftruncate(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdGetcwd(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getcwd((char*)Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void UnistdGetdtablesize(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getdtablesize();
}

void UnistdGetegid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getegid();
}

void UnistdGeteuid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = geteuid();
}

void UnistdGetgid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getgid();
}

void UnistdGethostid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = gethostid();
}

void UnistdGetlogin(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getlogin();
}

void UnistdGetlogin_r(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getlogin_r((char*)Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void UnistdGetpagesize(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getpagesize();
}

void UnistdGetpass(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getpass((const char*)Param[0]->Val->Pointer);
}

#if 0
void UnistdGetpgid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getpgid(Param[0]->Val->Integer);
}
#endif

void UnistdGetpgrp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getpgrp();
}

void UnistdGetpid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getpid();
}

void UnistdGetppid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getppid();
}

#if 0
void UnistdGetsid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getsid(Param[0]->Val->Integer);
}
#endif

void UnistdGetuid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = getuid();
}

void UnistdGetwd(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getcwd((char*)Param[0]->Val->Pointer, PATH_MAX);
}

void UnistdIsatty(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = isatty(Param[0]->Val->Integer);
}

void UnistdLchown(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = lchown((const char*)Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void UnistdLink(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = link((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void UnistdLockf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = lockf(Param[0]->Val->Integer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void UnistdLseek(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = lseek(Param[0]->Val->Integer, Param[1]->Val->Integer, Param[2]->Val->Integer);
}

void UnistdNice(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = nice(Param[0]->Val->Integer);
}

void UnistdPathconf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = pathconf((const char*)Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void UnistdPause(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = pause();
}

#if 0
void UnistdPread(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = pread(Param[0]->Val->Integer, Param[1]->Val->Pointer, Param[2]->Val->Integer, Param[3]->Val->Integer);
}

void UnistdPwrite(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = pwrite(Param[0]->Val->Integer, Param[1]->Val->Pointer, Param[2]->Val->Integer, Param[3]->Val->Integer);
}
#endif

void UnistdRead(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = read(Param[0]->Val->Integer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void UnistdReadlink(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = readlink((const char*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void UnistdRmdir(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = rmdir((const char*)Param[0]->Val->Pointer);
}

void UnistdSbrk(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = sbrk(Param[0]->Val->Integer);
}

void UnistdSetgid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setgid(Param[0]->Val->Integer);
}

void UnistdSetpgid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setpgid(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdSetpgrp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setpgrp();
}

void UnistdSetregid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setregid(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdSetreuid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setreuid(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdSetsid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setsid();
}

void UnistdSetuid(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = setuid(Param[0]->Val->Integer);
}

void UnistdSleep(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = sleep(Param[0]->Val->Integer);
}

#if 0
void UnistdSwab(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = swab(Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}
#endif

void UnistdSymlink(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = symlink((const char*)Param[0]->Val->Pointer, (const char*)Param[1]->Val->Pointer);
}

void UnistdSync(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    sync();
}

void UnistdSysconf(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = sysconf(Param[0]->Val->Integer);
}

void UnistdTcgetpgrp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = tcgetpgrp(Param[0]->Val->Integer);
}

void UnistdTcsetpgrp(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = tcsetpgrp(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdTruncate(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = truncate((const char*)Param[0]->Val->Pointer, Param[1]->Val->Integer);
}

void UnistdTtyname(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = ttyname(Param[0]->Val->Integer);
}

void UnistdTtyname_r(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ttyname_r(Param[0]->Val->Integer,(char*) Param[1]->Val->Pointer, Param[2]->Val->Integer);
}

void UnistdUalarm(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ualarm(Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void UnistdUnlink(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = unlink((const char*)Param[0]->Val->Pointer);
}

void UnistdUsleep(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = usleep(Param[0]->Val->Integer);
}

void UnistdVfork(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = vfork();
}

void UnistdWrite(struct ParseState *Parser, Value *ReturnValue, Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = write(Param[0]->Val->Integer, Param[1]->Val->Pointer, Param[2]->Val->Integer);
}


/* handy structure definitions */
const char UnistdDefs[] = "\
typedef unsigned int uid_t; \
typedef unsigned int gid_t; \
typedef int pid_t; \
typedef int off_t; \
typedef unsigned int size_t; \
typedef int ssize_t; \
typedef unsigned int useconds_t;\
typedef int intptr_t;\
";

/* all unistd.h functions */
struct LibraryFunction UnistdFunctions[] =
{
    { UnistdAccess,        "int access(char *, int);" },
    { UnistdAlarm,         "unsigned int alarm(unsigned int);" },
/*    { UnistdBrk,           "int brk(void *);" }, */
    { UnistdChdir,         "int chdir(char *);" },
    { UnistdChroot,        "int chroot(char *);" },
    { UnistdChown,         "int chown(char *, uid_t, gid_t);" },
    { UnistdClose,         "int close(int);" },
    { UnistdConfstr,       "size_t confstr(int, char *, size_t);" },
    { UnistdCtermid,       "char *ctermid(char *);" },
/*    { UnistdCuserid,       "char *cuserid(char *);" }, */
    { UnistdDup,           "int dup(int);" },
    { UnistdDup2,          "int dup2(int, int);" },
/*     { UnistdEncrypt,       "void encrypt(char[64], int);" }, */
/*    { UnistdExecl,         "int execl(char *, char *, ...);" }, */
/*    { UnistdExecle,        "int execle(char *, char *, ...);" }, */
/*    { UnistdExeclp,        "int execlp(char *, char *, ...);" }, */
/*    { UnistdExecv,         "int execv(char *, char *[]);" }, */
/*    { UnistdExecve,        "int execve(char *, char *[], char *[]);" }, */
/*    { UnistdExecvp,        "int execvp(char *, char *[]);" }, */
    { Unistd_Exit,         "void _exit(int);" },
    { UnistdFchown,        "int fchown(int, uid_t, gid_t);" },
    { UnistdFchdir,        "int fchdir(int);" },
    { UnistdFdatasync,     "int fdatasync(int);" },
    { UnistdFork,          "pid_t fork(void);" },
    { UnistdFpathconf,     "long fpathconf(int, int);" },
    { UnistdFsync,         "int fsync(int);" },
    { UnistdFtruncate,     "int ftruncate(int, off_t);" },
    { UnistdGetcwd,        "char *getcwd(char *, size_t);" },
    { UnistdGetdtablesize, "int getdtablesize(void);" },
    { UnistdGetegid,       "gid_t getegid(void);" },
    { UnistdGeteuid,       "uid_t geteuid(void);" },
    { UnistdGetgid,        "gid_t getgid(void);" },
/*    { UnistdGetgroups,     "int getgroups(int, gid_t []);" }, */
    { UnistdGethostid,     "long gethostid(void);" },
    { UnistdGetlogin,      "char *getlogin(void);" },
    { UnistdGetlogin_r,    "int getlogin_r(char *, size_t);" },
/*    { UnistdGetopt,        "int getopt(int, char * [], char *);" }, */
    { UnistdGetpagesize,   "int getpagesize(void);" },
    { UnistdGetpass,       "char *getpass(char *);" },
/*    { UnistdGetpgid,       "pid_t getpgid(pid_t);" }, */
    { UnistdGetpgrp,       "pid_t getpgrp(void);" },
    { UnistdGetpid,        "pid_t getpid(void);" },
    { UnistdGetppid,       "pid_t getppid(void);" },
/*    { UnistdGetsid,        "pid_t getsid(pid_t);" }, */
    { UnistdGetuid,        "uid_t getuid(void);" },
    { UnistdGetwd,         "char *getwd(char *);" },
    { UnistdIsatty,        "int isatty(int);" },
    { UnistdLchown,        "int lchown(char *, uid_t, gid_t);" },
    { UnistdLink,          "int link(char *, char *);" },
    { UnistdLockf,         "int lockf(int, int, off_t);" },
    { UnistdLseek,         "off_t lseek(int, off_t, int);" },
    { UnistdNice,          "int nice(int);" },
    { UnistdPathconf,      "long pathconf(char *, int);" },
    { UnistdPause,         "int pause(void);" },
/*    { UnistdPipe,          "int pipe(int [2]);" }, */
/*    { UnistdPread,         "ssize_t pread(int, void *, size_t, off_t);" }, */
/*    { UnistdPthread_atfork,"int pthread_atfork(void (*)(void), void (*)(void), void(*)(void));" }, */
/*    { UnistdPwrite,        "ssize_t pwrite(int, void *, size_t, off_t);" }, */
    { UnistdRead,          "ssize_t read(int, void *, size_t);" },
    { UnistdReadlink,      "int readlink(char *, char *, size_t);" },
    { UnistdRmdir,         "int rmdir(char *);" },
    { UnistdSbrk,          "void *sbrk(intptr_t);" },
    { UnistdSetgid,        "int setgid(gid_t);" },
    { UnistdSetpgid,       "int setpgid(pid_t, pid_t);" },
    { UnistdSetpgrp,       "pid_t setpgrp(void);" },
    { UnistdSetregid,      "int setregid(gid_t, gid_t);" },
    { UnistdSetreuid,      "int setreuid(uid_t, uid_t);" },
    { UnistdSetsid,        "pid_t setsid(void);" },
    { UnistdSetuid,        "int setuid(uid_t);" },
    { UnistdSleep,         "unsigned int sleep(unsigned int);" },
/*    { UnistdSwab,          "void swab(void *, void *, ssize_t);" }, */
    { UnistdSymlink,       "int symlink(char *, char *);" },
    { UnistdSync,          "void sync(void);" },
    { UnistdSysconf,       "long sysconf(int);" },
    { UnistdTcgetpgrp,     "pid_t tcgetpgrp(int);" },
    { UnistdTcsetpgrp,     "int tcsetpgrp(int, pid_t);" },
    { UnistdTruncate,      "int truncate(char *, off_t);" },
    { UnistdTtyname,       "char *ttyname(int);" },
    { UnistdTtyname_r,     "int ttyname_r(int, char *, size_t);" },
    { UnistdUalarm,        "useconds_t ualarm(useconds_t, useconds_t);" },
    { UnistdUnlink,        "int unlink(char *);" },
    { UnistdUsleep,        "int usleep(useconds_t);" },
    { UnistdVfork,         "pid_t vfork(void);" },
    { UnistdWrite,         "ssize_t write(int, void *, size_t);" },
    { nullptr,                 nullptr }
};

/* creates various system-dependent definitions */
void UnistdSetupFunc(Picoc *pc)
{
    /* define nullptr */
    if (!VariableDefined(pc, TableStrRegister(pc, "nullptr")))
        VariableDefinePlatformVar(pc, nullptr, "nullptr", &pc->IntType, (union AnyValue *)&ZeroValue, FALSE);

    /* define optarg and friends */
    VariableDefinePlatformVar(pc, nullptr, "optarg", pc->CharPtrType, (union AnyValue *)&optarg, TRUE);
    VariableDefinePlatformVar(pc, nullptr, "optind", &pc->IntType, (union AnyValue *)&optind, TRUE);
    VariableDefinePlatformVar(pc, nullptr, "opterr", &pc->IntType, (union AnyValue *)&opterr, TRUE);
    VariableDefinePlatformVar(pc, nullptr, "optopt", &pc->IntType, (union AnyValue *)&optopt, TRUE);
}

#endif /* !BUILTIN_MINI_STDLIB */

