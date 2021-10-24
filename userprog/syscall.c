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
#include "devices/input.h"
#include <string.h>
#include "filesys/file.h"

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
	/* initiate filesys_lock */
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// printf("[syscall_handler] start : %lld, (%lld, %lld, %lld, %lld, %lld, %lld)\n", 
		// f->R.rax, f->R.rdi,f->R.rsi,f->R.rdx,f->R.r10,f->R.r8,f->R.r9);

	// user program에 의해 syscall이 발생했을 때 user stack pointer의 위치를 저장해 둠
	// - stack growth 필요 여부를 판단하기 위해 사용됨
	thread_current()->last_usr_rsp = f->rsp;
	// printf("[syscall_handler] last_urp_rsp: %p\n", thread_current()->last_usr_rsp);


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
			f->R.rax = _remove((char *) f->R.rdi);
			break;

		case SYS_OPEN:                   /* Open a file. */
			f->R.rax = _open((char *) f->R.rdi);
			break;

		case SYS_FILESIZE:               /* Obtain a file's size. */
			f->R.rax = _filesize(f->R.rdi);
			break;

		case SYS_READ:                   /* Read from a file. */
			f->R.rax = _read(f->R.rdi, (char *) f->R.rsi, f->R.rdx);
			break;

		case SYS_WRITE:                  /* Write to a file. */
			f->R.rax = _write(f->R.rdi, (char *) f->R.rsi, f->R.rdx);
			break;

		case SYS_SEEK:                   /* Change position in a file. */
			_seek(f->R.rdi, f->R.rsi);
			break;

		case SYS_TELL:                   /* Report current position in a file. */
			f->R.rax = _tell(f->R.rdi);
			break;

		case SYS_CLOSE:                  /* Close a file. */
			_close(f->R.rdi);
			break;

		case SYS_MMAP:					 /*  */
			f->R.rax = _mmap((char *) f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;

		case SYS_MUNMAP:				 /*  */
			_munmap((char *) f->R.rdi);
			break;

		default:
			printf("  DEFAULT do nothing..\n");
			_exit(TID_ERROR);
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
	// printf("  [check_address] %p, %d, %p\n", uaddr, is_user_vaddr(uaddr), pml4_get_page(curr->pml4, uaddr));
	if (uaddr == NULL 
		|| !is_user_vaddr(uaddr) 
		|| pml4_get_page(curr->pml4, uaddr) == NULL) {
			_exit(-1);
		}
}

/* 파일 객체의 주소값을 FDT에 추가하기 */
int process_add_file (struct file *file) {
	/* 현재 thread의 fdt와 next_fd 확인
		- FDT에 추가 가능한 파일 개수가 가득 찼는지 확인
	*/
	struct thread *curr = thread_current();
	if (curr->next_fd >= FDT_ENTRY_MAX) {
		return TID_ERROR;	
	}
	/* 현재 thread의 fdt에 새로운 파일 추가 */
	int fd = curr->next_fd;
	curr->fdt[fd] = file;
	/* max_fd 업데이트: 새로운 파일의 fd값과 비교하여 max_fd값 설정 */
	if (fd > curr->max_fd) {
		curr->max_fd = fd;
	}
	/* next_fd 업데이트: 다음 빈 칸을 찾아 next_fd로 설정
		- process_close_file에서의 처리에 의해 기존 next_fd 보다 작은 fd에는 모두 채워져 있으므로
		- 한 칸 씩 위로 올라가며 빈 칸을 찾음
	 */
	while (curr->next_fd < FDT_ENTRY_MAX && curr->fdt[curr->next_fd]) {
		curr->next_fd++;
	}

	return fd;
}

/* FDT에서 fd값으로 파일의 주소값 가져오기 */
struct file *process_get_file (int fd) {
	/* fd 값이 유효한지 확인 */
	if (fd < 0 || fd >= FDT_ENTRY_MAX) {
		return NULL;
	}
	/* 현재 thread의 fdt에서 fd 위치에 값이 있는지 확인 */
	struct file *file = thread_current()->fdt[fd];
	if (file == NULL) {
		return NULL;
	}
	return file;
}

/* FDT에서 fd값으로 파일의 주소값 제거하기 */
void process_close_file (int fd) {
	/* fd 값이 유효한지 확인
		- 일단 stdin, stdout은 삭제 불가 처리
	 */
	if (fd >= FDT_ENTRY_MAX) {
		return;
	}
	/* sdt에서 값 제거 */
	struct thread *curr = thread_current();
	curr->fdt[fd] = NULL;
	/* stdin과 stdout를 삭제하더라도 next_fd가 0,1 자리에는 들어오지 못하도록 비워둠 (is it right?) */
	if (fd < 2) {
		return;
	}
	/* next_fd 업데이트
		- next_fd를 전체 비어있는 fd 중 가장 작은 값으로 유지
		- 즉, next_fd 아래의 값은 모두 채워져 있음
	 */
	if (fd < curr->next_fd) {
		curr->next_fd = fd;
	}
	/* max_fd 업데이트: 맨 마지막 파일을 삭제한 경우 */
	if (fd >= curr->max_fd) {
		curr->max_fd--;
	}
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
	// printf("[_fork] thread_name %s, %p\n", thread_name, thread_name);
	check_address(thread_name);
	return process_fork(thread_name, if_);
}

