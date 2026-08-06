#!/bin/sh
set -eu

qemu=$1
ovmf_code=$2
ovmf_vars_template=$3
ovmf_vars=$4
esp_image=$5
serial_log=${esp_image%/*}/serial.log

cp "$ovmf_vars_template" "$ovmf_vars"

set +e
timeout 20s "$qemu" \
    -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code" \
    -drive "if=pflash,format=raw,file=$ovmf_vars" \
    -drive "format=raw,file=$esp_image" \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -serial stdio -display none -monitor none -no-reboot -no-shutdown \
    >"$serial_log" 2>&1
status=$?
set -e

sed -n '1,240p' "$serial_log"

if [ "$status" -ne 33 ]; then
    echo "process test: unexpected QEMU status $status" >&2
    exit 1
fi

if grep -Eq 'PROCESS TEST FAIL|Kernel panic' "$serial_log"; then
    echo "process test: failure marker found" >&2
    exit 1
fi

for marker in \
    'PROCESS PASS: anonymous fault' \
    'PROCESS PASS: copy on write' \
    'PROCESS PASS: supervisor access' \
    'PROCESS PASS: user OOM rollback' \
    'PROCESS PASS: fork failure rollback' \
    'PROCESS PASS: nested wait' \
    'PROCESS PASS: repeated lifecycle' \
    'PROCESS TEST COMPLETE'
do
    if ! grep -Fq "$marker" "$serial_log"; then
        echo "process test: missing marker: $marker" >&2
        exit 1
    fi
done
