#include "vm/page.h"
#include <hash.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/palloc.h"
#include <stddef.h>
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/malloc.h"


/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->addr < b->addr;
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct page *
page_lookup (const void *address)
{
  struct page p;
  struct hash_elem *e;
  struct thread* thread = thread_current();
  p.addr = pg_round_down(address);
  e = hash_find (&thread->pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

struct page *
page_new_blank(void * addr, bool writable, size_t bytes) {
    struct page *page = malloc(sizeof(struct page));

    page->addr = pg_round_down(addr);
    page->page_read_bytes = 0;
    page->page_zero_bytes = bytes;
    page->writable = writable;
    page->type = ZERO_T;

    page_add (page);

    return page;
}

struct page *
page_new_file(void * addr, struct file *file, bool writable, size_t read_bytes, size_t zero_bytes, off_t ofs) {
    struct page *page = malloc(sizeof(struct page));
    /* Get a page of memory. */
    
    page->addr = pg_round_down (addr);
    page->file = file;
    page->writable = writable;
    page->page_read_bytes = read_bytes;
    page->page_zero_bytes = zero_bytes;
    page->ofs = ofs;
    page->type = FILE_T;

    page_add (page);

    return page;
}

void
page_add(struct page* page) {
    struct thread* thread = thread_current();
    hash_insert (&thread->pages, &page->hash_elem);
}

bool
page_load(struct page* page) {
    ASSERT(page != NULL);
  
  uint8_t *kpage = palloc_get_page (PAL_USER);
    if (kpage == NULL)
      return false;

    if (page->type == FILE_T) {
        if (file_read_at (page->file, kpage, page->page_read_bytes, page->ofs) != (int) page->page_read_bytes) {
          palloc_free_page (kpage);
          return false;
        }
    } 

    memset (kpage + page->page_read_bytes, 0, page->page_zero_bytes);

    if (!install_page (page->addr, kpage, page->writable)) {
      palloc_free_page (kpage);
      return false;
    }

  return true;
}
