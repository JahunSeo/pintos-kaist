#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "include/filesys/filesys.h"
// added
#include "userprog/process.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

void check_address(const char *addr);

void _halt (void);
void _exit (int status);
int _wait (tid_t pid);
tid_t _fork (const char* thread_name, struct intr_frame *if_);
int _exec (const char *file);
int _write (int fd, const void *buffer, unsigned size);
bool _create (const char *file, unsigned initial_size);

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
	// TODO: Your implementation goes here.
	// printf("[syscall_handler] start : %lld, (%lld, %lld, %lld, %lld, %lld, %lld)\n", 
		// f->R.rax, f->R.rdi,f->R.rsi,f->R.rdx,f->R.r10,f->R.r8,f->R.r9);
	switch(f->R.rax) {
		case SYS_HALT:                   /* Halt the operating system. */
			_halt ();
			break;

		case SYS_EXIT:				  	 /* Terminate this process. */
			_exit (f->R.rdi);
			break;

		case SYS_FORK:                   /* Clone current process. */
			f->R.rax = _fork ((char *) f->R.rdi, f);
			break;

		case SYS_EXEC:                   /* Switch current process. */
			f->R.rax = _exec ((char *) f->R.rdi);
			break;

		case SYS_WAIT:                   /* Wait for a child process to die. */
			f->R.rax = _wait((tid_t) f->R.rdi);
			break;

		case SYS_CREATE:                 /* Create a file. */
			f->R.rax = _create((char *) f->R.rdi, f->R.rsi);
			break;

		case SYS_REMOVE:                 /* Delete a file. */
			// printf("  SYS_REMOVE called!\n");
			break;

		case SYS_OPEN:                   /* Open a file. */
			// printf("  SYS_OPEN called!\n");
			break;

		case SYS_FILESIZE:               /* Obtain a file's size. */
			// printf("  SYS_FILESIZE called!\n");
			break;

		case SYS_READ:                   /* Read from a file. */
			// printf("  SYS_READ called!\n");
			break;

		case SYS_WRITE:                  /* Write to a file. */
			// printf("  SYS_WRITE called!\n");
			f->R.rax = _write(f->R.rdi, (char *) f->R.rsi, f->R.rdx);
			// power_off ();
			break;

		case SYS_SEEK:                   /* Change position in a file. */
			// printf("  SYS_HALT called!\n");
			break;

		case SYS_TELL:                   /* Report current position in a file. */
			// printf("  SYS_TELL called!\n");
			break;

		case SYS_CLOSE:                  /* Close a file. */
			// printf("  SYS_CLOSE called!\n");
			break;

		default:
			printf("  DEFAULT do nothing..\n");
	}


	// printf("[syscall_handler] end   : %lld \n", f->R.rax);
}

/* system call에 인자로 들어온 주소값의 유효성을 검사 
	- a null pointer, 
	- a pointer to unmapped virtual memory, 
	- or a pointer to kernel virtual address space (above KERN_BASE)
*/
void check_address(const char *uaddr) {
	struct thread *curr = thread_current();
	if (uaddr == NULL 
		|| !is_user_vaddr(uaddr) 
		|| pml4_get_page(curr->pml4, uaddr) == NULL) {
			_exit(-1);
		}
}

/* 파일 객체의 주소값을 FDT에 추가 */
int process_add_file (struct file *file) {
	/* file의 주소값이 유효한지 확인 */
	check_address(file);
	/* 현재 thread의 fdt와 next_fd 확인
		- FDT에 추가 가능한 파일 개수가 가득 찼는지 확인
	*/
	struct thread *curr = thread_current();
	if (curr->next_fd >= FDT_ENTRY_MAX) {
		// _exit(-1);	
		return TID_ERROR;	
	}
	/* 현재 thread의 fdt에 새로운 파일 추가 */
	int fd = curr->next_fd;
	curr->fdt[fd] = file;
	/* 다음 빈 칸을 찾아 next_fd로 설정
		- TODO: 파일이 closed 되었을 때, 그 빈 칸을 활용할 수 있도록 변경
	 */
	curr->next_fd++;
	return fd;
}

struct file *process_get_file (int fd) {
	/* fd 값이 유효한지 확인 */
	if (fd < 0 || fd >= FDT_ENTRY_MAX) {
		return TID_ERROR;
	}
	/* 현재 thread의 fdt 확인 */
	struct thread *curr = thread_current();
	struct file *file;	
	if (file = curr->fdt[fd] == NULL) {
		return TID_ERROR;
	}
	return file;
}


void _halt (void) {
	power_off();
}

void _exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;	
	/* process가 종료되었다는 문장 출력 */
	printf("%s: exit(%d)\n", thread_name(), curr->exit_status);
	thread_exit();
}

tid_t _fork (const char* thread_name, struct intr_frame *if_) {
	check_address(thread_name);
	return process_fork(thread_name, if_);
}

int _exec (const char *file_name) {
	/* 잘못된 주소값인지 확인 */
	check_address(file_name);

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load().
	 	- file_name을 그대로 process_exec의 인자로 넘기면 파일을 load하지 못하고 오류 발생
		- process_exec에서 파일 load 전에 process_cleanup()으로 현재 context를 지워버리기 때문
		- process_exec에서 파일 load가 잘 되기 위해서 실행할 파일의 이름을 palloc으로 생성된 별도의 페이지에 담아주어야 함
		- (process_exec에서도 파일 load 이후 palloc_free_page (file_name)로 fn_copy가 담겼던 페이지를 처리해준다는 점 확인)
	*/
	char *fn_copy;
	fn_copy = palloc_get_page (PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;
	// printf("[_exec] before copy %d, %s // %p\n", strlen(file_name), file_name, fn_copy);
	strlcpy (fn_copy, file_name, strlen(file_name) + 1);
	// printf("[_exec] after  copy %d, %s\n", strlen(fn_copy), fn_copy);

	/* 프로그램 실행 */
	if (process_exec(fn_copy) < 0)
		_exit(-1);
		// return -1;
	NOT_REACHED();
}

int _wait (tid_t pid) {
	// printf("[_wait] pid %d\n", pid);
	return process_wait(pid);
}

bool _create (const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

int _write (int fd UNUSED, const void *buffer, unsigned size) {
	// temporary code to pass args related test case
	putbuf(buffer, size);
	return size;
}
