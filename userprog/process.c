#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static struct thread *get_child_process(tid_t child_tid);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* 새로 생성되는 thread의 이름을 실행하려는 프로그램명으로 수정 */
	char *save_ptr;
	file_name = strtok_r(file_name, " ", &save_ptr);
	// printf("[process_create_initd] %s\n", file_name);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
	/* Clone current thread to new thread.*/
	// parent(현재 thread)의 상태를 parent_if에 보관 (나중에 child가 사용할 것)
	struct thread *curr = thread_current();
	memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));
	// child thread 생성 (child thread가 수행할 __do_fork와 그 함수에 전달할 인자 curr)
	tid_t child_tid = thread_create (name, PRI_DEFAULT, __do_fork, curr);
	if (child_tid == TID_ERROR)
		return TID_ERROR;
	// child_tid 로 thread 가져오기 (이 때는 NULL일 수 없음)
	struct thread *child = get_child_process(child_tid);

	// child가 생성 완료될 때까지 대기
	sema_down(&child->fork_sema);
	// child 생성 중에 오류가 발생하지는 않았는지 체크
	if (child->exit_status == -1) // TODO: __do_fork에서 exit_status를 변경하기
		return TID_ERROR;

	return child_tid;
}

/* children list에서 특정 child thread의 주소값 가져오기 */
struct thread *get_child_process(tid_t child_tid) {
	struct thread *curr = thread_current();
	struct thread *child;
	struct list *children = &curr->children;
	struct list_elem *e;
	for (e = list_begin(children); e != list_end(children); e = list_next(e)) {
		child = list_entry(e, struct thread, child_elem);
		if (child->tid == child_tid) {
			return child; 
		}
	}
	return NULL;

}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) 
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
		printf("[duplicate_pte] parent_page error\n");
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page (PAL_USER);
	if (newpage == NULL) {
		printf("[duplicate_pte] newpage error\n");
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		printf("[duplicate_pte] pml4_set_page error\n");
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	parent_if = &parent->parent_if;	
	bool succ = true;

	// printf("[__do_fork] checkpoint 1\n");

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; // syscall fork's return value for child process

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.
	 * 부모의 file descriptor table을 복사
	 * 이 때, thread_create에서 fdt와 next_fd는 초기화가 된 상태
	 * 그러므로 parent의 fdt와 next_fd를 가져와 붙여주어야 함
	 * */

	// multi-oom 통과를 위해 아래 라인이 추가되어야 함. 왜?
	if (parent->next_fd == FDT_ENTRY_MAX) {
		// printf("[__do_fork] max fail.. why??\n");
		goto error;
	}

	// next_fd, max_fd 가져오기
	current->next_fd = parent->next_fd;	
	current->max_fd = parent->max_fd;	

	// fdt 가져오기: parent fdt의 file들을 current fdt에서 duplicate (아직 dup 를 고려하지 않음)
	struct file *orig_file;
	for (int i = 0; i <= parent->max_fd; i++) {
		orig_file = parent->fdt[i];
		if (i < 2) {
			current->fdt[i] = orig_file;			
		} else if (orig_file != NULL) {
			current->fdt[i] = (struct file*) file_duplicate(orig_file);
		}
	}


	process_init ();

	/* child process가 생성 완료되었음을 parent에게 전달 */
	sema_up(&current->fork_sema);

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = TID_ERROR;
	sema_up(&current->fork_sema);
	// thread_exit (); // thread_handler가 실행 종료되면, thread_exit()이 kernel_thread에서 실행됨 
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. 
	initd에서도 process_exec를 사용하고 있으므로, initd에서 활용하는 방식과 통일되어야 함
 */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	// printf("[process_exec] startpoint: %s\n", file_name);

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. 
	 * 이게 무슨 말이지?!!
	 * */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();
	#ifdef VM
	// 기존 spt는 supplemental_page_table_kill()로 지워졌기 때문에 spt도 새로 init 해주어야 함
	supplemental_page_table_init(&thread_current()->spt);
	#endif
	// printf("[process_exec] before load: %s\n", file_name);
	/* And then load the binary */
	success = load (file_name, &_if);
	// printf("[process_exec] after load %d\n", success);

	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	/* child_tid가 현재 thread의 자식인지 확인 */
	struct thread *child;
	child = get_child_process(child_tid);
	if (child == NULL)
		return TID_ERROR;
	/* 이미 child_tid를 wait하는 상태인지 확인 */
	if (list_size(&child->wait_sema.waiters) != 0)
		return TID_ERROR;
	/* child의 wait_sema를 down하여 대기 상태로 진입 */
	sema_down(&child->wait_sema);
	/* child의 exit_status 확인 */
	int exit_status = child->exit_status;
	/* current의 children에서 child 제거 */
	list_remove(&child->child_elem);
	/* child의 free_sema를 up시켜 child가 회수 완료되었음을 알림
		- thread_exit() 등의 작업은 free_sema를 획득한 child에서 마저 처리됨
	 */
	sema_up(&child->free_sema);
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). 
	- 이 함수는 thread_exit 안에서 실행된다는 점 주의!!
