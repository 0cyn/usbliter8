.text
.org USBLITER8CTL_ORIGIN_OFFSET

.globl _start
_start:
    b iboot_recovery_stub

.globl _shellcode_end
_shellcode_end:

.org T8030_IBOOT_RECOVERY_STUB - USBLITER8CTL_LINK_BASE
iboot_recovery_stub:
