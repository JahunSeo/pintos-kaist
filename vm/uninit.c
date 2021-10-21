/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init, // lazy_load_segment 가 들어옴
			.type = type, // lazy load 시 변경될 실제 type
			.aux = aux,
			.page_initializer = initializer, // lazy load 시 page를 type에 맞게 초기화해주는 함수
		}
	};
}

/* Initalize the page on first fault 
  - vm_do_claim_page()을 통해 swap_in 될 때, page가 uninit 상태였다면 실행되는 함수
  - 인자로 전달된 page를 실제 type으로 다시 초기화하여 kva가 가리키는 물리메모리에 올려놓는 역할 수행
  - 이 함수가 실행될 때 page는 이미 thread의 page table(pml4)에 올라간 상태임
*/
static bool
uninit_initialize (struct page *page, void *kva) {

	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init; // lazy_load_segment 가 들어옴
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */
	// 일단 page_initializer로 실제 type에 맞게 page를 다시 초기화한 뒤
	// init으로, 즉 lazy_load_segment로 해당 page를 kva가 가리키는 물리 메모리에 올려 놓음
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
}
