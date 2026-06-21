.text
.org USBLITER8CTL_ORIGIN_OFFSET

.globl _start
_start:
    bl consolidate_security

.globl _shellcode_end
_shellcode_end:

.org T8030_CONSOLIDATE_SECURITY - USBLITER8CTL_LINK_BASE
consolidate_security:
