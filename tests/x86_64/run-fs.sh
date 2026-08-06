#!/bin/sh
set -eu

qemu=$1
ovmf_code=$2
ovmf_vars_template=$3
ovmf_vars=$4
esp_image=$5
root_image=$6
serial_log=${esp_image%/*}/fs-serial.log

cp "$ovmf_vars_template" "$ovmf_vars"

set +e
timeout 20s "$qemu" \
    -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code" \
    -drive "if=pflash,format=raw,file=$ovmf_vars" \
    -drive "format=raw,file=$esp_image" \
    -device "loader,file=$root_image,addr=0x04000000,force-raw=on" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -serial stdio -display none -monitor none -no-reboot -no-shutdown \
    >"$serial_log" 2>&1
status=$?
set -e

sed -n '1,240p' "$serial_log"

if [ "$status" -ne 33 ]; then
    echo "filesystem test: unexpected QEMU status $status" >&2
    exit 1
fi

if grep -Eq 'FS TEST FAIL|Kernel panic' "$serial_log"; then
    echo "filesystem test: failure marker found" >&2
    exit 1
fi

for marker in \
    'FS PASS: ramdisk bounds' \
    'FS PASS: mount Minix root' \
    'FS PASS: read /bin/init' \
    'FS TEST COMPLETE'
do
    if ! grep -Fq "$marker" "$serial_log"; then
        echo "filesystem test: missing marker: $marker" >&2
        exit 1
    fi
done
