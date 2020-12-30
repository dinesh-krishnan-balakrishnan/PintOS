#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/off_t.h"
#include <hash.h>

// Dinesh driving, use a lock to control access to the file system
extern struct lock filesys_lock;

// potential locations of a page
#define MAIN_MEMORY 0
#define FILE_SYSTEM 1
#define SWAP_SLOT 2
#define ZERO 3

/*
 the maximum size of the stack is 8MB (8 bytes * 1024000 = 8192000 bytes)
 https://piazza.com/class/k5iivwicu0kvg?cid=1047
*/
#define STACK_LIMIT 8192000

// Pravat driving
struct page_entry {
  // the user page for this page entry
  uint8_t* user_page;

  // a hash element to reference into the supplemental page table
  struct hash_elem page_entry_elem;

  // a lock to prevent evictions when the page is using resources
  struct lock pinning_lock;

  // the file associated with the process that owns this this page
  struct file* file_ptr;

  // the offset for the file from the virtual address
  off_t file_offset;

  // how many bytes to read for the initial part from the file
  off_t read_bytes;

  // how many bytes to zero-out after reading from read_bytes
  off_t zero_bytes;

  // whether or not this page is writable
  bool writable;

  // location of the page (based on the potential locations macros above)
  int location;

  // sector that this page will swap into
  block_sector_t swap_slot;
};

// Abhi driving
hash_hash_func hash_page_func;
hash_less_func hash_page_comparator;
bool grow_stack(uint8_t* user_page);
struct page_entry* get_page_entry(uint8_t* user_page);
uint8_t* get_user_frame();
bool allocate_file_page(struct page_entry* page);
bool allocate_swap_page(struct page_entry* page);
bool handle_faulted_page(uint8_t* fault_addr, uint32_t *esp);
