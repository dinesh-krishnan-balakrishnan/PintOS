#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "lib/user/syscall.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"
static void syscall_handler (struct intr_frame *);

// lock necessary for synchronization of the file system
extern struct lock filesys_lock;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&filesys_lock);
}

/*
  Dinesh driving here
  check if an address is within a user program's own
  virtual memory in order to prevent page faults.
  https://piazza.com/class/k5iivwicu0kvg?cid=726
*/
void valid_address(void* address) {
  struct thread* current_thread = thread_current();
  if(address == NULL || !is_user_vaddr(address) ||
    !pagedir_get_page(current_thread->pagedir, address)) {
    // invalid address, so exit with an error
    exit(-1);
  }
}

// Pravat driving here checks if the buffer is valid
void valid_buffer(void* buffer, int size) {
  char* buf_ptr = (char*) buffer;
  int buffer_index = 0;
  while(buffer_index < size) {
    valid_address(buffer + buffer_index);
    buffer_index++;
  }
}

// Pravat driving here checks if the read buffer is valid
void valid_read_buffer(void* buffer, int size) {
  char* buf_ptr = (char*) buffer;
  int buffer_index = 0;
  while(buffer_index < size) {
    void* buffer_address = buffer + buffer_index;
    if(!buffer_address || !is_user_vaddr(buffer_address)) {
      exit(-1);
    }
    buffer_index++;
  }

  /*
    validate that there exists a page for this buffer
    https://piazza.com/class/k5iivwicu0kvg?cid=1065
  */
  struct page_entry* page = get_page_entry(buffer);
  uint8_t* STK_SC = 0xbfff7f9c;
  if(!page && buffer != STK_SC) {
    exit(-1);
  }
}

// Abhi driving here, check if a file descriptor is within the valid range
void valid_fd(int fd) {
  if(fd < 0 || fd >= FILES_MAX) {
    exit(-1);
  }
}

// handle the system calls
void
syscall_handler (struct intr_frame *f)
{
  // Abhi driving here
  int* esp_pointer = f->esp;
  valid_address(esp_pointer);
  int sys_call_num = *esp_pointer;

  // receive all possible arguments for particular syscalls that use them
  int* arg1 = esp_pointer + 1;
  int* arg2 = esp_pointer + 2;
  int* arg3 = esp_pointer + 3;

  // Abhi driving here, call the particular syscall based on the stack pointer
  uint32_t* return_value;
  switch(sys_call_num) {
  	case SYS_HALT:
  		halt();
  		break;
  	case SYS_EXIT:
      valid_address(arg1);
  		exit(*arg1);
  		break;
    case SYS_EXEC:
      valid_address(arg1);
  		*return_value = exec(*arg1);
  		break;
  	case SYS_WAIT:
      valid_address(arg1);
  		*return_value = wait(*arg1);
  		break;
  	case SYS_CREATE:
      valid_address(arg2);
  		*return_value = create(*arg1, *arg2);
  		break;
  	case SYS_REMOVE:
      valid_address(arg1);
  		*return_value = remove(*arg1);
  		break;
  	case SYS_OPEN:
      valid_address(arg1);
  		*return_value = open(*arg1);
  		break;
  	case SYS_FILESIZE:
      valid_address(arg1);
  		*return_value = filesize(*arg1);
  		break;
  	case SYS_READ:
      valid_address(arg1);
      valid_address(arg3);
  		*return_value = read(*arg1, *arg2, *arg3);
  		break;
  	case SYS_WRITE:
      valid_address(arg1);
      valid_address(arg3);
  		*return_value = write(*arg1, *arg2, *arg3);
  		break;
  	case SYS_SEEK:
      valid_address(arg1);
      valid_address(arg2);
  		seek(*arg1, *arg2);
  		break;
  	case SYS_TELL:
      valid_address(arg1);
  		*return_value = tell(*arg1);
  		break;
  	case SYS_CLOSE:
      valid_address(arg1);
  		close(*arg1);
  		break;
    default:
      // failure, an improper syscall number so let's exit this thread
      thread_exit ();
  }
  if(return_value) {
    // there existed a return value for this syscall, so set the %eax register
    f->eax = *return_value;
  }
}

// turn off the entire system
void halt(void){
	shutdown_power_off();
}

// Pravat driving here, return the file name of a command line
char* get_file_name(char* cmd_line) {
  // copy the command line string
  int size = strlen(cmd_line) + 1;
  char* file_name = malloc(size);
  strlcpy(file_name, cmd_line, size);

  // receive the first argument (the file name) from the copied string
  char* file_name_ptr;
  file_name = strtok_r(file_name, " ", &file_name_ptr);
  return file_name;
}

/*
  Dinesh driving here
  exit this running process, then set the exit status for
  the currently running thread (abstracted process)
*/
void exit(int status){
  struct thread* current_thread = thread_current();

  /* prevent this child from exiting to prevent race conditions where
    other children are exiting simutaneously and causing errors */
  if(current_thread->loaded) {
    sema_down(&current_thread->exit_allowed);
  }

  if(!lock_held_by_current_thread(&filesys_lock)) {
    lock_acquire(&filesys_lock);
  }

  // if this thread has a parent thread, then let its parent know the exit
  if(current_thread->waiting_parent) {
    current_thread->waiting_parent->exit = status;
  }

  // now exit this thread and close the file
  char* file_name = get_file_name(current_thread->name);
  file_close(current_thread->executing);
  printf("%s: exit(%d)\n", file_name, status);
  lock_release(&filesys_lock);
  thread_exit();
}

// Abhi driving here, execute a program in this running process
pid_t exec(const char *cmd_line){
  valid_address(cmd_line);
  if(cmd_line == NULL) {
    return -1;
  }
  return process_execute(cmd_line);
}

// Pravat driving here, wait for a process with the specified pid
int wait(pid_t pid){
  return process_wait(pid);
}

// Pravat driving here, return whether or not the file was created
bool create(const char* file, unsigned initial_size){
  valid_address(file);
  lock_acquire(&filesys_lock);
  bool created = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return created;
}

// Abhi driving here, return whether or not the file was removed
bool remove(const char* file){
  valid_address(file);
  lock_acquire(&filesys_lock);
  bool removed = filesys_remove(file) != NULL ? true : false;
  lock_release(&filesys_lock);
  return removed;
}

/* Dinesh driving here, open a file in this process
  and return the file's file descriptor */
int open(const char* file){
  valid_address(file);
  lock_acquire(&filesys_lock);
  struct thread* current_thread = thread_current();

  struct file* file_ptr = filesys_open(file);
  int fd = -1;
  if(file_ptr) {
    // add this file into the files Array
    int index = 2;
    const int FILE_ARR_LEN = 130;
    while(index < FILE_ARR_LEN && current_thread->files[index] != NULL)
    	index++;
    if(current_thread->files[index] == NULL){
      current_thread->files[index] = file_ptr;
      fd = index;
    }
  }
  lock_release(&filesys_lock);
	return fd;
}

/* Pravat driving here, return the file size
  of a file using its file descriptor */
int filesize(int fd){
  valid_fd(fd);
  lock_acquire(&filesys_lock);

  // receive the file at the index, then receive the size
  struct file* file_ptr = thread_current()->files[fd];
  int file_size = -1;
  if(file_ptr) {
    file_size = file_length(file_ptr);
  }

  lock_release(&filesys_lock);
  return file_size;
}

// Pravat driving, read through a jagged page buffer instead of a whole buffer
int file_jagged_read(struct file* file_ptr, void* buffer,
  unsigned size, unsigned buffer_size) {
  // keep reading the bytes until finally reading size number of bytes
  int read_bytes = 0;
  while(read_bytes < size) {
    int bytes_read = file_read(file_ptr, buffer, buffer_size);
    if(bytes_read == buffer_size) {
      // read the correct number of bytes, so update the buffer
      read_bytes += bytes_read;
      buffer += bytes_read;

      // make sure to not read more than a page
      int remaining_read_bytes = size - read_bytes;
      if(remaining_read_bytes <= PGSIZE) {
        buffer_size = remaining_read_bytes;
      } else {
        buffer_size = PGSIZE;
      }
    } else {
      // stop the loop, could not read enough bytes
      read_bytes = size;
    }
  }
  return read_bytes;
}

