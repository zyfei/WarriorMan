#ifndef WM_FILE_H
#define WM_FILE_H

#include "header.h"
#include "log.h"
#include "wm_string.h"

int wm_tmpfile(char *filename);
long wm_file_get_size(FILE *fp);
long wm_file_size(const char *filename);
wmString* wm_file_get_contents(const char *filename);
int wm_file_put_contents(const char *filename, const char *content, size_t length, bool append);

#endif	/* WM_FILE_H */
