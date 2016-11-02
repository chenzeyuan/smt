#ifndef NET_MMT_MMTP_GETFILE_H_
#define NET_MMT_MMTP_GETFILE_H_

#include <dirent.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int getFileNameArray(const char *path, u_int32_t* fileCount,char * **fileNameList);
char ** getFileNameArray2(const char *path, u_int32_t* fileCount);
void get_full_path(const char *path ,char * filename, char * fullpath );
char *get_full_path2(const char *path ,char * filename);
//char * ReadFile(char * path, int *length);
int ReadFile(char * path, char ** fdata,u_int16_t *length);
char* strcatex(char* str1, char* str2);

#endif
