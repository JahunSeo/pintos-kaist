/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
// ADD
#include <list.h>
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* mmap list */
static struct list mmap_list;

/* The initializer of file vm */
void
vm_file_init (void) {
	list_init(&mmap_list);
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	printf("[do_mmap] %p, %ld, %d, %d, %d\n", addr, length, writable, file, offset);
	// load_segment 참고
	void *tmp_addr = addr;
	uint32_t read_bytes = length;
	uint32_t zero_bytes = PGSIZE - (length % PGSIZE);
	// 임시 코드: 더 좋은 방법 생각해보기
	if (zero_bytes == PGSIZE) {
		zero_bytes = 0;
	}
	int page_cnt = 0;
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Set up aux to pass information to the lazy_load_segment. */
		// lazy_load_segment에 전달할 load_info를 구성하기 위해 메모리를 할당 받음
		struct load_info *aux = malloc(sizeof(struct load_info));
		// aux에 필요한 정보 저장
		aux->file = file;
		aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->type = VM_FILE;

		if (!vm_alloc_page_with_initializer (VM_FILE, tmp_addr,
					writable, lazy_load_segment, aux))
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		tmp_addr += PGSIZE;
		// 다음 page에 전달할 ofs를 업데이트
		offset += page_read_bytes;
		page_cnt++;
	}
	struct mmap_info *info = malloc(sizeof(struct mmap_info));
	info->addr = addr;
	info->length = length;
	info->page_cnt = page_cnt;
	list_push_back(&mmap_list, &info->elem);

	printf("[do_mmap] %p, %ld, %d\n", info->addr, info->length, info->page_cnt);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
