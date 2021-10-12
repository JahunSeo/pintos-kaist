#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/* Projects 2 */
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
// pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
struct file *find_file_by_fd(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	switch ((f->R.rax))
	{
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;

	case SYS_HALT:
		halt();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		break;

	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;

	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	case SYS_EXEC:
		if (exec(f->R.rdi) == -1)
			exit(-1);
		break;

	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;

	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;

	default:
		break;
	}

}

void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;    // kernal이 exit 하는 경우에도 이쪽 route 탄다.

	printf("%s: exit(%d)\n", thread_name(), status); // Process Termination Message
	thread_exit();
}


void is_useradd(const uint64_t *uaddr)
{
	struct thread *cur = thread_current();
	if (uaddr == NULL || !(is_user_vaddr(uaddr)) || pml4_get_page(cur->pml4, uaddr) == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

tid_t fork(const char *thread_name, struct intr_frame *f){

	return process_fork(thread_name, f);
}


// Run new 'executable' from current process
// Don't confuse with open! 'open' just opens up any file (txt, executable), 'exec' runs only executable
// Never returns on success. Returns -1 on fail.
int exec(const char *file)
{
	is_useradd(file);
	// *file address is located at f->R.rdi, when exec, 
	// cleanup process resource and context before file mapping 
	// that is the reason of page allocate.
	int size = strlen(file) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		exit(-1);
	strlcpy(fn_copy, file, size);

	if (process_exec(fn_copy) == -1){
		palloc_free_page(fn_copy);  /* fail, resource return */
		return -1;}

	// Not reachable
	NOT_REACHED();
	return 0;
}



/*filesys에 구현되어있는 내용임, 나중에 이해할 것.*/
// Creates a new file called file initially initial_size bytes in size.
// Returns true if successful, false otherwise
bool create(const char *file, unsigned initial_size)
{
	is_useradd(file);
	return filesys_create(file, initial_size);
}

// Deletes the file called 'file'. Returns true if successful, false otherwise.
bool remove(const char *file)
{
	is_useradd(file);
	return filesys_remove(file);
}

int open (const char *file)
{
	is_useradd(file);
	struct file *fileobj = filesys_open(file);

	if (fileobj == NULL){
		return -1;}
	
	int fd = add_file_to_fdt(fileobj);
	
	if (fd == -1)
		file_close(fileobj);

	return fd;
}


// Closes file descriptor fd. Ignores NULL file. Returns nothing.
void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return;

	remove_file_from_fdt(fd);

	file_close(fileobj);
}


// Returns the size, in bytes, of the file open as fd.
int filesize(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;
	return file_length(fileobj);
}

// Reads size bytes from the file open as fd into buffer.
// Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read
int read(int fd, void *buffer, unsigned size)
{
	is_useradd(buffer);
	int ret;
	struct thread *cur = thread_current();

	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;

	if (fileobj == STDIN)     
	{
		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++)
		{
			char c = input_getc();  
			*buf++ = c;
			if (c == '\0')
				break;
		}
		ret = i;
	}
	else if (fileobj == STDOUT)  /*write only device*/
	{
		ret = -1;
	}
	else
	{

		// lock_acquire(&file_rw_lock);   
		ret = file_read(fileobj, buffer, size);
		// lock_release(&file_rw_lock);
	}
	return ret;
}


// Writes size bytes from buffer to the open file fd.
// Returns the number of bytes actually written, or -1 if the file could not be written
int write(int fd, const void *buffer, unsigned size)
{
	is_useradd(buffer);
	int ret;

	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return -1;
		
	struct thread *cur = thread_current();

	if (fileobj == STDOUT)
	{
		putbuf(buffer, size);
		ret = size;
	}
	else if (fileobj == STDIN)
	{
		ret = -1;
	}
	else
	{
		// lock_acquire(&file_rw_lock);
		ret = file_write(fileobj, buffer, size);
		// lock_release(&file_rw_lock);
	}

	return ret;
}


// Changes the next byte to be read or written in open file fd to position,
// expressed in bytes from the beginning of the file (Thus, a position of 0 is the file's start).
void seek(int fd, unsigned position)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == STDIN || fileobj == STDOUT || fileobj == STDERR)
		return;
	fileobj->pos = position;

}



/**************************/
/*SYSCALL HELPER FUNCTION */
/**************************/

// Find open spot in current thread's fdt and put file in it. Returns the fd.
int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->FDT; // file descriptor table

	// Error - fdt full
	if (cur->fd_total >= 64)  /*상수로 정의할 것*/
		return -1;

	int n = 0;
	while (fdt[n])
		n++;
	fdt[n] = file;
	(cur->fd_total)++;

	if (cur->fd_max<n)
		cur->fd_max=n;

	return n;
}

// Project 2-4. File descriptor
// Check if given fd is valid, return cur->fdTable[fd]

struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid fd
	if (fd < 0 || fd >= 64)
		return NULL;

	return cur->FDT[fd]; // automatically returns NULL if empty
}

// Check for valid fd and do cur->fdTable[fd] = NULL. Returns nothing
void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	// Error - invalid fd
	if (fd < 0 || fd >= 64)
		return;

	cur->FDT[fd] = NULL;
	cur->fd_total--;
}
