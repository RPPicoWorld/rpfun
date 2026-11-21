# rpfun
Raspberry Pi Pico RP2350 Fun

This repository contains the source used in our RP2350 Getting Started Tutorial
video series: 

https://www.youtube.com/@RPPicoWorld

## Episodes

| # | Video | Example(s) |
|---|-------|------------|
| 1 | Development boards and Tools | - |
| 2 | First Blinky | [blink](examples/blink) |
| 3 | Second Blinky | [blink2](examples/blink2) |
| 4 | Serial Print | [serial](examples/serial) |
| 5 | Dual Core Setup | [dual](examples/dual) |
| 6 | Overclock RP2350 | [overclock](examples/overclock), [dhrystone](examples/dhrystone) |
| 7 | Extreme Overclock - 540 MHz | [temp](examples/temp) |
| 8 | FreeRTOS on RP Pico (part 1) | [freertos](examples/freertos) |
| 9 | FreeRTOS (part 2) - Semaphores, Notifications, Queues and Timers | [freertos2](examples/freertos2), [freertos3](examples/freertos3) |
| 10 | WS2812 (Neopixel) RGB LED (part 1) | [ws2812](examples/ws2812), [ws2812_dma](examples/ws2812_dma) |
| 11 | FreeRTOS WS2812 (part 2) | [ws2812_freertos](examples/ws2812_freertos) |
| 12 | CAN Bus | [can](examples/can) |
| 13 | CMSIS-DSP | [cmsis-dsp](examples/cmsis-dsp) |
| 14 | USB Ethernet | [usb_ethernet](examples/usb_ethernet), [usb_ethernet_client](examples/usb_ethernet_client) |
| 15 | I2S Audio with PCM5102A DAC | [i2s_dual_sine](examples/i2s_dual_sine) |
| 16 | LittleFS on built-in Flash | [flash_fs](examples/flash_fs) |
| 17 | I2C Masterclass: 1MHz EEPROM (M24M01) | [i2c_scan](examples/i2c_scan), [m24cxx](examples/m24cxx) |
| 18 | I2C EEPROM, WS2812 and FreeRTOS | [m24cxx_ws2812_freertos](examples/m24cxx_ws2812_freertos) |
| 19 | SPI Flash (W25Qxx) | - |
| 20 | W5500 SPI Ethernet | [w5500_ping](examples/w5500_ping), [w5500_ping_dual_core](examples/w5500_ping_dual_core) |
| 21 | Hybridlink Open Source Programmer and Debugger | - |

## Switching between Arm and RISC-V

The RP2350 has two Arm Cortex-M33 cores and two Hazard3 RISC-V cores, but only one
pair runs at a time. At boot the bootrom looks at the image in flash and starts the
matching cores (switching architecture by itself if needed). Building for the other
architecture is just a matter of changing one line in `CMakeLists.txt`:

```cmake
set(PICO_PLATFORM rp2350)         # Arm Cortex-M33
set(PICO_PLATFORM rp2350-riscv)   # Hazard3 RISC-V
```

plus the matching `toolchainVersion` (an Arm toolchain such as `15_2_Rel1`, or a
RISC-V one such as `RISCV_PICO_2_3_1_0`). See `dhrystone_arm` / `dhrystone_riscv`
and `whetstone_arm` / `whetstone_riscv`, which differ only in those lines.

### Flashing with a CMSIS-DAP probe (e.g. Hybridlink)

The stock "Flash" task picks the OpenOCD config from the project:
`target/rp2350.cfg` (Arm cores only) or `target/rp2350-riscv.cfg` (RISC-V cores
only). That breaks as soon as the chip is still running the *other*
architecture: the cores OpenOCD wants to use are switched off, so it has nothing
to run the flash algorithm on, and you end up holding BOOTSEL and copying a UF2.

The `.vscode/tasks.json` in every example fixes this with two changes to the
Flash task:

1. `"dependsOn": ["Rescue Reset"]` – first run the rescue reset
   (`target/rp2350-rescue.cfg`, `reset halt`). This resets the whole chip and
   leaves Arm core 0 halted in the bootrom, *whatever* image is in flash.
2. Always program through `target/${command:raspberry-pi-pico.getChip}.cfg`
   (= `rp2350.cfg`, the Arm side) instead of `getTarget` – also for RISC-V
   images. Flash contents don't care which core wrote them; on the following
   reset the bootrom sees a RISC-V image and switches architecture itself.

From the command line the same thing is:

```bash
OCD=~/.pico-sdk/openocd/0.12.0+dev
$OCD/openocd -s $OCD/scripts -f interface/cmsis-dap.cfg -f target/rp2350-rescue.cfg -c "adapter speed 5000; reset halt; exit"
$OCD/openocd -s $OCD/scripts -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000; program build/main.elf verify reset exit"
```

Debugging is unchanged: the debug launch configuration still attaches to the
RISC-V cores for a RISC-V build.

## FreeRTOS

FreeRTOS port is added with:


git submodule add https://github.com/raspberrypi/FreeRTOS-Kernel.git FreeRTOS-Kernel
