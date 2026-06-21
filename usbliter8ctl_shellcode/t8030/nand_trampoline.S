.text
.org USBLITER8CTL_ORIGIN_OFFSET

.globl _start
_start:
    ldr x20, boot_device
    ldr x21, boot_arg
    ldr x23, boot_flag
    str w21, [sp, #0x50]
    str w23, [sp, #0x54]
    str w20, [sp, #0x58]
    ldr x16, nand_entry
    br x16

boot_device:
    .quad T8030_NAND_BOOT_DEVICE
boot_arg:
    .quad T8030_NAND_BOOT_ARG
boot_flag:
    .quad T8030_NAND_BOOT_FLAG
nand_entry:
    .quad T8030_NAND_ENTRY

.globl _shellcode_end
_shellcode_end:
