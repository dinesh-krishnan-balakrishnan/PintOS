# PintOS

The following repository contains code developed upon the base PintOS code, with general instructions provided here:
> https://www.cs.utexas.edu/users/ans/classes/cs439/projects/pintos/WWW/pintos.html

## Structure

4 main sections of the OS were developed:

### Thread Manager

The implemented thread manager code can be found in **threads/thread.h** and **threads/thread.c**. The thread manager requires a system timer, which my group implemented in **devices/timer.c**. The thread manager also uses locks and semaphores to control process access, implementations for which can be found in **threads/synch.c**.

### Virtual Memory

Virtual memory was created to allow for process allocation. Pages, page tables, supplementary page tables, and swap slots were created to allow for virtual memory allocation. These implementations can be found in the **vm** directory, in **frame.c**, **page.c**, and **swap.c**.

### User Program Execution

Safely running user programs requires a few steps. Firstly, a simulated disk with file-system partitioning is required. The commands for this can be found in the project website. Methods restricting filesystem access were then implemented in **userprog/process.c**. Virtual addressing and paging allocation algorithms were also implemented, the code for which can be found in **syscall.c**. Lastly, command-line interactions were implemented in **syscall.c** to allow program execution from the command line.

### File System

The NTFS file system was implemented for the current OS. This required writing code to interact with the disk, which also requires running the commands for file-system partioning. Once the disk space is created, the files to create the filesystem can be found in **filesys/directory.c**, **filesys/filesys.c**, and **filesys/file.c**. 

---- 

## Running the PintOS

Instructions for running PintOS can be found in the project website, which is linked in the top of the README.