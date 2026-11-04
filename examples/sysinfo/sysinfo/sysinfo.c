/**
 * @file sysinfo.c
 * @brief System Information Retrieval and Display
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * This file provides functions to retrieve and display detailed system information for
 * Raspberry Pi Pico (RP2040) and RP2350 series microcontrollers.
 * It includes identification of the chip family, architecture, memory configuration,
 * unique device ID, and clock/power status.
 */

#include "sysinfo.h"
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/sysinfo.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#if PICO_RP2040
#include "hardware/structs/vreg_and_chip_reset.h"
#elif PICO_RP2350
#include "hardware/structs/otp.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/tbman.h"
#endif

// ARM Cortex-M33 TrustZone CONTROL register helper
#if PICO_RP2350 && !defined(__riscv)
static inline uint32_t get_control_reg(void) {
    uint32_t result;
    __asm__ volatile("MRS %0, control" : "=r"(result));
    return result;
}
#endif

void sysinfo_get(sysinfo_t *info) {
    if (!info)
        return;
    memset(info, 0, sizeof(sysinfo_t));

    // 1. Parse raw 32-bit SYSINFO CHIP_ID register
    uint32_t chip_id_reg = sysinfo_hw->chip_id;

    // Bits [31:28] = Revision (4 bits)
    info->revision_raw = (uint8_t)((chip_id_reg >> 28) & 0x0F);

    // Bits [27:12] = Part Number (16 bits) -> 0x2040 or 0x2350
    info->chip_id_part = (uint16_t)((chip_id_reg >> 12) & 0xFFFF);

#if PICO_RP2040
    info->family = CHIP_FAMILY_RP2040;
    info->current_arch = ARCH_CORTEX_M0PLUS;
    info->sram_size_bytes = 264 * 1024;
    info->package_pin_count = 56; // Fixed QFN-56 package
    info->has_embedded_flash = false;
    info->flash_size_bytes = 0;
    info->chip_name = "RP2040";
    info->is_secure_mode = false;
    info->secure_boot_en = false;

    // RP2040 Stepping (B0=1, B1=2, B2=3)
    switch (info->revision_raw) {
    case 1:
        snprintf(info->stepping_str, sizeof(info->stepping_str), "B0");
        break;
    case 2:
        snprintf(info->stepping_str, sizeof(info->stepping_str), "B1");
        break;
    case 3:
        snprintf(info->stepping_str, sizeof(info->stepping_str), "B2");
        break;
    default:
        snprintf(info->stepping_str, sizeof(info->stepping_str), "A0");
        break;
    }

    info->vreg_ok = (vreg_and_chip_reset_hw->vreg & VREG_AND_CHIP_RESET_VREG_RO_OK_BITS) != 0;
    info->rom_gitrev = sysinfo_hw->gitref_rp2040;
    info->rom_version = (uint8_t)(info->rom_gitrev & 0xFF);

#elif PICO_RP2350
    info->family = CHIP_FAMILY_RP2350;
    info->sram_size_bytes = 520 * 1024;

// Architecture Detection
#if defined(__riscv)
    info->current_arch = ARCH_HAZARD3_RISCV;
    info->is_secure_mode = false;
#else
    info->current_arch = ARCH_CORTEX_M33;
    info->is_secure_mode = (get_control_reg() & 1) == 0;
#endif

    // Read BootROM version directly from fixed ROM vector address 0x00000013
    uint8_t bootrom_ver = *(volatile uint8_t *)(0x00000013);

    // Silicon & BootROM Stepping resolution:
    // Rev 0=A0, 1=A1, 2=A2, 3=A3 (or A4 if BootROM ver >= 4)
    if (info->revision_raw == 3) {
        if (bootrom_ver >= 4) {
            snprintf(info->stepping_str, sizeof(info->stepping_str), "A4");
        } else {
            snprintf(info->stepping_str, sizeof(info->stepping_str), "A3");
        }
    } else {
        snprintf(info->stepping_str, sizeof(info->stepping_str), "A%d", info->revision_raw);
    }

    // Package Pin Count: Target Board / SDK Target check (QFN80 vs QFN60)
#if (defined(PICO_RP2350B) && PICO_RP2350B) || defined(SL_MCU_RP2354)
    bool is_qfn80 = true;
#else
    bool is_qfn80 = false;
#endif
    info->package_pin_count = is_qfn80 ? 80 : 60;

    // Embedded Stacked Flash (RP2354)
#if defined(SL_MCU_RP2354) || (defined(PICO_FLASH_SIZE_BYTES) && (PICO_FLASH_SIZE_BYTES == 2 * 1024 * 1024))
    bool has_stacked_flash = true;
#else
    bool has_stacked_flash = false;
#endif

    info->has_embedded_flash = has_stacked_flash;
    if (has_stacked_flash) {
        info->flash_size_bytes = 2 * 1024 * 1024;
        info->chip_name = is_qfn80 ? "RP2354B" : "RP2354A";
    } else {
        info->flash_size_bytes = 0;
        info->chip_name = is_qfn80 ? "RP2350B" : "RP2350A";
    }

// Query OTP Secure Boot Hardware Flag
#if defined(OTP_CRITICAL_SECURE_BOOT_ENABLE_BITS)
    info->secure_boot_en = (otp_hw->critical & OTP_CRITICAL_SECURE_BOOT_ENABLE_BITS) != 0;
#else
    info->secure_boot_en = false;
#endif

    // POWMAN Core Power Regulator Status Check
    info->vreg_ok = (powman_hw->vreg_ctrl & POWMAN_VREG_CTRL_RST_N_BITS) != 0;
    info->rom_gitrev = sysinfo_hw->gitref_rp2350;
    info->rom_version = bootrom_ver;

#else
    info->family = CHIP_FAMILY_UNKNOWN;
    info->chip_name = "Unknown Silicon";
    snprintf(info->stepping_str, sizeof(info->stepping_str), "??");
#endif

    // Active Execution Core ID & Clocks
    info->active_core_id = (uint8_t)get_core_num();
    info->total_cores = 2;
    info->sys_clk_freq_hz = clock_get_hz(clk_sys);
    info->rosc_stable = (rosc_hw->status & ROSC_STATUS_STABLE_BITS) != 0;

    // Direct Hardware 64-bit Unique Device ID
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    memcpy(info->unique_id, board_id.id, 8);
}

