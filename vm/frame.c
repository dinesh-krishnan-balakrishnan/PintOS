#include <stdio.h>
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

// Abhi driving, initialize the frame table
void initialize_frame_table() {
  list_init(&frame_table);
  lock_init(&frame_lock);
}

// Pravat driving, allocate a frame as a new frame entry
frame_entry* allocate_frame(uint8_t* user_frame) {
  // now that we have the user frame, let's add it into the frame table
  struct frame_entry* frame = malloc(sizeof(struct frame_entry));
  frame->user_frame = user_frame;
  frame->process = thread_current();

  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &frame->frame_entry_elem);
  lock_release(&frame_lock);
  return frame;
}

// Pravat driving, free the specified user frame from a page
void free_frame(uint8_t* user_frame) {
  lock_acquire(&frame_lock);
  struct frame_entry* frame = NULL;
  if(!list_empty(&frame_table)) {
    bool foundFrame = false;
    struct list_elem* frame_iterator = list_front(&frame_table);

    // iterate the list of frame entries to find the frame
    while(!foundFrame && frame_iterator != list_end(&frame_table)) {
      // receive this frame entry
      frame = list_entry(frame_iterator, struct frame_entry, frame_entry_elem);

      // if we found the corresponding frame, then stop the loop
      if(frame->user_frame == user_frame) {
        foundFrame = true;
      } else {
        // go to the next frame
        frame_iterator = frame_iterator->next;
      }
    }
  }
  // if we found a corresponding frame, free its page from the frame table
  if(frame) {
    list_remove(&frame->frame_entry_elem);
    palloc_free_page(frame->user_frame);
  }
  lock_release(&frame_lock);
}

// return the frame to evict its page based on the clock algorithm
struct frame_entry* get_evict_frame() {
  // iterate through every frame to find a page that can be evicted
  struct frame_entry* evict_frame = NULL;
  if(!list_empty(&frame_table)) {
    struct list_elem* frame_iterator = list_front(&frame_table);

    // iterate the list of frame entries to find the frame
    bool stopSearch = false;
    while(!stopSearch && frame_iterator != list_end(&frame_table)) {
      // receive this frame entry, then get its associated page
      struct frame_entry* frame = list_entry(frame_iterator,
        struct frame_entry, frame_entry_elem);
      struct thread* frame_process = frame->process;
      struct page_entry* page = frame->page;

      // check if this page is not pinned, so we can potentially evict it
      if(!page->pinning_lock.holder) {
        bool dirty = pagedir_is_dirty(frame_process->pagedir, page->user_page);

        if(dirty) {
          // must evict this page, so let's stop the loop
          evict_frame = frame;
          stopSearch = true;
        }
      }
      frame_iterator = frame_iterator->next;
    }
  }

  // if we could not find a frame to evict, just evict the first one
  if(!evict_frame) {
    struct list_elem* first_frame_element = list_front(&frame_table);
    evict_frame = list_entry(first_frame_element,
      struct frame_entry, frame_entry_elem);
  }
  return evict_frame;
}

// Dinesh driving, evict a page from the frame table using the clock algorithm
bool evict_page() {
  // receive the best frame to evict its page out of it
  struct frame_entry* evict_frame = get_evict_frame();
  if(evict_frame) {
    struct page_entry* page = evict_frame->page;
    lock_acquire(&page->pinning_lock);

    // now write its page from main memory into swap
    load_to_swap(page);
    page->location = SWAP_SLOT;

    // deattach this frame from the page
    pagedir_clear_page(evict_frame->process->pagedir, page->user_page);

    // free the frame from this page
    lock_acquire(&frame_lock);
    list_remove(&evict_frame->frame_entry_elem);
    lock_release(&frame_lock);
    palloc_free_page(evict_frame->user_frame);

    lock_release(&page->pinning_lock);
  }

  // could not resolve "hanging" issues with eviction, so we gave up
  return false;
}
