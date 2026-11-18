# Open firmware for the Meross MSS110 smart plug

Custom [LibreTiny](https://docs.libretiny.eu/) firmware for the **Meross MSS110**
(hardware v7.0.0), whose Wi-Fi module is a **Realtek RTL8710CM** (AmebaZ2). It
replaces the stock cloud firmware with a tiny local-only firmware: the button
toggles the relay, and holding the button brings up a Wi-Fi recovery AP with a
web page for over-the-air firmware updates — no cloud, no account, no app.

> ⚠️ **Vibe-coded, hardware-verified.** This was written almost entirely by an AI
> assistant (Claude / Claude Code) through natural-language prompting. I didn't
> hand-write the firmware — but I **verified all of it on real hardware**: the
> pin map was reverse-engineered and confirmed with a scanner sketch, and every
> behavior here (relay, LEDs, button, recovery AP, OTA) was tested on an actual
> MSS110. Treat it as a hobby project, read the safety notes, and flash at your
> own risk.

## Features

- **Short button press** → toggle the relay.
- **Hold button 10 s** → Wi-Fi recovery mode: open AP `MSS110-RECOVERY`, reachable
  at `http://mss110.local/` (or `http://192.168.1.80/`), with relay on/off, a
  reboot button, and a **firmware upload (OTA)** form.
- **Hold button during power-on** → boot straight into recovery (debounced, so a
  disconnected button can't trip it). An escape hatch if an update ever misbehaves.
- **LED**: off in normal use; **red** while the button is held; **solid green**
  while in recovery.
- Optional **self-running relay toggle** on a fixed interval (see below).
- Once this firmware is on, **all further updates are OTA** — no serial needed.

## Hardware (RTL8710CM, 4 MB embedded flash)

Verified GPIO map (Realtek `PA` numbers; LibreTiny Arduino pin in parens):

| Function | Pin | Notes |
|---|---|---|
| Relay | **PA20** | active-HIGH (HIGH = on). Hongfa HF3FF/005 single-coil. |
| Button | **PA23** (D19) | active-LOW |
| LED red | **PA13** | bicolor common-anode, drive LOW to light |
| LED green | **PA14** | bicolor common-anode, drive LOW to light |
| UART log | PA15 (RX) / PA16 (TX) | 115200 8N1 |
| Download strap | PA0 **and** PA13 | both tied to 3.3V + power-cycle (see Flashing) |

On this board, entering UART download mode needed **both** exposed test pads —
`gpioa_0` (PA0) and `gpioa_13` (PA13, on the back) — tied **straight to 3.3V**.
Wiring them directly (no series resistor) worked fine.

The relay/LEDs sit on pads LibreTiny leaves muxed to SDIO/SWD, so the firmware
drives them through the **raw Realtek HAL** (`hal_gpio_*`) rather than
`digitalWrite()`. See the comments in `firmware/src/main.cpp`. The relay is fully
drivable on 3.3V bench power — it does **not** need mains to click.

## Repo layout

```
firmware/
  platformio.ini        PlatformIO + LibreTiny project
  src/main.cpp          the firmware
  mss110_v1.0.uf2       prebuilt: normal (button-controlled)
  mss110_v1.2_15min.uf2 prebuilt: relay auto-toggles every 15 min
tools/                  serial GPIO-hunting scripts used while reverse engineering
```

## Build

```sh
pip install platformio
cd firmware
pio run -e mss110
# output: .pio/build/mss110/firmware.uf2
```

### Optional: self-running relay toggle

Set `RELAY_AUTO_TOGGLE_MS` at the top of `firmware/src/main.cpp`
(`0` = off; `900000` = 15 min; `30000` = 30 s) and rebuild. The interval is timed
from boot (not wall-clock) and does not persist across power loss. The prebuilt
`mss110_v1.2_15min.uf2` is this with a 15-minute interval.

## Flashing

**First flash** must go over UART, because the stock firmware doesn't expose an
update path. After that, use OTA.

1. Wire a 3.3V USB-UART adapter: module **TX2/PA16 → adapter RX**,
   **RX2/PA15 → adapter TX**, **GND → GND**.
2. Enter download mode: tie **both PA0 and PA13 to 3.3V** (the `gpioa_0` and
   `gpioa_13` test pads — straight to 3.3V, no resistor needed), then power-cycle.
   A full ~5 s power-down with the straps held is the reliable way in.
3. Flash with [ltchiptool](https://github.com/libretiny-eu/ltchiptool) — and
   **close any serial monitor first**, or it steals the bootloader's replies and
   the link times out:
   ```sh
   ltchiptool flash write firmware/mss110_v1.0.uf2 -d /dev/cu.usbserial-XXXX
   ```
4. Remove the strap, power-cycle.

**Updates after that:** hold the button 10 s → connect to `MSS110-RECOVERY` →
open `http://mss110.local/` → upload a `.uf2`.

## Safety

- **Mains voltage.** A smart plug switches mains. Only work on it unplugged, and
  **never connect the USB-UART adapter while the plug is on mains** — many of
  these supplies are non-isolated, which can put the board's "ground" at mains
  potential and injure you or destroy your laptop. Flash on the bench at 3.3V.
- Custom firmware overwrites the stock firmware. **Back up the full flash first**
  (`ltchiptool flash read realtek-ambz2 backup.bin`) so you can restore. That dump
  contains device secrets — keep it private, don't commit it.
- No warranty. See `LICENSE`.

## License

Released into the public domain under the [Unlicense](https://unlicense.org).
Do whatever you want with it.
