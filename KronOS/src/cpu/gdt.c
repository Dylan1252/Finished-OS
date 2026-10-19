#include "header/cpu/gdt.h"
#include "header/cpu/interrupt.h"

/**
 * global_descriptor_table, predefined GDT.
 * Initial SegmentDescriptor already set properly according to Intel Manual & OSDev.
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data}, {User Code}, {User Data}, {TSS}].
 */
struct GlobalDescriptorTable global_descriptor_table = {
.table = {
        {
            // Null Descriptor (Index 0)
            .segment_low = 0,
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0,
            .non_system  = 0,
            .dpl         = 0,
            .present     = 0,
            .limit_high  = 0,
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 0,
            .granularity = 0,
            .base_high   = 0
        },
        {
            // Kernel Code Segment (Index 1)
            .segment_low = 0xFFFF,
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0xA, // 0b1010
            .non_system  = 1,
            .dpl         = 0,
            .present     = 1,
            .limit_high  = 0xF, // 0xFFFF (low) + 0xF (high) = 0xFFFFF
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 1,   // 32-bit operand
            .granularity = 1    // 4KB
        },
        {
            // Kernel Data Segment (Index 2)
            .segment_low = 0xFFFF,
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0x2, // 0b0010
            .non_system  = 1,
            .dpl         = 0,
            .present     = 1,
            .limit_high  = 0xF,
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 1,
            .granularity = 1
        },
        {
            // User Code Segment (Index 3)
            .segment_low = 0xFFFF,
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0xA, // 0b1010
            .non_system  = 1,
            .dpl         = 3,
            .present     = 1,
            .limit_high  = 0xF,
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 1,
            .granularity = 1
        },
        {
            // User Data Segment (Index 4)
            .segment_low = 0xFFFF,
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0x2, // 0b0010
            .non_system  = 1,
            .dpl         = 3,
            .present     = 1,
            .limit_high  = 0xF,
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 1,
            .granularity = 1
        },
        {
            // Task State Segment (Index 5)
            .segment_low = sizeof(struct TSSEntry),
            .base_low    = 0,
            .base_mid    = 0,
            .type_bit    = 0x9, // 0b1001 : 32-bit available TSS
            .non_system  = 0,   // S bit
            .dpl         = 0,
            .present     = 1,
            .limit_high  = (sizeof(struct TSSEntry) & (0xF << 16)) >> 16,
            .avl         = 0,
            .l_bit       = 0,
            .d_b         = 1,
            .granularity = 0
        },
        {0}
    }
};

/**
 * _gdt_gdtr, predefined system GDTR.
 * GDT pointed by this variable is already set to point global_descriptor_table above.
 * From: https://wiki.osdev.org/Global_Descriptor_Table, GDTR.size is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    .size    = sizeof(struct GlobalDescriptorTable) - 1,
    .address = &global_descriptor_table
};

void gdt_install_tss(void) {
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    global_descriptor_table.table[5].base_high = (base & (0xFF << 24)) >> 24;
    global_descriptor_table.table[5].base_mid  = (base & (0xFF << 16)) >> 16;
    global_descriptor_table.table[5].base_low  = base & 0xFFFF;
}
