/*
 * Fake ELF32 i386 interpreter.
 * To enable place ld-linux.so.2 under /lib directory
 * and create `i386' tchroot environment.
 */

#include <asm/unistd.h>
#include <linux/auxvec.h>

	.section .rodata

.Lexec:
	.ascii "/usr/bin/"
.Larg0:
	.string "tchroot"
.Larg1:
	.string "-p"
.Larg2:
	.string "-e"
.Larg4:
	.string "i386"
.Larg5:
	.string "--"

	.text

.globl _start
	.type _start, @function

_start:
	/* Set 32-bit personality */
	mov $__NR_personality, %eax
	mov $8, %ebx /* PER_LINUX32 */
	int $0x80
	cmp $0, %eax
	jg .Lfail

	/* Locate envp on stack and save it in %edx */
	xor %eax, %eax
	lea -1(%eax), %ecx
	lea 4(%esp), %edi
	repne scasl
	mov %edi, %edx

	/* Start prepending argv with tchroot stuff */
	movl $.Larg5, (%esp)
	pushl $.Larg4

	/* Locate auxv on stack */
	repne scasl

.Lnext_aux:
	/* Search AT_EXECFN */
	cmp $AT_NULL, (%edi)
	je .Lno_execfn
	add $8, %edi
	cmp $AT_EXECFN, -8(%edi)
	jne .Lnext_aux
	mov -4(%edi), %eax
	push %eax
	pushl $.Larg2
.Lno_execfn:

	/* Finish command line */
	pushl $.Larg1
	pushl $.Larg0
	mov %esp, %ecx

	/* Bang! */
	mov $.Lexec, %ebx
	mov $__NR_execve, %eax
	int $0x80

.Lfail:
	/* Explicitly call exit in case of an error */
	neg %eax
	mov %eax, %ebx
	mov $__NR_exit, %eax
	int $0x80

	.size _start, .-_start
