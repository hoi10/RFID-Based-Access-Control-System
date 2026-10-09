# Build modes

- **Debug** keeps `APP_USE_FREERTOS=0` and is the Wokwi/super-loop build. `wokwi.toml` continues to use `Debug/RFID.hex`.
- **Release** defines `APP_USE_FREERTOS=1` and is the hardware/FreeRTOS build.

The application logic is shared in `Core/Src/app.c`. In FreeRTOS mode, `RFIDTask`
polls the RC522 and sends UIDs through a queue; `CardDisplayTask` owns the LCD,
LED, buzzer and servo response. In simulation mode, `main` polls and handles
cards sequentially.

## FreeRTOS kernel dependency

FreeRTOS Kernel V11.3.1 is included under `FreeRTOS/Source`. `Release` alone
includes the `FreeRTOS` source folder, so its kernel files are not built in
Debug/Wokwi. The kernel source files included are:

- `tasks.c`, `queue.c`, `list.c` from `Source`
- `portable/GCC/ARM_CM3/port.c`
- `portable/MemMang/heap_4.c`

Do not include the other port or heap `.c` files.

The Release configuration has these include paths:

- `FreeRTOS/Source/include`
- `FreeRTOS/Source/portable/GCC/ARM_CM3`

`Core/Inc/FreeRTOSConfig.h` is configured for the Cortex-M3 at 72 MHz. Keep the
FreeRTOS `SVC_Handler` and `PendSV_Handler` aliases enabled; `SysTick_Handler`
also calls the FreeRTOS tick handler while retaining the HAL tick.

Build the **Debug** configuration for Wokwi and **Release** for a physical
board. The two outputs use separate `Debug` and `Release` directories, so the
simulator HEX remains the bare-metal build.