void sysinfo_print(const sysinfo_t *info) {
    if (!info)
        return;

    printf("================ SYSTEM HARDWARE INFO ================\n");
    printf(" Target Chip        : %s\n", info->chip_name);
    printf(" Chip ID Part       : 0x%04X\n", info->chip_id_part);
    printf(" Silicon Stepping   : Step %s (Rev Reg: 0x%X)\n", info->stepping_str, info->revision_raw);
    printf(" BootROM Version    : v%d (Git: 0x%08X)\n", info->rom_version, (unsigned int)info->rom_gitrev);
    printf(" Architecture       : ");
    switch (info->current_arch) {
    case ARCH_CORTEX_M0PLUS:
        printf("ARM Cortex-M0+\n");
        break;
    case ARCH_CORTEX_M33:
        printf("ARM Cortex-M33 (TrustZone %s)\n", info->is_secure_mode ? "Secure" : "Non-Secure");
        break;
    case ARCH_HAZARD3_RISCV:
        printf("Hazard3 RISC-V\n");
        break;
    default:
        printf("Unknown\n");
        break;
    }
    printf(" Security Flags     : Secure Boot [%s]\n", info->secure_boot_en ? "ENABLED" : "DISABLED");
    printf(" Active Core ID     : Core %d / %d\n", info->active_core_id, info->total_cores);
    printf(" SRAM Capacity      : %lu KB\n", (unsigned long)(info->sram_size_bytes / 1024));
    printf(" Package Pins       : %d-pin\n", info->package_pin_count);
    printf(" Flash Config       : %s\n", info->has_embedded_flash ? "2MB Stacked Internal Flash (RP2354)" : "External SPI Flash");

    printf(" Hardware UID       : ");
    for (int i = 0; i < 8; i++) {
        printf("%02X%s", info->unique_id[i], (i == 7) ? "" : ":");
    }
    printf("\n");

    printf(" System Clock       : %lu MHz\n", (unsigned long)(info->sys_clk_freq_hz / 1000000));
    printf(" Power & Oscillator : VREG OK [%s] | ROSC Stable [%s]\n", info->vreg_ok ? "YES" : "NO", info->rosc_stable ? "YES" : "NO");
    printf("======================================================\n");
}

// vim: set ts=4 sw=4 sts=4 expandtab: