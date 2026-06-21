.text
.org T8030_POST_IMAGE_LOAD_RESUME - USBLITER8CTL_LINK_BASE
post_image_load_resume:

.org USBLITER8CTL_ORIGIN_OFFSET

.globl _start
_start:
    // Patch staged iBoot file offset 0x39c0: bl autoboot_dispatcher -> nop.
    movz x9, #T8030_IBOOT_AUTOBOOT_CALL_0
    movk x9, #T8030_IBOOT_AUTOBOOT_CALL_16, lsl #16
    movk x9, #T8030_IBOOT_AUTOBOOT_CALL_32, lsl #32

    movz w10, #0x201f
    movk w10, #0xd503, lsl #16

    str w10, [x9]
    dc cvac, x9
    dsb sy
    ic ivau, x9
    dsb sy
    isb
    b post_image_load_resume

.globl _shellcode_end
_shellcode_end:
