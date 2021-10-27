/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

// ADD
#include <round.h>
#include <bitmap.h>
// SECTORS_PER_PAGE: 한 PAGE를 수용하는데 필요한 disk sector의 수 (4096 bytes // 512 bytes)
#define SECTORS_PER_PAGE DIV_ROUND_UP(PGSIZE, DISK_SECTOR_SIZE)
#define INITIAL_SWAP_IDX -1

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* ANON 중 stack임을 표시해둔 operations */
static const struct page_operations stack_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON | VM_MARKER_0,
};

/* swap table 
  - 특정 slot이 사용중인지 여부를 체크하는데, bitmap table이 유리
  - anon.c에서만 접근할 것이므로 static 으로 설정
*/
static struct bitmap *swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// disk에서 swap을 위한 영역 받아오기: swap을 위해서는 1, 1로 설정
	swap_disk = disk_get(1, 1); 
	// 섹터 단위로 표현된 swap disk의 크기
	//  - the size of disk D, measured in DISK_SECTOR_SIZE-byte sectors. 
	disk_sector_t disk_capacity = disk_size(swap_disk);
	// disk 내에 몇 개의 페이지를 수용 가능한지 계산
	//  - SECTORS_PER_PAGE : 한 page를 수용하는데 필요한 sector의 수 (4096byte // 512bytes)
	size_t max_slot = disk_capacity / SECTORS_PER_PAGE;
	// printf("[vm_anon_init] max_slot %d\n", max_slot);
	// swap table 초기화
	swap_table = bitmap_create(max_slot);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler: 스택 여부를 구분 */
	if (type & VM_MARKER_0) {
		page->operations = &stack_ops;
	} else {
		page->operations = &anon_ops;
	}
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_idx = INITIAL_SWAP_IDX; // swap_idx가 0부터 시작하기 때문에 init값으로 -1
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	// printf("[anon_swap_in] start swap_idx %d\n", anon_page->swap_idx);
	if (anon_page->swap_idx == INITIAL_SWAP_IDX)
		return false;
	// swap disk에 있는 내용을 page에 옮겨 적기
	// - page를 옮겨 적기 위해서는 SECTORS_PER_PAGE 개수의 sector가 필요함
	// - 한 sector의 크기는 DISK_SECTOR_SIZE bytes임
	disk_sector_t sec_no;
	uintptr_t offset; // 주의! 자료형에 따라 값이 제대로 안 잡힐 수 있음
	for (int i=0; i < SECTORS_PER_PAGE; i++) {
		sec_no = anon_page->swap_idx * SECTORS_PER_PAGE + i;
		offset = page->frame->kva + DISK_SECTOR_SIZE * i;
		disk_read(swap_disk, sec_no, offset);
	}
	// swap table의 해당 위치가 비었음을 표시
	bitmap_set(swap_table, anon_page->swap_idx, false);
	// printf("[anon_swap_in] end swap_idx %d\n", anon_page->swap_idx);
	// swap_idx 초기화
	anon_page->swap_idx = INITIAL_SWAP_IDX;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	// printf("[anon_swap_out] start %p, %p\n", page->va, page->frame->kva);
	struct anon_page *anon_page = &page->anon;
	// page 유효성 체크
	if (page == NULL
		|| page->frame == NULL
		|| page->frame->kva == NULL)
		return false;
	// swap table에서 page 1개 들어갈 위치 확보 
	// - swap table의 해당 slot의 bit들은 false로 초기화
	size_t swap_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
	// printf("[anon_swap_out] swap_idx %d\n", swap_idx);
	// swap table이 가득 찬 경우 에러 처리
	if (swap_idx == BITMAP_ERROR)
		return false;
	// page에 있는 내용을 disk에 옮겨 적기
	// - page를 옮겨 적기 위해서는 SECTORS_PER_PAGE 개수의 sector가 필요함
	// - 한 sector의 크기는 DISK_SECTOR_SIZE bytes임
	disk_sector_t sec_no;
	uintptr_t offset; // 주의! 자료형에 따라 값이 제대로 안 잡힐 수 있음
	for (int i=0; i < SECTORS_PER_PAGE; i++) {
		sec_no = swap_idx * SECTORS_PER_PAGE + i;
		offset = page->frame->kva + DISK_SECTOR_SIZE * i;
		disk_write(swap_disk, sec_no, offset);
	}
	// swap table의 해당 위치에 page가 추가되었음을 표시
	bitmap_set(swap_table, swap_idx, true);
	// 나중에 swap in을 하기 위해 anon_page에 swap_idx 를 저장
	anon_page->swap_idx = swap_idx;
	// pml4에서 빠졌음을 표시
	// pml4_clear_page(thread_current()->pml4, page->va);
	pml4_clear_page(page->frame->thread->pml4, page->va);
	// printf("[anon_swap_out] done %p\n", page->va);
	pml4_set_dirty (page->frame->thread->pml4, page->va, false);
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// frame에 할당되었던 메모리 해제
	if (page->frame != NULL) {
		list_remove (&page->frame->elem);
		free(page->frame);
	} 
	// 만약 swap 되어 있었다면 
	else {
		struct anon_page *anon_page = &page->anon;
		if (anon_page->swap_idx != INITIAL_SWAP_IDX) {
			bitmap_set (swap_table, anon_page->swap_idx, false);
		}
	}
}
