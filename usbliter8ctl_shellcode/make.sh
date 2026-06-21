#!/bin/bash

set -euo pipefail

cd "$(dirname "$0")"

target=t8030
src_dir="$target"
build_dir="$src_dir/build"
out_dir="$src_dir/payloads"

mkdir -p "$build_dir" "$out_dir"

rom_base=$((0x100000000))
rom_size=$((0x30000))
main_task_stack_lr=$((0x19C01DF08))
nand_boot_device=$((3))
nand_boot_arg=$((0))
nand_boot_flag=$((0))
nand_entry=$((0x100001EF4))
panic=$((0x100008F90))
handler_base=$((rom_base + rom_size - 0x400))
image_load_tag_count_patch=$((0x1000020C4))
post_image_load_patch=$((0x1000021B4))
handoff_patch=$((0x1000021D0))
consolidate_security=$((0x100009614))
load_area=$((0x19C030000))
load_area_size=$((0x358000))

nand_boot_trampoline=$((panic + 4))
nand_boot_target_literal=$((panic + 0x60))
iboot_recovery_stub=$((handler_base + 0x300))
post_image_load_resume=$((post_image_load_patch + 4))
iboot_autoboot_call=$((load_area + 0x39c0))

hex() {
    printf "0x%x" "$1"
}

hex_to_dec() {
    printf "%d" "$((16#$1))"
}

add_mov64_48_defines() {
    local prefix="$1"
    local value="$2"

    defs+=("-D${prefix}_0=$(hex $((value & 0xffff)))")
    defs+=("-D${prefix}_16=$(hex $(((value >> 16) & 0xffff)))")
    defs+=("-D${prefix}_32=$(hex $(((value >> 32) & 0xffff)))")
}

find_symbol() {
    local macho="$1"
    local wanted="$2"
    local address kind name rest

    while read -r address kind name rest; do
        if [ "$name" = "$wanted" ]; then
            printf "%s\n" "$address"
            return 0
        fi
    done < <(nm "$macho")

    return 1
}

build_payload() {
    local source="$1"
    local address="$2"
    shift 2

    local stem="${source%.S}"
    local link_base="$address"
    local ref

    for ref in "$@"; do
        if ((ref < link_base)); then
            link_base="$ref"
        fi
    done

    link_base=$((link_base & ~0xf))
    local origin_offset=$((address - link_base))
    local macho="$build_dir/$stem.o"
    local raw="$build_dir/$stem.raw"
    local out="$out_dir/$stem.bin"
    local -a defs=(
        "-DUSBLITER8CTL_LINK_BASE=$(hex "$link_base")"
        "-DUSBLITER8CTL_ORIGIN_OFFSET=$(hex "$origin_offset")"
        "-DT8030_NAND_BOOT_DEVICE=$(hex "$nand_boot_device")"
        "-DT8030_NAND_BOOT_ARG=$(hex "$nand_boot_arg")"
        "-DT8030_NAND_BOOT_FLAG=$(hex "$nand_boot_flag")"
        "-DT8030_NAND_ENTRY=$(hex "$nand_entry")"
        "-DT8030_NAND_BOOT_TARGET_LITERAL=$(hex "$nand_boot_target_literal")"
        "-DT8030_CONSOLIDATE_SECURITY=$(hex "$consolidate_security")"
        "-DT8030_IBOOT_RECOVERY_STUB=$(hex "$iboot_recovery_stub")"
        "-DT8030_POST_IMAGE_LOAD_RESUME=$(hex "$post_image_load_resume")"
    )

    add_mov64_48_defines T8030_MAIN_TASK_STACK_LR "$main_task_stack_lr"
    add_mov64_48_defines T8030_IBOOT_AUTOBOOT_CALL "$iboot_autoboot_call"

    clang \
        -arch arm64 \
        -x assembler-with-cpp \
        -Wl,-preload \
        -Wl,-segalign,0x10 \
        -Wl,-e,_start \
        -Wl,-segaddr,__TEXT,"$(hex "$link_base")" \
        -nostdlib \
        "${defs[@]}" \
        "$src_dir/$source" \
        -o "$macho"

    local start_hex end_hex start end offset length raw_size
    start_hex="$(find_symbol "$macho" _start)" || {
        printf "missing _start in %s\n" "$source" >&2
        exit 1
    }
    end_hex="$(find_symbol "$macho" _shellcode_end)" || {
        printf "missing _shellcode_end in %s\n" "$source" >&2
        exit 1
    }

    start="$(hex_to_dec "$start_hex")"
    end="$(hex_to_dec "$end_hex")"

    if ((start != address)); then
        printf "%s assembled at 0x%x, expected 0x%x\n" "$source" "$start" "$address" >&2
        exit 1
    fi

    if ((end < start)); then
        printf "%s has invalid shellcode bounds\n" "$source" >&2
        exit 1
    fi

    vmacho -f "$macho" "$raw"

    offset=$((start - link_base))
    length=$((end - start))
    raw_size=$(wc -c < "$raw")

    if ((offset + length > raw_size)); then
        printf "%s shellcode exceeds extracted binary\n" "$source" >&2
        exit 1
    fi

    dd if="$raw" of="$out" bs=1 skip="$offset" count="$length" 2>/dev/null
    printf "built %s (%d bytes)\n" "$out" "$length"
}

build_payload nand_trampoline.S "$nand_boot_trampoline"
build_payload image_load_tag_count_patch.S "$image_load_tag_count_patch"
build_payload handoff_patch.S "$handoff_patch" "$consolidate_security"
build_payload post_image_load_hook.S "$post_image_load_patch" "$iboot_recovery_stub"
build_payload iboot_recovery_flow_stub.S "$iboot_recovery_stub" "$post_image_load_resume"
