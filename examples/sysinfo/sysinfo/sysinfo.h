#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHIP_FAMILY_RP2040 = 0,
    CHIP_FAMILY_RP2350 = 1,
    CHIP_FAMILY_UNKNOWN = 0xFF
} chip_family_t;

typedef enum {
    ARCH_CORTEX_M0PLUS = 0,
    ARCH_CORTEX_M33 = 1,
    ARCH_HAZARD3_RISCV = 2,
    ARCH_UNKNOWN = 0xFF
} core_arch_t;

typedef struct {
    // Silicon & Target Identification
    chip_family_t family;
    uint16_t chip_id_part; // 12-bit Part ID (0x2040 or 0x2350)
    uint8_t revision_raw;  // Raw 4-bit revision register value
    char stepping_str[4];  // Resolved stepping string ("A0", "A1", "A2", "B0", etc.)
    uint32_t rom_gitrev;   // 32-bit ROM Git commit ID / build ref
    uint8_t rom_version;   // Derived ROM release version
    const char *chip_name; // Resolved target string ("RP2040", "RP2350A", "RP2354B", etc.)

    // Core & Security Architecture
    core_arch_t current_arch; // Active core architecture
    uint8_t active_core_id;   // Executing core ID (0 or 1)
    uint8_t total_cores;      // Total physical cores (2)
    bool is_secure_mode;      // Arm TrustZone execution state (RP2350 M33 only)
    bool secure_boot_en;      // OTP Secure Boot state (RP2350)

    // Memory & Storage Config
    uint32_t sram_size_bytes;  // Total internal SRAM (264 KB or 520 KB)
    bool has_embedded_flash;   // Stacked 2MB internal flash flag (RP2354)
    uint32_t flash_size_bytes; // Flash size in bytes
    uint8_t package_pin_count; // Package pin count (56, 60, or 80)

    // Hardware Unique Identifier
    uint8_t unique_id[8]; // 64-bit unique factory device ID

    // Clock & Power Status
    uint32_t sys_clk_freq_hz; // Active system clock frequency
    bool vreg_ok;             // Core voltage regulator status
    bool rosc_stable;         // Ring oscillator status
} sysinfo_t;

void sysinfo_get(sysinfo_t *info);
void sysinfo_print(const sysinfo_t *info);

#ifdef __cplusplus
}
#endif

#endif // SYSINFO_H