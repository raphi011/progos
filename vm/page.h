#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hash.h>
#include "filesys/filesys.h"
#include "threads/thread.h"

struct page {
	
	uintptr_t pg_no; //Page number (hash key)
	struct hash_elem hash_elem; /* Hash table element, like list_elem for example */
	void *addr; /*Virtual address */
	struct file *file;
	bool writable;
	size_t page_read_bytes;
	size_t page_zero_bytes;
	off_t ofs;


};

struct page *
page_lookup (const void *address);

bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED);

bool
page_load(void *fault_addr, void* esp);




