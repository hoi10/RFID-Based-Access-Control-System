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

## UART commands

USART1 runs at 9600 baud, 8 data bits, no parity, 1 stop bit. The RX interrupt
buffers input so commands can be entered in a serial terminal while RFID scans
continue. Commands are case-insensitive:

- `HELP` — show command syntax
- `LIST` — list authorized UIDs
- `ADD 11:22:33:44` — add a UID (up to five cards total)
- `DEL 11:22:33:44` — remove a UID
- `LOG` — show the last ten card scans
- `LOG CLEAR` — clear the in-RAM scan history
- `ENROLL` — add the next scanned UID to the authorized list
- `OPEN` — open the barrier manually, then close it after the configured interval
- `LOCK` / `UNLOCK` — temporarily deny or restore RFID-based access
- `STATUS` — report lock state, card/log counts, invalid-scan count and enrollment state

`LOCK` blocks RFID-based access; `OPEN` remains a manual UART override, including
while locked. Administrative UART commands do not require PIN authentication.

An emergency pushbutton is connected between PB1 and GND, using the MCU's
internal pull-up. Pressing it opens the barrier for the configured interval,
even while RFID access is locked. A falling-edge interrupt latches the press
so short presses are not lost during RFID polling; software debounces release
before rearming the interrupt.

Each scan is also sent immediately as `LOG,<uptime-ms>,<UID>,<ALLOW|DENY>`.
Every three consecutive unauthorized-card scans trigger a buzzer/LCD warning
and reset the counter; a valid UID also resets it. Authorized cards, lock state
and scan history are held in RAM and reset to their defaults when the board
restarts. UART commands and card responses run on the same FreeRTOS task so
UART-driven barrier actions do not run concurrently with card handling.

`Core/Inc/FreeRTOSConfig.h` is configured for the Cortex-M3 at 72 MHz. Keep the
FreeRTOS `SVC_Handler` and `PendSV_Handler` aliases enabled; `SysTick_Handler`
also calls the FreeRTOS tick handler while retaining the HAL tick.

Build the **Debug** configuration for Wokwi and **Release** for a physical
board. The two outputs use separate `Debug` and `Release` directories, so the
simulator HEX remains the bare-metal build.
