/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

// ADD
#include <hash.h>
#include <list.h>
#include "threads/mmu.h"

/* frame_table */
static struct list frame_table;

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
		newpage->writable = writable;
		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, newpage);
		return true;
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
	if (e != NULL)
		return false;
	// 새로운 page 추가에 성공한 경우
	return true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
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
		PANIC("TODO: vm_evict_frame");
		// TODO: 기존의 frame 중 victim을 정해 swap out 처리 후 재활용 
		frame = vm_evict_frame();
		frame->page = NULL;
	} 
	// page 할당에 성공한 경우
	else {
		// frame 또한 malloc을 통해 새로 구성
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = phys_page;
		frame->page = NULL; // 여기의 page는 phys_page에 들어갈 가상 주소 공간의 page
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
vm_stack_growth (void *addr UNUSED) {
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
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	printf("[vm_try_handle_fault] hello!\n");

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
	if (pml4_get_page (t->pml4, page) == NULL
		&& pml4_set_page (t->pml4, page->va, frame->kva, page->writable)) {
		return swap_in (page, frame->kva);
	}
	// page table에 추가 실패 시 처리
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
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/* Returns a hash of element's data, as a value anywhere in the range of unsigned int */ 
uint64_t page_hash (const struct hash_elem *e, void *aux UNUSED) {
	// 매개변수 hash_elem *e는 page 구조체의 hash_elem이며, page들을 연결하는 연결고리 역할을 함
	struct page *p = hash_entry(e, struct page, h_elem);
	// hash_bytes: Returns a hash of the size bytes starting at buf
	// -  va는 (void *), 그냥 주소값? 주소값이 unique 하긴 하지.. 그러면 주소값을 hash 하는 건가?
	return hash_bytes(&p->va, sizeof p->va);
}

/*  Compares the keys stored in elements a and b. 
	Returns true if a is less than b, false if a is greater than or equal to b. 
	If two elements compare equal, then they must hash to equal values. */
bool page_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	struct page *pa = hash_entry(a, struct page, h_elem);
	struct page *pb = hash_entry(b, struct page, h_elem);
	// page의 주소값의 크기(선후관계)를 비교
	return pa->va < pb->va; 
}