/* Abhi driving here, read size bytes from the
  file identified by the file descriptor */
int read(int fd, void* buffer, unsigned size){
  valid_read_buffer(buffer, size);
  valid_fd(fd);
  lock_acquire(&filesys_lock);

  void* page_buffer = pg_round_down(buffer);
  unsigned int buffer_size = (unsigned int) (page_buffer + PGSIZE - buffer);

  // check each page of the buffer and grow stack if necessary
  int stack_grows = 0;
  const int MAX_READ_BYTES = buffer + size;
  while(page_buffer <= MAX_READ_BYTES) {
    struct page_entry* page = get_page_entry(page_buffer);
    if(!page) {
      // need to increase the stack because there aren't enough pages
      stack_grows++;
      grow_stack(page_buffer);
    }
    page_buffer += PGSIZE;
  }

  // Dinesh driving, receive the file at the index
  int read_bytes = 0;
  struct file* file_ptr = thread_current()->files[fd];
  if(file_ptr) {
    if(fd == STDIN_FILENO) {
      while(stack_grows < size) {
        // read from the keyboard inputs
        uint8_t* keyboard_input_read = buffer + stack_grows;
        *keyboard_input_read = input_getc();
        stack_grows++;
        read_bytes++;
      }
    } else if(size > buffer_size) {
      // only read up until the buffer size to prevent going out of bounds
      read_bytes = file_jagged_read(file_ptr, buffer, size, buffer_size);
    } else {
      // read the file normally, without reading through the pages
      read_bytes = file_read(file_ptr, buffer, size);
    }
  }

  lock_release(&filesys_lock);
  return read_bytes;
}

/* Dinesh driving here, write the buffer
  into the terminal or write into a file */
int write(int fd, const void* buffer, unsigned size){
  valid_buffer(buffer, size);
  valid_address(buffer);
  valid_fd(fd);
  if(fd == STDOUT_FILENO) {
    // simply putbuf into the console, do not write into an actual file
    putbuf(buffer, size);
    return size;
  }
  lock_acquire(&filesys_lock);

  // write size number of bytes into a file that this process owns
  struct file* file_ptr = thread_current()->files[fd];
  int write_bytes = 0;
  if(file_ptr) {
    write_bytes = file_write(file_ptr, buffer, size);
  }

  lock_release(&filesys_lock);
  return write_bytes;
}

/* Pravat driving here, change the next byte to
  be read or written at the file descriptor */
void seek(int fd, unsigned position){
  valid_fd(fd);
  lock_acquire(&filesys_lock);

  // seek the file
  struct thread* current_thread = thread_current();
  struct file* file_ptr = current_thread->files[fd];
  if(file_ptr) {
    file_seek(file_ptr, position);
  }

  lock_release(&filesys_lock);
}

/* Abhi driving here, return the position of the
  next byte to be written at the file descriptor */
unsigned tell(int fd){
  valid_fd(fd);
  lock_acquire(&filesys_lock);

  // tell the file
  struct thread* current_thread = thread_current();
  struct file* file_ptr = current_thread->files[fd];
  int next_position = 0;
  if(file_ptr) {
    next_position = file_tell(file_ptr);
  }

  lock_release(&filesys_lock);
  return next_position;
}

// Dinesh driving here, close a file at the file descriptor
void close(int fd){
  valid_fd(fd);
  lock_acquire(&filesys_lock);

  // close the file, then null it out
  struct thread* current_thread = thread_current();
  struct file* file_ptr = current_thread->files[fd];
  if(file_ptr) {
    file_close(file_ptr);
    current_thread->files[fd] = NULL;
  }

  lock_release(&filesys_lock);
}
