// T8030 ROM

#define WITH_PAC    (1)

#define HANDLE_USB_REQ              0x10000EF34
#define FINISH_USB_TRANSFER         0x10000EC38
#define PLATFORM_DEMOTE             0x10000832C
#define PLATFORM_SET_REMOTE_BOOT    0x100006EB0
#define PANIC                       0x100008F90 // Debugging for broke people
#define DFU_BASE                    0x19c030000

#define ROM_BASE                    0x100000000
#define ROM_WRITE_ALIAS             0x100100000
#define ROM_SIZE                    0x30000

#define MAIN_TASK_STACK_LR      0x19C01DF08
#define JUMP_AWAY               0x1000021D0
