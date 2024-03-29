/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

// ADD
#include <hash.h>
#include <list.h>
#include "threads/mmu.h"
#include "threads/synch.h"

/* frame_table */
static struct list frame_table;
/* evict victim 로직(clock algorithm) 관련 */
static struct lock clock_lock; // race 방지를 위한 lock
static struct list_elem *clock_elem; // 마지막 탐색 위치부터 이어서하기 위해 보관

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	// frame_table 초기화
	// - 정적 변수로 정의된 상태 (만약 malloc으로 할당한다면 여기서 처리)
	list_init(&frame_table); 
	// clock lock 초기화
	lock_init(&clock_lock);
	clock_elem = NULL;
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`.
 * - process.c의 load에서 load_segment를 콜하고, 
 * - load_segment에서 vm_alloc_page_with_initializer을 콜함
 * - 이 initializer 함수는 필요한 시점에 lazy load할 수 있도록
 * - init으로 전달된 lazy_load_segment를 활용해 처리해 둠
 *  */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check whether the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// malloc으로 page 생성: 나중에 vm_dealloc_page()를 통해 free하게 됨
		struct page *newpage = (struct page *) malloc(sizeof(struct page));
		// uninit_new()로 page 초기화: VM_UNINIT 상태로 만듦
		if (VM_TYPE(type) == VM_ANON) {
			uninit_new(newpage, upage, init, type, aux, anon_initializer);
		} else if (VM_TYPE(type) == VM_FILE) {
			uninit_new(newpage, upage, init, type, aux, file_backed_initializer);
		} 
		// printf("[vm_alloc_page_with_initializer] %d, %p\n", type, upage);
		newpage->writable = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, newpage);
	}
err:
	// printf("[vm_alloc_page_with_initializer] error!!\n");
	return false;
}

/* Find VA from spt and return page. On error, return NULL.
	- spt에서 va를 가진 page의 주소값을 리턴하기
 */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	/* TODO: Fill this function. */
	// spt에서 va를 가진 page를 찾기 위해, 동일한 va를 가진 dummy page를 생성해서 hash_find의 인자로 넘겨줌
	// - ((WARNING)) dummy page를 malloc 등으로 생성해야 할까? 일단은 아닐 것 같음!
	struct page page; 
	page.va = pg_round_down(va); // page boundary에 일치하도록 조정
	struct hash_elem *e;
	e = hash_find(&spt->page_table, &page.h_elem);
	// va가 일치하는 page를 찾지 못한 경우
	if (e == NULL)
		return NULL;
	// va가 일치하는 page를 찾은 경우
	return hash_entry(e, struct page, h_elem);
}

/* Insert PAGE into spt with validation.
    - spt에 page를 추가
	- 추가하기 전에 spt에 동일한 va(virtual address)가 이미 존재하는지 체크해주어야 함
 */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
	/* TODO: Fill this function. */
	// hash_insert 함수에서 동일한 va를 가진 page가 있었는지에 따라 리턴값이 다름
	// - 기존에 있었다면 해당 page, 없었다면 NULL을 리턴함
	struct hash_elem *e;
	e = hash_insert(&spt->page_table, &page->h_elem);
	// 새로운 page 추가에 실패한 경우 (기존에 동일한 주소값을 가진 page가 존재한 경우)
	if (e != NULL) {
		printf("[spt_insert_page] fail! %p\n", page);
		return false;
	}
	// 새로운 page 추가에 성공한 경우
	return true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	struct hash_elem *e;
	e = hash_delete(&spt->page_table, &page->h_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	// printf("[vm_get_victim] start\n");
	/* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current();
	struct frame *victim;
	// thread 간 race problem을 방지하기 위해 lock으로 접근을 통제
	lock_acquire(&clock_lock);
	// 마지막 탐색 위치부터 탐색 시작
	struct list_elem *e = clock_elem;
	// frame_table을 하나씩 돌며 access되지 않은 frame 찾기
	// - current thread에서 accessed 여부를 체크
	// - 즉 현재 로직에서는 current thread가 아닌 다른 thread의 page는 바로 victim으로 처리됨
	// - 만약 다른 프로세스도 포함시키고 싶다면 frame 구조체에 thread member를 추가해주면 될 듯
	// - [실험] frame에서 thread를 관리하는 방향으로 시도!!
	bool found = false;
	while (!found) {
		if (e == NULL || e == list_end(&frame_table))
			e = list_begin(&frame_table);
		// 한 바퀴 돌면서 victim을 못 찾지 못한 경우(즉 모두 accessed 였던 경우) 반복해 순회 
		//  - 최대 for loop 이 3번 시작됨
		for (clock_elem = e; 
		clock_elem != list_end(&frame_table); 
		clock_elem = list_next(clock_elem)) {
			victim = list_entry (clock_elem, struct frame, elem);
			if (pml4_is_accessed(curr->pml4, victim->page->va))	{
				pml4_set_accessed(curr->pml4, victim->page->va, 0);
			// if (pml4_is_accessed(victim->thread->pml4, victim->page->va)) {
			// 	pml4_set_accessed(victim->thread->pml4, victim->page->va, 0);
			} else {
				// 현재 victim으로 탐색 종료
				found = true;
				break;
			}
		}
	}
	// 다음 elem로 옮겨 둚
	clock_elem = list_next(clock_elem);
	// 
	lock_release(&clock_lock);
	// printf("[vm_get_victim] end %p\n", victim);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim == NULL)
		return NULL;
	// swap out 처리: page type에 맞게 처리됨
	if (!swap_out(victim->page))
		PANIC("fail to swap out.. maybe swap disk is full.");

	// frame 비워주기
	// - 주의! frame을 비워주지 않는다면 서로 다른 process 간에 침범이 발생할 수 있음
	// - 가령, swap out된 process B의 page를 process A가 볼 수도 있음
	victim->page = NULL;
	victim->thread = NULL;
	memset(victim->kva, 0, PGSIZE);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	// 물리메모리의 유저 영역에서 page 하나를 할당 받음
	struct page *phys_page;
	phys_page = palloc_get_page(PAL_USER);
	// frame을 구성
	struct frame *frame;
	// page 할당에 실패한 경우 (이미 가득찬 경우)
	if (phys_page == NULL) {
		// 임시로 PANIC 처리
		// PANIC("TODO: vm_evict_frame");
		// TODO: 기존의 frame 중 victim을 정해 swap out 처리 후 재활용 
		frame = vm_evict_frame();
	} 
	// page 할당에 성공한 경우
	else {
		// frame 또한 malloc을 통해 새로 구성
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = phys_page;
		frame->page = NULL; // 여기의 page는 phys_page에 들어갈 가상 주소 공간의 page
		frame->thread = NULL; // 실험적 코드
		// 새로 생성한 frame을 frame_table에 추가
		// - 일단 push_back으로 처리하되, 추후 victim 정하는 정책에 맞게 수정
		list_push_back(&frame_table, &frame->elem);
	}
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	// printf("[vm_stack_growth] %p\n", addr);
	// stack_bottom: 현재 주소가 들어갈 page의 주소값
	uintptr_t new_stack_bottom = pg_round_down(addr);
	// thread의 stack_bottom이 목표 위치에 도달할 때까지 page를 추가
	while (thread_current ()->stack_bottom != new_stack_bottom) {
		// 한 page 씩 추가하며 stack_bottom을 업데이트
		uintptr_t tmp_stack_bottom = thread_current ()->stack_bottom - PGSIZE;
		// spt에 page 추가
		if (!vm_alloc_page(VM_ANON | VM_MARKER_0, tmp_stack_bottom, 1))
			goto error;
		// 즉시 물리메모리에 배치
		if (!vm_claim_page(tmp_stack_bottom))
			goto error;
		// 스택에 할당되어 있는 메모리 영역의 범위(최상단) 표시
		thread_current()->stack_bottom = tmp_stack_bottom;
	}
	return;
error:
	PANIC("vm_stack_growth fail");
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* On page fault, the page fault handler (page_fault in userprog/exception.c) 
  transfers control to vm_try_handle_fault, which first checks if it is a valid page fault. 
  By valid, we mean the fault that accesses invalid.  If it is a bogus fault, 
  you load some contents into the page and return control to the user program.
  There are three cases of bogus page fault: 
  lazy-loaded, swaped-out page, and write-protected page.
  - Return true on success 
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr,
		bool user, bool write, bool not_present) {
	// printf("[vm_try_handle_fault] hello! %p, %p, %d, %d, %d\n", f->rsp, addr, user, write, not_present);
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page;
	/* TODO: Validate the fault */
	// user mode 일 때 kernel 영역에 접근하려 한 경우, 잘못된 접근이 맞음
	if (user && is_kernel_vaddr(addr))
		return false;
	/* stack growth 가 필요한 상황인지 확인
		- 이 때, addr은 현재 process의 가상주소
		- stack size는 가이드에 따라 1MB로 제한 (0x100000)
	 */
	uintptr_t curr_stack_bottom = thread_current ()->stack_bottom;
	uintptr_t new_stack_bottom = pg_round_down(thread_current ()->last_usr_rsp);

	// printf("[vm_try_handle_fault] stack check: %d, %d, %d, %d, %d, %d \n", 
	// 	is_user_vaddr(addr),
	// 	write, not_present, 
	// 	new_stack_bottom < (uintptr_t) addr,
	// 	(curr_stack_bottom - new_stack_bottom) / PGSIZE,
	// 	new_stack_bottom >= USER_STACK - 0x100000);
	// printf("[vm_try_handle_fault]  - %p, %p, %p, %p\n", addr, thread_current ()->last_usr_rsp, new_stack_bottom, curr_stack_bottom);

	if (is_user_vaddr(addr)     // 스택이므로 접근하려는 주소는 유저 영역이어야 함
		&& write				// 스택이 부족한 상황이므로 write을 위한 접근 
		&& not_present			// 스택이 부족한 상황이므로 not_present (read only가 아님)
		&& new_stack_bottom < (uintptr_t) addr // 접근하려는 주소가 스택 범위 내에 속해야 함
		// && new_stack_bottom == curr_stack_bottom - PGSIZE // 한 번에 하나의 페이지만 증가한다는 전제 제거
		&& new_stack_bottom >= USER_STACK - 0x100000
	) {
		vm_stack_growth(addr);
		return true;
	}
	/* TODO: Your code goes here */
	page = spt_find_page(spt, addr);
	if (page == NULL) {
		// printf("[vm_try_handle_fault] no page! %p, %p\n", page, addr);
		return false;
	}
	// printf("[vm_try_handle_fault] found page! %p, %p, %d, %d\n", 
	// 	page->va, addr, page->operations->type, page->uninit.type);
	
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	/* TODO: Fill this function */
	// 현재 thread의 spt에서 va에 해당하는 page를 찾음
	struct page *page;
 	page = spt_find_page(&thread_current()->spt, va);
	// page 찾기에 실패한 경우
	if(page == NULL) {
		return false;
	}
	// page를 찾은 경우, page를 page table(pml4)를 통해 물리 메모리에 배치시킴
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// page를 넣을 frame 한 개를 선택
	//  - 여기서 page는 supplemental page table에 있지만, 
	//  - 아직 page table(pml4)에는 등록되지 않은, 즉 물리 메모리 (혹은 disk) 상에는 올라가지 않은 상태
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 현재 thread의 page table(pml4)에 pte 추가하기
	//  - pte를 만드는데 필요한 정보: page의 가상 주소(page->va), 물리메모리에 실제로 놓인 위치(frame->kva) 등 
	//  - 이 때, page table에 이미 동일한 가상 주소가 추가되어 있는지 사전 체크
	//  - 2주차 코드 중 install_page 참고
	struct thread *t = thread_current();
	if (pml4_get_page (t->pml4, page->va) == NULL
		&& pml4_set_page (t->pml4, page->va, frame->kva, page->writable)) {
		// 실험적 코드
		frame->thread = thread_current();
		// printf("[vm_do_claim_page] before swap_in %p %p\n", page->va, frame->kva);
		return swap_in (page, frame->kva);
	}
	// page table에 추가 실패 시 처리
	// printf("[vm_do_claim_page] fail swap_in\n");
	return false;	
}

/* Initialize new supplemental page table 
  - 실행시점: 새로운 프로세스가 생성될 때 & forK될 때
  - 매개변수: 새로 생성되거나 fork 되는 thread의 spt 멤버의 주소값
  - 역할: 해당 thread에 spt를 초기화
	- 이 때, spt의 자료 구조는 정의하기 나름이며, 여기서는 hash table로 결정
*/
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	// hash_init으로 초기화
	//   - hash_init은 malloc을 통해 본인의 bucket 멤버에 메모리를 할당함
	if (!hash_init(&spt->page_table, page_hash, page_less, NULL)) {
		// TODO: malloc을 통한 메모리 할당에 실패했을 경우 처리 필요
	}
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	// dst는 process를 fork하며 새로 생성 및 초기화된 spt (비어 있음)
	// printf("[spt_copy] start %p, %p\n", dst, src);
	// hash iterator 초기화
	struct hash_iterator i;
	hash_first(&i, &src->page_table);
	// hash table에 있는 각 page들을 돌며 복사
	while (hash_next(&i)) {
		struct page *p_page = hash_entry(hash_cur(&i), struct page, h_elem);
		enum vm_type p_type = p_page->operations->type;
		// printf("[spt_copy] parent_page: %p, %d\n", p_page->va, p_type);
		// VM_UNINIT인 경우: 아직 spt에만 존재하고 물리메모리에 올라가지 않은 페이지들
		if (VM_TYPE(p_type) == VM_UNINIT) {
			// printf("[spt_copy] VM_UNINIT! %d\n", p_type);
			// parent_page 에서 보관 중인 정보들을 가져옴
			vm_initializer *p_init = p_page->uninit.init;
			struct load_info *p_aux = p_page->uninit.aux;
			// child_page에 전달할 새로운 aux를 구성
			struct load_info *c_aux = malloc(sizeof(struct load_info));
			if (p_page->uninit.type == VM_FILE) {
				c_aux->file = file_duplicate(p_aux->file);
			} else {
				c_aux->file = p_aux->file;
			}
			c_aux->ofs = p_aux->ofs;
			c_aux->page_read_bytes = p_aux->page_read_bytes;
			c_aux->page_zero_bytes = p_aux->page_zero_bytes;
			// child process에서 새로운 page를 할당
			if (!vm_alloc_page_with_initializer (p_page->uninit.type, p_page->va,
						p_page->writable, p_init, c_aux))
				return false;
		} 
		// 나머지 경우: page table(pml4)와 물리메모리에 올라간 상태의 페이지들
		else if (VM_TYPE(p_type) == VM_ANON) {
			// printf("[spt_copy] VM_ANON! %d\n", p_type);
			// child process를 위한 새로운 page 할당: type, va, writable 그대로 유지
			if (!vm_alloc_page(p_type, p_page->va, p_page->writable))
				return false;
			// 새로 할당된 child_page의 주소값 찾기
			struct page *c_page = spt_find_page(dst, p_page->va);
			// 새로운 child_page를 바로 물리메모리에 배치시킴
			if (!vm_do_claim_page(c_page))
				return false;
			// parent page를 child page에 복사함
			// - 만약 parent page가 disk로 swap 되어 있었다면 어떻게 하지? 
			// - memcpy 전에 parent page도 물리메모리에 올려놓도록 조치를 해야할까?
			memcpy(c_page->frame->kva, p_page->frame->kva, PGSIZE);
		} else if (VM_TYPE(p_type) == VM_FILE) {
			printf("[spt_copy] VM_FILE! %d\n", p_type);
			// TODO: 일단 아무 것도 하지 않음
		}
	}
	// 정상적으로 copy 되었다는 것을 알려주기 위해 true 리턴
	return true;
}

/* hash_destroy에 전달할 두 번째 인자 함수
    - hash 내 각 hash_elem들에게 특정한 액션을 취할 수 있음
	- hash로 구성된 page table 내 page들을 free하는데 사용 (destroy)
	- spt_kill에서만 사용되므로 static으로 정의 
 */
static void
page_destroy (struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, h_elem);
	destroy(page);
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->page_table, page_destroy);
}

/* Returns a hash of element's data, as a value anywhere in the range of unsigned int */ 
uint64_t 
page_hash (const struct hash_elem *e, void *aux UNUSED) {
	// 매개변수 hash_elem *e는 page 구조체의 hash_elem이며, page들을 연결하는 연결고리 역할을 함
	struct page *p = hash_entry(e, struct page, h_elem);
	// hash_bytes: Returns a hash of the size bytes starting at buf
	// -  va는 (void *), 그냥 주소값? 주소값이 unique 하긴 하지.. 그러면 주소값을 hash 하는 건가?
	return hash_bytes(&p->va, sizeof p->va);
}

/*  Compares the keys stored in elements a and b. 
	Returns true if a is less than b, false if a is greater than or equal to b. 
	If two elements compare equal, then they must hash to equal values. */
bool 
page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	struct page *pa = hash_entry(a, struct page, h_elem);
	struct page *pb = hash_entry(b, struct page, h_elem);
	// page의 주소값의 크기(선후관계)를 비교
	return pa->va < pb->va; 
}