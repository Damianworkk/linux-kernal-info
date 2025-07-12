# Linux Kernel Info

The **Linux Kernel** is the core component of the Linux operating system, responsible for managing hardware, system resources, process scheduling, memory management, and security enforcement. Developed as a **monolithic kernel**, it includes device drivers, networking, file system support, and system calls directly in the kernel space for performance efficiency.

## Key Responsibilities

- Process & Thread Management  
- Virtual Memory and Paging  
- System Call Interface (SCI)  
- I/O Scheduling and Device Drivers  
- Inter-Process Communication (IPC)  
- Networking Stack (IPv4/IPv6, Netfilter, etc.)

## Tech Highlights

- Written in: C and Assembly  
- Modular design with support for dynamically loadable kernel modules  
- Maintained with Git  
- License: GPLv2  
- Supported platforms: x86, ARM, RISC-V, and more

## Commands

- Check Kernel Version: `uname -r`
- View Boot Logs: `dmesg`
- List Modules: `lsmod`, `modinfo`