int _exec (const char *file_name) {
	/* 잘못된 주소값인지 확인 */
	// printf("[_exec] file_name %s, %p\n", file_name, file_name);
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

bool _create (const char *file_name, unsigned initial_size) {
	check_address(file_name);
	return filesys_create(file_name, initial_size);
}

bool _remove (const char *file_name) {
	check_address(file_name);
	return filesys_remove(file_name);
}

int _open (const char *file_name) {
	// printf("[_open] file_name %s, %p\n", file_name, file_name);
	check_address(file_name);
	struct file* file;
	lock_acquire(&filesys_lock);
	file = filesys_open(file_name);
	lock_release(&filesys_lock);

	if (file == NULL) {
		return TID_ERROR;
	}
	int fd = process_add_file(file);

	if (fd == TID_ERROR) {
		lock_acquire(&filesys_lock);
		file_close (file);
		lock_release(&filesys_lock);
	}
	return fd;
}

int _read (int fd, void *buffer, unsigned size) {
	/* buffer로 들어온 주소값이 유효한지 확인 */
	// printf("[_read] buffer %p\n", buffer);
	check_address(buffer);
	/* 현재 thread의 FDT에서 fd 값이 유효한지 확인 */
	struct file *file = process_get_file(fd);
	if (file == NULL) {
		return TID_ERROR;
	}
	/* readcnt 초기화 */
	int readcnt = 0;
	/* fd가 STDIN인 경우 처리 */
	if (fd == 0) {
		char key;
		char *buf = buffer;
		for (int i=0; i<size; i++) {
			key = input_getc();
			*buf++ = key;
			readcnt++;
			if (key == '\0') {
				break;
			}
		}
		return readcnt;
	}
	/* fd가 STDOUT인 경우 처리 */ 
	else if (fd == 1) {
		return TID_ERROR;
	}
	/* 그 외의 파일 처리 */
	lock_acquire(&filesys_lock);
	readcnt = file_read(file, buffer, size);
	lock_release(&filesys_lock);
	return readcnt;
}

int _filesize (int fd) {
	struct file* file = process_get_file(fd);
	if (file == NULL) {
		return TID_ERROR;
	}
	return file_length(file);
}


int _write (int fd, const void *buffer, unsigned size) {
	/* buffer로 들어온 주소값이 유효한지 확인 */
	// printf("[_write] buffer %p\n", buffer);
	check_address(buffer);
	/* 현재 thread의 FDT에서 fd 값이 유효한지 확인 */
	struct file *file = process_get_file(fd);
	if (file == NULL) {
		return TID_ERROR;
	}
	/* writecnt 초기화 */
	int writecnt = 0;
	/* fd가 STDIN인 경우 처리 */
	if (fd == 0) {
		return TID_ERROR;
	}
	/* fd가 STDOUT인 경우 처리 */ 
	else if (fd == 1) {
		putbuf(buffer, size);
		return size;
	}
	/* 그 외의 파일 처리 */
	lock_acquire(&filesys_lock);
	writecnt = file_write(file, buffer, size);
	lock_release(&filesys_lock);
	return writecnt;
}

void _close (int fd) {
	if (fd >= 2) {
		struct file *file = process_get_file(fd);
		lock_acquire(&filesys_lock);
		file_close(file);
		lock_release(&filesys_lock);
	}
	process_close_file(fd);
}

void _seek (int fd, unsigned position) {
	if (fd < 2) return;
	struct file *file = process_get_file(fd);
	if (file == NULL) return;
	lock_acquire(&filesys_lock);
	file_seek(file, position);
	lock_release(&filesys_lock);
}

unsigned _tell (int fd) {
	if (fd < 2) return;
	struct file *file = process_get_file(fd);
	if (file == NULL) return;
	unsigned position;
	lock_acquire(&filesys_lock);
	position = file_tell(file);
	lock_release(&filesys_lock);
	return position;
}

void * _mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	// printf("[_mmap] %p, %ld, %d, %d, %d\n", addr, length, writable, fd, offset);
	/* 입력값 유효성 체크 */
	if (addr == NULL
		|| !is_user_vaddr(NULL)
		|| (uintptr_t) addr % PGSIZE != 0    // addr이 page-aligned 되어야 함
		// || (uintptr_t) offset % PGSIZE != 0
		|| length <= 0)
		goto error;
	if (fd == 0 || fd == 1)
		goto error;
	/* 가상주소 공간에서 기존의 페이지들과 겹치지 않는지 확인 
		- addr와 addr+length 사이에 있는 페이지들이 기존에 spt에 등록된 페이지와 겹치지 않는지 확인
	*/
	for (uintptr_t tmp_addr = addr; tmp_addr < addr + length; tmp_addr += PGSIZE) {
		// 이미 해당 가상주소 영역이 다른 목적으로 사용되고 있다면 종료
		if (spt_find_page(thread_current()->spt, tmp_addr)) 
			goto error;
	}

error:
	return NULL;
}

void _munmap (void *addr) {

}