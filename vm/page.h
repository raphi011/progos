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

enum page_type {
    FILE_T,
    ZERO_T
};

struct page {
	struct hash_elem hash_elem; /* Hash table element, like list_elem for example */
	struct hash_elem map_elem; /* We need it to save a page in our hash table of our mappings */
	void *addr; /*Virtual address */
	struct file *file;
	bool writable;
	size_t page_read_bytes;
	size_t page_zero_bytes;
	off_t ofs;
	int ID;
    enum page_type type;
};

struct mmap_entry {
	struct hash_elem hash_elem;
	struct hash pages;
	int entries;
	struct page* page;
	int ID;
};

struct page *
page_lookup (const void *);

void
map_add(struct mmap_entry* mmap_entry);

bool
page_less (const struct hash_elem *, const struct hash_elem *b, void * UNUSED);

unsigned
page_hash (const struct hash_elem *, void * UNUSED);

bool
page_load(struct page*);

void
page_add(struct page*);

struct page*
page_new_blank(void*, bool, size_t);

struct page*
page_new_file(void *, struct file *, bool, size_t, size_t, off_t);

struct mmap_entry *
mmap_lookup (const int ID);

bool
mmap_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED);

unsigned
mmap_hash (const struct hash_elem *p_, void *aux UNUSED);


