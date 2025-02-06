#ifndef SERVER_FUNCTIONS_H
#define SERVER_FUNCTIONS_H
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>
#include "api.h"

int msSleep(long time);

int mkdir_p(const char *path);

int openConnection(const char* nome_sock, int msec, const struct timespec abstime);

int closeConnection(const char* nome_sock);

int openFile(const char* path, int flags);

int readFile(const char* path, void** buf, size_t* size);

int readNFile(int N, const char* dir);

int writeFile(const char* path, const char* dir);

int appendToFile(const char* path, void* buf, size_t size, const char* dir);

int lockFile(const char* path);

int unlockFile(const char* path);

int closeFile(const char* path);

int removeFile(const char* path);

int readNFiles(int N, const char* dir);

size_t get_last_w_size ();

size_t get_last_rN_size ();

#endif
