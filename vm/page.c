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

bool
page_load(void *fault_addr, void* esp){

  
  uint8_t *kpage = palloc_get_page (PAL_USER);
    if (kpage == NULL)
      return false;

  struct page* page = page_lookup(fault_addr);

  if(page!=NULL){

    if (file_read_at (page->file, kpage, page->page_read_bytes, page->ofs) != (int) page->page_read_bytes) {
      palloc_free_page (kpage);
      return false;
    }

    memset (kpage + page->page_read_bytes, 0, page->page_zero_bytes);

    if (!install_page (page->addr, kpage, page->writable)) {
      palloc_free_page (kpage);
      return false;
    }

  }

  return true;

}