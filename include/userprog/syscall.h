#include "threads/thread.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void check_address(const char *addr);
int process_add_file (struct file *file);
struct file *process_get_file (int fd);
void process_close_file (int fd);

void _halt (void);
void _exit (int status);
int _wait (tid_t pid);
tid_t _fork (const char* thread_name, struct intr_frame *if_);
int _exec (const char *file);

bool _create (const char *file_name, unsigned initial_size);
bool _remove (const char *file_name);
int _open (const char *file_name);
int _read (int fd, void *buffer, unsigned size);
int _filesize (int fd);
int _write (int fd, const void *buffer, unsigned size);
void _close (int fd);
void _seek (int fd, unsigned position);
unsigned _tell (int fd);
void * _mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void _munmap(void *addr);

struct lock filesys_lock; // use global lock to avoid race condition on file

#endif /* userprog/syscall.h */
