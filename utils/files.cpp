//
// Created by jan on 3.4.19.
//

#include "files.hpp"
#include <cerrno>
#include <cstring>

char *readFile(const char *FileName) {
    struct stat FileInfo;
    char* ReadText;
    FILE* InFile;
    int BytesRead;

    int const statResult = stat(FileName, &FileInfo);
    if (statResult != 0) {
        printf("Cannot read file %s, stat failed with error '%s'.\n", FileName, strerror(errno));
        return nullptr;
    }

    ReadText = static_cast<char *>(malloc(FileInfo.st_size + 1));
    if (ReadText == nullptr) {
        printf("Failed to allocate memory for reading file, out of memory!\n");
    }

    InFile = fopen(FileName, "r");
    if (InFile == nullptr) {
        printf("Cannot read file %s, fopen() failed!\n", FileName);
    }

    BytesRead = fread(ReadText, 1, FileInfo.st_size, InFile);

    if (BytesRead == 0) {
        printf("Cannot read file %s, read 0 Bytes!\n", FileName);
    }

    ReadText[BytesRead] = '\0';
    fclose(InFile);

    return ReadText;
}
