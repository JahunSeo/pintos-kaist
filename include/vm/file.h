#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"
// ADD
#include <list.h>

struct page;
enum vm_type;

struct file_page {
	// munmap 등에서 page가 dirty 상태일 때, 다시 저장해주기 위해 file을 들고 있어야 함
	struct file* file;
	// 파일에 저장할 위치를 파악하기 위해 offset도 들고 있어야 함
	off_t ofs;
	size_t size;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);

struct mmap_info {
	void *addr;
	size_t length;
	int page_cnt;
	struct list_elem elem;
};

#endif
