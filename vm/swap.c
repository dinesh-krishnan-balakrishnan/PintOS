#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

/*
  Pravat driving, the number of blocks in a page
  https://piazza.com/class/k5iivwicu0kvg?cid=994
*/
const int BLOCKS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

// Abhi driving, initialize the swap table
void initialize_swap_table() {
  // initialize the swap variables
  block_device = block_get_role(BLOCK_SWAP);
  swap_table = bitmap_create(block_size(block_device));
  lock_init(&swap_lock);
}

// Abhi driving, load a page from main memory into a swap slot
block_sector_t load_to_swap(struct page_entry* page) {
  lock_acquire(&swap_lock);
  uint8_t* user_page = page->user_page;
  int bitmap_index = bitmap_scan_and_flip(swap_table, 0, BLOCKS_PER_PAGE, 0);
  page->swap_slot = bitmap_index * BLOCKS_PER_PAGE;

  // write to each block only if there is space in the swap
  if(bitmap_index != BITMAP_ERROR) {
    int block_index = bitmap_index;
    int write_index = 0;
    while(write_index != BLOCKS_PER_PAGE) {
      block_write(block_device, block_index, user_page);

      // go to the next block of the page to add into swap
      user_page += BLOCK_SECTOR_SIZE;
      write_index++;
      block_index++;
    }
  }
  lock_release(&swap_lock);
  return bitmap_index;
}

/*
 Dinesh driving, receive the page from a swap
 slot and then load it into main memory
*/
void load_from_swap(struct page_entry* page) {
  lock_acquire(&swap_lock);
  block_sector_t swap_slot = page->swap_slot;
  uint8_t* user_page = page->user_page;
  bitmap_reset(swap_table, swap_slot / BLOCKS_PER_PAGE);

  // read each block of this page
  int block_index = swap_slot;
  int read_index = 0;
  while(read_index != BLOCKS_PER_PAGE) {
    block_read(block_device, block_index, user_page);

    // go to the next block of the page to add into swap
    user_page += BLOCK_SECTOR_SIZE;
    block_index++;
    read_index++;
  }
  lock_release(&swap_lock);
}
