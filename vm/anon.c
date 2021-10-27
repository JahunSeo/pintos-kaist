/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

// ADD
#include <round.h>
#include <bitmap.h>
// SECTORS_PER_PAGE: 한 PAGE를 수용하는데 필요한 disk sector의 수 (4096 bytes // 512 bytes)
#define SECTORS_PER_PAGE DIV_ROUND_UP(PGSIZE, DISK_SECTOR_SIZE)

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
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// page가 담겨 있던 frame에 해당 page가 삭제되었다는 표시를 해둠
	if (page->frame)
		page->frame->page = NULL;
}
