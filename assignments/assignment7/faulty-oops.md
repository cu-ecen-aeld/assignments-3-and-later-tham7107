# Analysis of /dev/faulty error for ECEA 5306 Assignment 7, Part 2
Below is the output on the console showing the result of executing `echo “hello_world” > /dev/faulty` on the console of the running buildroot image in ECEA 5306 Assignment 7, Part 2:
```
Welcome to Buildroot
buildroot login: root
Password:
# echo “hello_world” > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=00000000420a1000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 156 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008c83d80
x29: ffffffc008c83d80 x28: ffffff80020da640 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 0000000000000012 x21: 000000555c792a70
x20: 000000555c792a70 x19: ffffff8002088d00 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008c83df0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
---[ end trace c4d185841b13b49c ]---

Welcome to Buildroot
buildroot login:
```
The first line "Unable to handle kernel NULL pointer dereference" makes it clear that the fault is a null dereference.  The next several lines ("Mem abort info" through "Data abort info, CM=0, Wnr=1") gives information about the exception.  Next follows VM and page table information.

We can see the famous "Oops" error here: `Internal error: Oops: 96000045 [#1] SMP`.  That is followed by a list of the loaded modules (hello, faulty, and scull), and a reference to the CPU on which the fault occured (0), and the pid and processes name that triggered it (156, sh), an indication that the kernel is tainted by the loaded module, and the name of the hardware ("virt" is the VM type emulated by QEMU).

Next we have CPU register information, followed by a stack trace.  The most important thing to note here is the line `pc : faulty_write+0x14/0x20 [faulty]`.  This tells us the error occured in the module `faulty`, and the routine `faulty_write`.  The fault PC is at `faulty_write+0x14`.  `/0x20` tells us the routine is 0x20 bytes long.

We can use objdump to disassemble the bad code:
```
tga@ecea530x:~/ecea5305/assignment-5-tham7107$ buildroot/output/host/bin/aarch64-buildroot-linux-uclibc-objdump -S buildroot/output/target/lib/modules/5.15.18/extra/faulty.ko 

buildroot/output/target/lib/modules/5.15.18/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:	d503245f 	bti	c
   4:	d2800001 	mov	x1, #0x0                   	// #0
   8:	d2800000 	mov	x0, #0x0                   	// #0
   c:	d503233f 	paciasp
  10:	d50323bf 	autiasp
  14:	b900003f 	str	wzr, [x1]
  18:	d65f03c0 	ret
  1c:	d503201f 	nop

0000000000000020 <faulty_read>:
  20:	d503233f 	paciasp
  ```
Note, -S is not displaying source, as the module was compiled without `-g`.  However, we can clearly confirm the following:
* The routine `faulty_write` is clearly 0x20 bytes long (starts at 0x0, last instruction at 0x1c, and `faulty_read` starts at 0x20).
* The register x1 is loaded with 0 at instruction address 0x4
* The store at 0x14 (the fault address) is attempting to write 0 (wzr, the zero register) to the address pointed to by x1, loaded 4 instructions above with 0.

Thus, the store at 0x14 is our `*(int *)0 = 0;` in `faulty_write` at `faulty.c:53`