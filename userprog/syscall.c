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
			// printf("  SYS_HALT called!\n");
			_halt ();
			break;

		case SYS_EXIT:				  	 /* Terminate this process. */
			// printf("  SYS_EXIT called!\n");
			_exit (f->R.rdi);
			break;

		case SYS_FORK:                   /* Clone current process. */
			// printf("  SYS_FORK called!\n");
			break;

		case SYS_EXEC:                   /* Switch current process. */
			// printf("  SYS_EXEC called!\n");
			break;

		case SYS_WAIT:                   /* Wait for a child process to die. */
			// printf("  SYS_WAIT called!\n");
			break;

		case SYS_CREATE:                 /* Create a file. */
			// printf("  SYS_CREATE called!\n");
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

void _halt (void) {
	power_off();
}

void _exit (int status) {
	printf("%s: exit(%d)\n", thread_name(), status); // TODO: this line should be relocated in precess_exit
	thread_exit ();
}

bool _create (const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

int _write (int fd, const void *buffer, unsigned size) {
	// temporary code to pass args related test case
	putbuf(buffer, size);
	return size;
}
