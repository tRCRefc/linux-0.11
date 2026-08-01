#ifndef X86_64_BOOT_UEFI_EFI_H
#define X86_64_BOOT_UEFI_EFI_H

typedef __UINT8_TYPE__ efi_uint8_t;
typedef __UINT16_TYPE__ efi_uint16_t;
typedef efi_uint16_t efi_char16_t;
typedef __UINT32_TYPE__ efi_uint32_t;
typedef __UINT64_TYPE__ efi_uint64_t;
typedef efi_uint64_t efi_status_t;
typedef void *efi_handle_t;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS ((efi_status_t)0)

struct efi_table_header {
    efi_uint64_t signature;
    efi_uint32_t revision;
    efi_uint32_t header_size;
    efi_uint32_t crc32;
    efi_uint32_t reserved;
};

struct efi_simple_text_output_protocol;

typedef efi_status_t(EFIAPI *efi_text_output_string_t)(
    struct efi_simple_text_output_protocol *self,
    efi_char16_t *string);

struct efi_simple_text_output_protocol {
    void *reset;
    efi_text_output_string_t output_string;
};

struct efi_system_table {
    struct efi_table_header header;
    efi_char16_t *firmware_vendor;
    efi_uint32_t firmware_revision;
    efi_uint32_t padding;
    efi_handle_t console_in_handle;
    void *con_in;
    efi_handle_t console_out_handle;
    struct efi_simple_text_output_protocol *con_out;
};

#endif
