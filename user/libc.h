#ifndef __LIBC_H
#define __LIBC_H

#include <stddef.h>
#include <stdint.h>


// write n bytes from x to the file descriptor fd
int write( int fd, void* x, size_t n );

void writeStr(char* str);

void fork();
void exit();
char* itoa(int i, char b[]);
#endif
