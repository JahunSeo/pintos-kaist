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
	// aux에 담겨 있는 정보들로 file_page 업데이트: file 주소값, offset
	struct load_info *info = page->uninit.aux;
	// printf("[file_backed_initializer] %p, %p, %d, %d\n", page, info->file, info->ofs, info->page_read_bytes);
	file_page->file = info->file;
	file_page->ofs = info->ofs;
	return true;
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
	struct file_page *file_page = &page->file;
	// printf("[file_backed_destroy] %p\n", page);
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		// printf("[file_backed_destroy] handle dirty case\n");
		file_seek(file_page->file, file_page->ofs);
		file_write(file_page->file, page->va, file_page->size);
	}
	file_close(file_page->file);


}

/* lazy_load_file */
static bool
lazy_load_file (struct page *page, void *aux) {
	struct load_info *info = (struct load_info *) aux;
	// printf("[lazy_load_file] type %d\n", info->type);
	struct file *file = info->file;
	off_t ofs = info->ofs;
	size_t page_read_bytes = info->page_read_bytes;
	// size_t page_zero_bytes = info->page_zero_bytes;
	// file_read로 file을 읽어 물리메모리에 저장
	file_seek (file, ofs);
	size_t read_results = file_read (file, page->frame->kva, page_read_bytes);
	page->file.size = read_results;
	// TODO: file read 과정에서 발생할만한 에러가 있을까?
	if (false) {
		spt_remove_page(&thread_current()->spt, page); // destroy and free page
		return false;	
	}
	// 페이지의 남은 부분을 0으로 처리: testcase 'mmap-read' 참고
	if (read_results < PGSIZE) {
		memset (page->frame->kva + read_results, 0, PGSIZE - read_results);
	}
	// page table에 해당 page의 dirty bit를 false로 초기화
	pml4_set_dirty(&thread_current()->pml4, page->va, false);
	// aux의 역할이 끝났으므로 할당되었던 메모리 free
	free(info);
	return true;
}


/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// printf("[do_mmap] %p, %ld, %d, %p, %d\n", addr, length, writable, file, offset);
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
		aux->file = file_reopen(file);
		aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->type = VM_FILE;

		if (!vm_alloc_page_with_initializer (VM_FILE, tmp_addr,
					writable, lazy_load_file, aux))
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

	// printf("[do_mmap] %p, %ld, %d\n", info->addr, info->length, info->page_cnt);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// printf("[do_munmap] %p\n", addr);
	// mmap_list에서 addr로 시작하는 mmap 영역 찾기
	struct mmap_info *info;
	struct list_elem *e;
	for (e = list_begin (&mmap_list); e != list_end (&mmap_list); e = list_next (e)) {
		if (list_entry(e, struct mmap_info, elem)->addr == addr) {
			info = list_entry(e, struct mmap_info, elem);
			break;
		}
	}
	if (info != NULL) {
		// printf("[do_munmap] found! count: %d\n", info->page_cnt);
		// mmap되었던 페이지들을 전체 dealloc
		struct page *page;
		for (int i=0; i<info->page_cnt; i++) {
			page = spt_find_page(&thread_current()->spt, addr);
			// page가 없어진 상황은 에러인가? 아니면 넘어가도 되는가?
			if (page)
				spt_remove_page(&thread_current()->spt, page);
			addr = addr + PGSIZE;
		}
		// 할당되었던 info 영역 free
		list_remove(&info->elem);
		free(info);
	}
}
