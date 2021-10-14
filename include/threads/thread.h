#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif
/* added */
#include "threads/vaddr.h"

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* file descriptor related */
// #define FDT_ENTRY_MAX 64
// #define FDT_PAGE_CNT (FDT_ENTRY_MAX + (PGSIZE - 1)) / (PGSIZE)

#define FDT_PAGE_CNT 3				
#define FDT_ENTRY_MAX FDT_PAGE_CNT *(1 << 9)    

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int64_t wakeup_tick;				/* 이 thread가 깨어나야 할 시점을 tick으로 저장 */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	/* donation 관련 멤버 */
	int init_priority; 					/* (donate 받는 입장에서) donate 받기 전 최초의 priority를 기록 */

	struct lock *wait_on_lock;          /* (donate 주는 입장에서) donate 주는 이유인 lock을 기록 */
	struct list donations;				/* (donate 받는 입장에서) 본인에게 donate 준 thread 들을 기록 */
	struct list_elem donation_elem;		/* (donate 주는 입장에서) donate 받은 thread의 donation list에서 연결 노드로 사용됨 */

	/* child precess 관련 멤버 */
	struct list children;				/* (부모 thread 입장에서) 자식 thread들을 담은 list */
	struct list_elem child_elem;		/* (부모 thread 입장에서) 자식 thread들이 연결되는 노드로 사용됨 */

	/* fork 관련 멤버
		- parent_if
			- parent가 fork될 당시의 register 상태를 보관하는 곳 (parent 본인의 parent_if를 업데이트한 뒤 thread_create를 실행)
			- 이 때, thread_create로 생성된 child가 곧바로 실행될지, 혹은 parent가 계속 진행되다가 child가 실행될지 알 수 없음
			- 그러므로 child가 처음 실행되는 시점의 parent register 상태는 fork가 요청된 시점의 상태와 다를 수 있음 (그래서 parent_if에 보관해두는 것)
			- child가 실행될 때 parent의 parent_if에서 보관된 reg 상태 정보를 가저와 본인의 reg 로 업데이트함 (즉 fork된 시점의 reg 상태)
	 */
	int exit_status;					/* 종료되었을 때의 상태 정보: parent가 child의 종료 상태를 확인하기 위해 사용 */
	struct intr_frame parent_if;
	struct semaphore fork_sema;			/* 현재 thread가 fork 완료되었는지 여부 // Q. 왜 lock이 아닐까? */
	struct semaphore wait_sema;			/* 현재 thread가 parent에 의해 wait되는지 여부 */
	struct semaphore free_sema;			/* 현재 thread가 parent에 의해 회수되었는지 여부 (회수 대상은 exit_status) */
	/* file descriptor 관련 멤버 */
	struct file** fdt;					/* "'파일의 주소값'들을 담은 배열"에 대한 주소값 */
	int next_fd;						/* 새로운 파일을 open 시 그 파일에 부여할 fd 값 */
	int max_fd;							/* 파일이 들어가 있는 fd의 최대값 (fork, exit에서 활용) */
	/* executable 관련 멤버 */
	struct file* running_file;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);

int64_t get_next_tick_to_awake(void);
void update_next_tick_to_awake(int64_t ticks);

int thread_get_priority (void);
void thread_set_priority (int);

// function for Priority Scheduling 
void test_max_priority (void);
bool thread_compare_priority (const struct list_elem *a, const struct list_elem *b, void *aux);
bool thread_compare_donate_priority (const struct list_elem *a, const struct list_elem *b, void *aux);

void donate_priority (void);
void remove_with_lock (struct lock *lock);
void refresh_priority(void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
