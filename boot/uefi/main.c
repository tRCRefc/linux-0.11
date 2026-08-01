#include "efi.h"
#include <asm/boot.h>
#include <asm/serial.h>

#define MEMORY_MAP_EXTRA_DESCRIPTORS 8
#define MEMORY_MAP_MAX_ATTEMPTS 4
#define EXIT_BOOT_SERVICES_MAX_ATTEMPTS 4

struct boot_memory_map {
    struct efi_memory_descriptor *descriptors;
    efi_uintn_t size;
    efi_uintn_t capacity;
    efi_uintn_t key;
    efi_uintn_t descriptor_size;
    efi_uint32_t descriptor_version;
};

static struct boot_info kernel_boot_info;

void __attribute__((noreturn))
x86_64_start(const struct boot_info *boot_info);

static efi_char16_t console_message[] = {
    'L', 'i', 'n', 'u', 'x', ' ', '0', '.', '1', '1', ' ',
    'x', '8', '6', '-', '6', '4', ':', ' ',
    'U', 'E', 'F', 'I', ' ', 'c', 'o', 'n', 's', 'o', 'l', 'e', ' ',
    'i', 's', ' ', 'a', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e',
    '\r', '\n', 0
};

static efi_status_t release_memory_map(
    struct efi_boot_services *boot_services,
    struct boot_memory_map *memory_map)
{
    efi_status_t status;

    if (memory_map->descriptors == 0)
        return EFI_SUCCESS;

    status = boot_services->free_pool(memory_map->descriptors);
    if (!EFI_ERROR(status)) {
        memory_map->descriptors = 0;
        memory_map->size = 0;
        memory_map->capacity = 0;
    }

    return status;
}

static efi_status_t refresh_memory_map(
    struct efi_boot_services *boot_services,
    struct boot_memory_map *memory_map)
{
    efi_uintn_t required_size;
    efi_uintn_t allocation_size;
    efi_uintn_t attempt;
    efi_status_t status;

    for (attempt = 0; attempt < MEMORY_MAP_MAX_ATTEMPTS; ++attempt) {
        memory_map->size = memory_map->capacity;
        status = boot_services->get_memory_map(
            &memory_map->size, memory_map->descriptors, &memory_map->key,
            &memory_map->descriptor_size,
            &memory_map->descriptor_version);
        if (!EFI_ERROR(status)) {
            if (memory_map->descriptor_size <
                    sizeof(struct efi_memory_descriptor) ||
                memory_map->size % memory_map->descriptor_size != 0)
                return EFI_LOAD_ERROR;

            return EFI_SUCCESS;
        }

        if (status != EFI_BUFFER_TOO_SMALL)
            return status;

        required_size = memory_map->size;
        if (memory_map->descriptor_size <
            sizeof(struct efi_memory_descriptor))
            return EFI_LOAD_ERROR;

        status = release_memory_map(boot_services, memory_map);
        if (EFI_ERROR(status))
            return status;

        if (memory_map->descriptor_size >
                (~(efi_uintn_t)0) / MEMORY_MAP_EXTRA_DESCRIPTORS ||
            required_size >
                (~(efi_uintn_t)0) -
                    memory_map->descriptor_size *
                        MEMORY_MAP_EXTRA_DESCRIPTORS)
            return EFI_OUT_OF_RESOURCES;

        allocation_size = required_size +
                          memory_map->descriptor_size *
                              MEMORY_MAP_EXTRA_DESCRIPTORS;
        status = boot_services->allocate_pool(EFI_LOADER_DATA,
                                               allocation_size,
                                               (void **)&memory_map->descriptors);
        if (EFI_ERROR(status))
            return status;

        memory_map->capacity = allocation_size;
    }

    return EFI_BUFFER_TOO_SMALL;
}

static void print_memory_map(const struct boot_memory_map *memory_map)
{
    efi_uintn_t entry_count;
    efi_uintn_t entry;
    efi_uint64_t conventional_pages = 0;

    entry_count = memory_map->size / memory_map->descriptor_size;
    for (entry = 0; entry < entry_count; ++entry) {
        struct efi_memory_descriptor *descriptor;

        descriptor = (struct efi_memory_descriptor *)(
            (efi_uint8_t *)memory_map->descriptors +
            entry * memory_map->descriptor_size);
        if (descriptor->type == EFI_CONVENTIONAL_MEMORY)
            conventional_pages += descriptor->number_of_pages;
    }

    serial_write("UEFI memory map: entries=");
    serial_write_uint64(entry_count);
    serial_write("\r\nUEFI descriptor size: ");
    serial_write_uint64(memory_map->descriptor_size);
    serial_write("\r\nUEFI conventional pages: ");
    serial_write_uint64(conventional_pages);
    serial_write("\r\n");
}

static efi_status_t leave_boot_services(
    efi_handle_t image_handle,
    struct efi_boot_services *boot_services,
    struct boot_memory_map *memory_map)
{
    efi_uintn_t attempt;
    efi_status_t status;

    for (attempt = 0; attempt < EXIT_BOOT_SERVICES_MAX_ATTEMPTS; ++attempt) {
        status = boot_services->exit_boot_services(image_handle,
                                                   memory_map->key);
        if (!EFI_ERROR(status))
            return EFI_SUCCESS;
        if (status != EFI_INVALID_PARAMETER)
            return status;

        status = refresh_memory_map(boot_services, memory_map);
        if (EFI_ERROR(status))
            return status;
    }

    return EFI_INVALID_PARAMETER;
}

static __attribute__((noreturn)) void handoff_to_kernel(
    const struct boot_memory_map *memory_map)
{
    __asm__ volatile ("cli" : : : "memory");

    kernel_boot_info.memory_map = memory_map->descriptors;
    kernel_boot_info.memory_map_size = memory_map->size;
    kernel_boot_info.memory_descriptor_size = memory_map->descriptor_size;
    kernel_boot_info.memory_descriptor_version =
        memory_map->descriptor_version;

    serial_write("UEFI boot services exited\r\n");
    x86_64_start(&kernel_boot_info);
}

efi_status_t EFIAPI efi_main(efi_handle_t image_handle,
                             struct efi_system_table *system_table)
{
    struct boot_memory_map memory_map = {0};
    struct efi_boot_services *boot_services;
    efi_status_t status;

    serial_init();
    serial_write("Linux 0.11 x86-64: efi_main reached\r\n");

    if (system_table == 0 || system_table->boot_services == 0) {
        serial_write("UEFI system table is unavailable\r\n");
        return EFI_INVALID_PARAMETER;
    }

    boot_services = system_table->boot_services;
    if (system_table->con_out != 0) {
        system_table->con_out->output_string(system_table->con_out,
                                             console_message);
    }

    status = refresh_memory_map(boot_services, &memory_map);
    if (EFI_ERROR(status)) {
        serial_write("UEFI memory map failed: ");
        serial_write_hex64(status);
        serial_write("\r\n");
        return status;
    }

    print_memory_map(&memory_map);
    status = leave_boot_services(image_handle, boot_services, &memory_map);
    if (EFI_ERROR(status)) {
        efi_status_t free_status = release_memory_map(boot_services,
                                                      &memory_map);

        serial_write("ExitBootServices failed: ");
        serial_write_hex64(status);
        serial_write("\r\n");
        if (EFI_ERROR(free_status))
            return free_status;
        return status;
    }

    handoff_to_kernel(&memory_map);
}
