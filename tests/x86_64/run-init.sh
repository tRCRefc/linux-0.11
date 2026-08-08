#!/bin/sh
set -eu

qemu=$1
ovmf_code=$2
ovmf_vars_template=$3
ovmf_vars=$4
esp_image=$5
root_image=$6
serial_log=${esp_image%/*}/init-serial.log

cp "$ovmf_vars_template" "$ovmf_vars"
root_checksum=$(cksum "$root_image")

set +e
timeout 10s "$qemu" \
    -drive "if=pflash,format=raw,readonly=on,file=$ovmf_code" \
    -drive "if=pflash,format=raw,file=$ovmf_vars" \
    -drive "if=ide,index=0,format=raw,file=$esp_image" \
    -drive "if=ide,index=1,format=raw,file=$root_image" \
    -serial stdio -display none -monitor none -no-reboot -no-shutdown \
    >"$serial_log" 2>&1
status=$?
set -e

if [ "$(cksum "$root_image")" != "$root_checksum" ]; then
    echo "init test: root image was modified" >&2
    exit 1
fi

sed -n '1,240p' "$serial_log"

if [ "$status" -ne 124 ]; then
    echo "init test: unexpected QEMU status $status" >&2
    exit 1
fi

if grep -Eq 'init failed|cannot create init|init returned control' "$serial_log"; then
    echo "init test: failure marker found" >&2
    exit 1
fi

for marker in \
    'PID 1 created from /bin/init' \
    'Kernel panic: init exited'
do
    if ! grep -Fq "$marker" "$serial_log"; then
        echo "init test: missing marker: $marker" >&2
        exit 1
    fi
done