*/
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// fdt에서 열려있는 파일 닫기
	struct file *file;
	for (int fd = 0; fd <= curr->max_fd; fd++) {
		_close(fd);

		// file = curr->fdt[i];
		// if (i < 2) {
		// 	curr->fdt[i] = NULL; // TODO			
		// } else {
		// 	file_close(file);
		// }
	}
	// fdt에 할당된 kernel 영역의 메모리 회수하기
	palloc_free_multiple(curr->fdt, FDT_PAGE_CNT);
	// 실행 중이던 파일이 있다면 종료하기
	file_close(curr->running_file);

	process_cleanup ();

	/* parent가 현재 thread를 wait하고 있었다면, 종료되었음을 알림 
		- parent가 wait을 걸기 전에 child가 먼저 종료되었을 수도 있음
	*/ 
	sema_up(&curr->wait_sema);
	/* parent가 현재 thread를 회수할 때까지 thread_exit하지 않고 기다림 (exit_status를 전달하기 위함) */
	sema_down(&curr->free_sema);
	/* parent가 회수한 뒤 thread_exit의 남은 부분이 실행됨 */
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 1차로 argument parsing
		- file_name에서 인자들을 '\0'으로 구분
		- file_name에서 첫 번째 token으로 file 실행
		- file_name을 argument_stack의 첫번째 인자로 전달
	*/
	char *argv[64]; // string literal 주소값들의 배열
	int argc = 0;
 
	char *token, *save_ptr;
	// printf("[load] file_name %d, %s\n", argc, file_name);
	for (token = strtok_r(file_name, " ", &save_ptr); 
		token != NULL;
		token = strtok_r(NULL, " ", &save_ptr)) {
			argv[argc] = (char *) token;
			// printf("'%s'\n", argv[argc]);
			argc++;
	}
	// printf("[load] file_name %d, %s\n", argc, file_name);

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	t->running_file = file;
	file_deny_write(file);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	argument_stack(argv, argc, if_);
	// hex_dump(if_->rsp, if_->rsp, USER_STACK - if_->rsp, true);
	success = true;
done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file); // process_exit() 시점에 종료되도록 변경
	return success;
}

