#include <stdio.h>
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

// Pravat driving, return a hash index for this page entry
unsigned int hash_page_func(const struct hash_elem* page_element, void* aux) {
  (void*) aux;

  // receive the page entry referring to the page entry element
  struct page_entry* page =  hash_entry(page_element,
    struct page_entry, page_entry_elem);

  // remove the offset bits of the page's address to return the page number
  unsigned int user_page = (unsigned int) page->user_page;
  return user_page >> PGBITS;
}

/*
  Dinesh driving, return if page entry 1 is less than page entry 2
  https://piazza.com/class/k5iivwicu0kvg?cid=1037
*/
bool hash_page_comparator(const struct hash_elem* page_element_1,
  const struct hash_elem* page_element_2, void* aux) {
  (void*) aux;

  // receive the page entries referring to the page entry elements
  struct page_entry* page_1 = hash_entry(page_element_1,
    struct page_entry, page_entry_elem);
  struct page_entry* page_2 = hash_entry(page_element_2,
    struct page_entry, page_entry_elem);

  uint8_t* user_page_1 = page_1->user_page;
  uint8_t* user_page_2 = page_2->user_page;
  return user_page_1 < user_page_2;
}

// Abhi driving, grow the stack using a user pool page
bool grow_stack(uint8_t* user_page) {
  // create and set the user page property for the new page entry
  struct page_entry* page = malloc(sizeof(struct page_entry));
  if(!page) {
    return false;
  }
  page->user_page = pg_round_down(user_page);

  // check if the page is not past the stack's size limit
  size_t page_size = PHYS_BASE - (void*) (page->user_page);
  if(page_size > STACK_LIMIT) {
    return false;
  }

  // set the rest of the properties for this new page entry
  page->writable = true;
  lock_init(&page->pinning_lock);

  // receive a frame used for user pages from the user pool
  uint8_t* user_frame = get_user_frame();

  if(user_frame) {
    // allocate this user frame into the frame table
    struct frame_entry* frame = allocate_frame(user_frame);
    frame->page = page;

    // map this page to the user frame
    bool mapped = install_page(page->user_page, user_frame, page->writable);
    if(!mapped) {
      // mapping failed, so remove the page from the frame table
      free_frame(user_frame);
      return false;
    }

    // add the page table entry into this process's supplemental page table
    struct thread* current_thread = thread_current();
    lock_acquire(&current_thread->page_table_lock);
    hash_replace(current_thread->page_table, &page->page_entry_elem);
    lock_release(&current_thread->page_table_lock);
  } else {
    free(page);
  }
  return user_frame != NULL;
}

// Abhi driving, return the page entry using a user pool page
struct page_entry* get_page_entry(uint8_t* user_page) {
  struct page_entry page;
  page.user_page = pg_round_down(user_page);

  // receive the element mapping to the user page
  struct hash* page_table = thread_current()->page_table;
  struct hash_elem* page_element = hash_find(page_table,
    &page.page_entry_elem);

  if(page_element) {
    // found the mapped page, so return it as a page entry
    return hash_entry(page_element, struct page_entry, page_entry_elem);
  }
  return NULL;
}

// Pravat driving, return a frame used for user pages from the user pool
uint8_t* get_user_frame() {
  uint8_t* user_frame = palloc_get_page(PAL_USER);

  if(!user_frame) {
    // could not receive a new frame because all frames are used, so evict!
    bool evicted = evict_page();
    if(evicted) {
      // since a page was evicted, now try receiving a user frame
      user_frame = palloc_get_page(PAL_USER);
    } else {
      PANIC("Swap is full, cannot evict without allocating a swap slot!");
    }
  }
  return user_frame;
}

// Abhi driving, allocate this file system page into a frame in the frame table
bool allocate_file_page(struct page_entry* page) {
  lock_acquire(&page->pinning_lock);

  // receive a frame used for user pages from the user pool
  uint8_t* user_frame = get_user_frame();

  if(user_frame) {
    // allocate this user frame into the frame table
    struct frame_entry* frame = allocate_frame(user_frame);
    frame->page = page;

    if(page->read_bytes) {
      // read a page starting at the file offset
      lock_acquire(&filesys_lock);
      file_seek(page->file_ptr, page->file_offset);
      int read_bytes = file_read(page->file_ptr, user_frame, page->read_bytes);
      lock_release(&filesys_lock);

      if(read_bytes != page->read_bytes) {
        // failed to read the bytes, so remove the page from the frame table
        free_frame(user_frame);
        lock_release(&page->pinning_lock);
        return false;
      }
    }

    // set the unread bytes in the user frame to zero
    memset(user_frame + page->read_bytes, 0, page->zero_bytes);

    // map this page to the user frame
    bool mapped = install_page(page->user_page, user_frame, page->writable);
    if(!mapped) {
      // mapping failed, so remove the page from the frame table
      free_frame(user_frame);
      lock_release(&page->pinning_lock);
      return false;
    }
  }
  lock_release(&page->pinning_lock);
  return user_frame != NULL;
}

/*
  Pravat driving, load page from swap then
  allocate it a frame in the frame table
*/
bool allocate_swap_page(struct page_entry* page) {
  lock_acquire(&page->pinning_lock);

  // receive a frame used for user pages from the user pool
  uint8_t* user_frame = get_user_frame();

  if(user_frame) {
    // allocate this user frame into the frame table
    struct frame_entry* frame = allocate_frame(user_frame);
    frame->page = page;

    // map this page to the user frame
    bool mapped = install_page(page->user_page, user_frame, page->writable);
    if(!mapped) {
      // mapping failed, so remove the page from the frame table
      free_frame(user_frame);
      lock_release(&page->pinning_lock);
      return false;
    }

    // load the page from the swap slot into main memory
    load_from_swap(page);
    page->location = MAIN_MEMORY;
  }
  lock_release(&page->pinning_lock);
  return user_frame != NULL;
}

/*
  Dinesh driving
  handle a page fault from the provided faulted address, which
  is the virtual address that was accessed to cause the fault
*/
bool handle_faulted_page(uint8_t* fault_addr, uint32_t *esp) {
  // check the bottom and top of stack to determine if it's a stack access
  size_t page_size = PHYS_BASE - (pg_round_down(fault_addr));
  const int PUSHA_BYTES = 32;
  bool stack_top = ((uint32_t*) fault_addr >= (esp - PUSHA_BYTES));
  bool stack_access = (page_size <= STACK_LIMIT) && stack_top;

  struct thread* current_thread = thread_current();
  if(current_thread->page_table) {
    // receive the page entry for this page
    struct page_entry* page = get_page_entry(fault_addr);
    if(page && page->location == FILE_SYSTEM) {
      // page in file system, allocate the faulted page a frame
      return allocate_file_page(page);
    } else if(page && page->location == SWAP_SLOT) {
      // page in swap (on disk), load it from swap and allocate it a frame
      return allocate_swap_page(page);
    } else if(!page && stack_access) {
      // grow the stack because the page faulted above the stack pointer
      return grow_stack(fault_addr);
    }
  }
  return false;
}
