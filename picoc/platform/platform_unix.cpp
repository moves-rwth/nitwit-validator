#include "../picoc.hpp"
#include "../interpreter.hpp"

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

/* mark where to end the program for platforms which require this */
jmp_buf PicocExitBuf;

#ifndef NO_DEBUGGER
#include <signal.h>

//Picoc *break_pc = nullptr;

//static void BreakHandler(int Signal)
//{
//    break_pc->DebugManualBreak = TRUE;
//}

void PlatformInit(Picoc *pc)
{
    /* capture the break signal and pass it to the debugger */
//    break_pc = pc;
//    signal(SIGINT, BreakHandler);
}
#else
void PlatformInit(Picoc *pc)
{
}
#endif

void PlatformCleanup(Picoc *pc)
{
}

/* get a line of interactive input */
char *PlatformGetLine(char *Buf, int MaxLen, const char *Prompt)
{
#ifdef USE_READLINE
    if (Prompt != nullptr)
    {
        /* use GNU readline to read the line */
        char *InLine = readline(Prompt);
        if (InLine == nullptr)
            return nullptr;
    
        Buf[MaxLen-1] = '\0';
        strncpy(Buf, InLine, MaxLen-2);
        strncat(Buf, "\n", MaxLen-2);
        
        if (InLine[0] != '\0')
            add_history(InLine);
            
        free(InLine);
        return Buf;
    }
#endif

    if (Prompt != nullptr)
        printf("%s", Prompt);
        
    fflush(stdout);
    return fgets(Buf, MaxLen, stdin);
}

/* get a character of interactive input */
int PlatformGetCharacter()
{
    fflush(stdout);
    return getchar();
}

/* write a character to the console */
void PlatformPutc(unsigned char OutCh, union OutputStreamInfo *Stream)
{
    putchar(OutCh);
}

/* read a file into memory */
char *PlatformReadFile(Picoc *pc, const char *FileName)
{
    struct stat FileInfo;
    char *ReadText;
    FILE *InFile;
    int BytesRead;
    char *p;
    
    if (stat(FileName, &FileInfo))
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);
    
    ReadText = static_cast<char *>(malloc(FileInfo.st_size + 1));
    if (ReadText == nullptr)
        ProgramFailNoParserWithExitCode(pc, 251, "Out of memory");
        
    InFile = fopen(FileName, "r");
    if (InFile == nullptr)
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);
    
    BytesRead = fread(ReadText, 1, FileInfo.st_size, InFile);
    if (BytesRead == 0)
        ProgramFailNoParser(pc, "can't read file %s\n", FileName);

    ReadText[BytesRead] = '\0';
    fclose(InFile);
    
    if ((ReadText[0] == '#') && (ReadText[1] == '!'))
    {
        for (p = ReadText; (*p != '\r') && (*p != '\n'); ++p)
        {
            *p = ' ';
        }
    }
    
    return ReadText;    
}

/* read and scan a file for definitions */
void PicocPlatformScanFile(Picoc *pc, const char *FileName)
{
    char *SourceStr = PlatformReadFile(pc, FileName);

    /* ignore "#!/path/to/picoc" .. by replacing the "#!" with "//" */
    if (SourceStr != nullptr && SourceStr[0] == '#' && SourceStr[1] == '!')
    { 
        SourceStr[0] = '/'; 
        SourceStr[1] = '/'; 
    }

    nitwit::parse::PicocParse(pc, FileName, SourceStr, strlen(SourceStr), TRUE, FALSE, TRUE, TRUE, nullptr);
}

/* exit the program */
void PlatformExit(Picoc *pc, int RetVal)
{
    pc->PicocExitValue = RetVal;
    if (pc->IsInAssumptionMode)
        longjmp(pc->AssumptionPicocExitBuf, 1);
    else
        longjmp(pc->PicocExitBuf, 1);
}


/* exit the program */
void AssumptionPlatformExit(Picoc *pc, int RetVal)
{
    pc->PicocExitValue = RetVal;
    longjmp(pc->AssumptionPicocExitBuf, 1);
}