/*
	실행할 파일의 stack(esp)에 인자를 전달하는 함수
	- parse: 프로그램 이름과 인자가 저장되어 있는 메모리 공간
	- count: 인자의 개수
	- esp: 스택 포인터를 가리키는 주소 값
*/
void argument_stack(char **argv, const int argc, struct intr_frame *if_) {
	// printf("[argument_stack] %d, %p\n", argc, (char *) if_->rsp); // 0x47480000
	uintptr_t rsp = if_->rsp;
	/* 프로그램 이름 및 인자(문자열) push */
	for (int i = argc-1; i >= 0; i--) {
		// printf("  argv[%d] %ld, %s\n", i, strlen(argv[i]), (char *) argv[i]);
		rsp -= strlen(argv[i]) + 1; // rsp를 이동시켜 공간을 확보, '\0'을 위해 추가
		memcpy((char *) rsp, argv[i], strlen(argv[i]) + 1); // 확보된 공간에 문자열 추가
		argv[i] = (char *) rsp; // 스택에 추가된 문자열의 주소값을 보관 (argv 재활용)
		// printf("  argument: %p, %s, %p\n", (char *) rsp, (char *) rsp, (char *) argv[i]);
	}

	/* word alignment push
		- 현재 rsp 위치까지 데이터가 차 있음
		- rsp의 바로 아래 byte부터 rsp % 8개 만큼 0으로 채우면 8byte 패딩을 만들 수 있음
		- stack bottom (7,6,5==rsp,0,0,0,0,0)
	 */
	while (rsp % 8 != 0) {
		rsp--;
		// rsp는 그냥 interger이기 때문에, 먼저 1byte 주소값으로 casting을 해준 뒤 역변환을 통해 그 byte 자리에 0을 넣음
		*(char *)rsp = (char)0; // 여기서는 1byte
		// printf("  padding: %p, %c\n", (char *)rsp, *(char *) rsp);
	}

	/* 프로그램 이름 및 인자 주소들 push */
	// 포인터의 크기 계산
	size_t PTR_SIZE = sizeof(char *);
	// printf("  size of pointer: %ld\n", PTR_SIZE);
	// argv[argc] 위치에 0 삽입
	rsp -= PTR_SIZE;
	*(char **)rsp = (char *)0; // 여기서는 8 bytes
	// argv[0] ~ argv[argc-1]에 각 문자열의 주소값 저장
	for (int j=argc-1; j>=0; j--) {
		rsp -= PTR_SIZE;
		// 여기서 *(char *) 로 처리하면 에러 발생! 왜? 8byte가 아닌 1byte로 처리되기 때문
		// rsp부터 sizeof(char *) 크기 만큼, 즉 주소값 크기 만큼의 자리에 argv[j]를 넣겠다는 의미
		// 여기서 rsp는 (char *)에 대한 주소값이므로, *(char **)
		*(char **)rsp = argv[j]; 
		// printf("  at %p, %p (%p)\n", (char *)rsp, *(char **)rsp, (char *) argv[j]);
	}
	/* fake address(0) 저장 */
	rsp -= PTR_SIZE;
	*(char **)rsp = (char *)0; // 여기서도 8 bytes
	// printf("  fake address: %p, %p\n", (char *)rsp, *(char **)rsp);

	if_->rsp = rsp;
	/* argc (문자열의 개수 저장) push */
	if_->R.rdi = argc;
	/* argv (문자열을 가리키는 주소들의 배열을 가리킴) push*/ 
	if_->R.rsi = rsp + PTR_SIZE; // fake return address 위치를 빼주어야 함
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifdef VM
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */	
	/* TODO: VA is available when calling this function. */
	// aux를 load_info로 casting
	struct load_info *info = (struct load_info *) aux;
	struct file *file = info->file;
	off_t ofs = info->ofs;
	size_t page_read_bytes = info->page_read_bytes;
	size_t page_zero_bytes = info->page_zero_bytes;
	// file_read로 file을 읽어 물리메모리에 저장
	file_seek (file, ofs);
	if (file_read (file, page->frame->kva, page_read_bytes) != (int) page_read_bytes) {
		vm_dealloc_page(page); // destroy and free page
		return false;		
	}
	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	// page type이 VM_FILE 인 경우
	if (info->type == VM_FILE) {
		printf("[lazy_load_segment] type %d\n", info->type);
		// page table에 해당 page의 dirty bit를 false로 초기화
		pml4_set_dirty(&thread_current()->pml4, page->va, false);
	}

	// aux의 역할이 끝났으므로 할당되었던 메모리 free
	free(info);
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		// lazy_load_segment에 전달할 load_info를 구성하기 위해 메모리를 할당 받음
		struct load_info *aux = malloc(sizeof(struct load_info));
		// aux에 필요한 정보 저장
		aux->file = file;
		aux->ofs = ofs;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->type = VM_ANON;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		// 다음 page에 전달할 ofs를 업데이트
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
	// printf("[setup_stack] %p\n", stack_bottom);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	// stack_bottom 위치에 page 할당
	// - vm_type에 anonymous라는 점과 stack이라는 마커까지 추가하여 전달
	// - writable을 1로 설정
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {
		// 즉시 물리메모리에 배치
		success = vm_claim_page(stack_bottom);
		if (success) {
			// user stack pointer 업데이트
			if_->rsp = USER_STACK;
			// 스택에 할당되어 있는 메모리 영역의 범위(최상단) 표시
			thread_current()->stack_bottom = stack_bottom;
		} else {
			// TODO: 다시 page를 찾아 dealloc 해주어야 함
			PANIC("setup_stack fail");
			// vm_dealloc_page(page);	
		}
	}
	return success;
}

#else
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}

#endif /* VM */
