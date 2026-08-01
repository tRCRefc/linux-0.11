#ifndef X86_64_BOOT_UEFI_EFI_H
#define X86_64_BOOT_UEFI_EFI_H

typedef __UINT8_TYPE__ efi_uint8_t;
typedef __UINT16_TYPE__ efi_uint16_t;
typedef efi_uint16_t efi_char16_t;
typedef __UINT32_TYPE__ efi_uint32_t;
typedef __UINT64_TYPE__ efi_uint64_t;
typedef __UINTPTR_TYPE__ efi_uintn_t;
typedef efi_uint64_t efi_physical_address_t;
typedef efi_uint64_t efi_virtual_address_t;
typedef efi_uint64_t efi_status_t;
typedef void *efi_handle_t;

#define EFIAPI __attribute__((ms_abi))
#define EFI_ERROR_MASK ((efi_status_t)1 << 63)
#define EFI_ERROR(status) (((status) & EFI_ERROR_MASK) != 0)
#define EFI_SUCCESS ((efi_status_t)0)
#define EFI_LOAD_ERROR (EFI_ERROR_MASK | (efi_status_t)1)
#define EFI_INVALID_PARAMETER (EFI_ERROR_MASK | (efi_status_t)2)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_MASK | (efi_status_t)5)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_MASK | (efi_status_t)9)

#define EFI_LOADER_DATA ((efi_uint32_t)2)
#define EFI_CONVENTIONAL_MEMORY ((efi_uint32_t)7)

struct efi_table_header {
    efi_uint64_t signature;
    efi_uint32_t revision;
    efi_uint32_t header_size;
    efi_uint32_t crc32;
    efi_uint32_t reserved;
};

struct efi_simple_text_output_protocol;
struct efi_boot_services;

struct efi_memory_descriptor {
    efi_uint32_t type;
    efi_uint32_t padding;
    efi_physical_address_t physical_start;
    efi_virtual_address_t virtual_start;
    efi_uint64_t number_of_pages;
    efi_uint64_t attribute;
};

typedef efi_status_t(EFIAPI *efi_text_output_string_t)(
    struct efi_simple_text_output_protocol *self,
    efi_char16_t *string);

struct efi_simple_text_output_protocol {
    void *reset;
    efi_text_output_string_t output_string;
};

typedef efi_status_t(EFIAPI *efi_get_memory_map_t)(
    efi_uintn_t *memory_map_size,
    struct efi_memory_descriptor *memory_map,
    efi_uintn_t *map_key,
    efi_uintn_t *descriptor_size,
    efi_uint32_t *descriptor_version);

typedef efi_status_t(EFIAPI *efi_allocate_pool_t)(
    efi_uint32_t pool_type,
    efi_uintn_t size,
    void **buffer);

typedef efi_status_t(EFIAPI *efi_free_pool_t)(void *buffer);

typedef efi_status_t(EFIAPI *efi_exit_boot_services_t)(
    efi_handle_t image_handle,
    efi_uintn_t map_key);

struct efi_boot_services {
    struct efi_table_header header;
    void *raise_tpl;
    void *restore_tpl;
    void *allocate_pages;
    void *free_pages;
    efi_get_memory_map_t get_memory_map;
    efi_allocate_pool_t allocate_pool;
    efi_free_pool_t free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    void *handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_exit_boot_services_t exit_boot_services;
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
    efi_handle_t standard_error_handle;
    struct efi_simple_text_output_protocol *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
};

_Static_assert(sizeof(struct efi_memory_descriptor) == 40,
               "unexpected EFI memory descriptor layout");
_Static_assert(__builtin_offsetof(struct efi_boot_services, get_memory_map) ==
                   0x38,
               "unexpected EFI GetMemoryMap offset");
_Static_assert(
    __builtin_offsetof(struct efi_boot_services, exit_boot_services) == 0xe8,
    "unexpected EFI ExitBootServices offset");
_Static_assert(__builtin_offsetof(struct efi_system_table, boot_services) ==
                   0x60,
               "unexpected EFI BootServices offset");

#endif
