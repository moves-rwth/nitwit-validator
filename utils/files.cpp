//
// Created by jan on 3.4.19.
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

char *readFile(const char *FileName)
{
    struct stat FileInfo;
    char *ReadText;
    FILE *InFile;
    int BytesRead;

    if (stat(FileName, &FileInfo))
        printf("Cannot read file %s\n", FileName);

    ReadText = static_cast<char *>(malloc(FileInfo.st_size + 1));
    if (ReadText == nullptr)
        printf("Out of memory!\n");

    InFile = fopen(FileName, "r");
    if (InFile == nullptr)
        printf("Cannot read file %s\n", FileName);

    BytesRead = fread(ReadText, 1, FileInfo.st_size, InFile);
    if (BytesRead == 0)
        printf("Cannot read file %s\n", FileName);

    ReadText[BytesRead] = '\0';
    fclose(InFile);

    return ReadText;
}
