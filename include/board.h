#pragma once

//*******  Pin definitions  ***************
//
// BC250 PSU controller wiring — XIAO ESP32C3 "2 channel" perfboard
// (see hardware/bc250-xiao-2channel.diy). Both switched lines go through a 2N7000
// low-side MOSFET with a gate pull-down, so the GPIOs drive a gate (push-pull,
// active HIGH) rather than the line itself:
//
//   XIAO   ESP32-C3                       External
//   ----   --------                       --------
//   D1     GPIO3  (BOARD_SENSE)  <-R3 1k- BC250 TPMS1 pin 9 (3.3V = board on)
//   D2     GPIO4  (BUTTON_SENSE) <-----   momentary switch (other side to GND)
//   D3     GPIO5  (PWR_BTN_PIN)  ----->   Q1 gate (R2 10k pull-down) -> BC250 PWRBTN#
//   D10    GPIO10 (PS_ON_PIN)    ----->   Q2 gate (R1 10k pull-down) -> ATX PS_ON#
//
// The switch shorts GPIO4 to ground; it's read with an internal pull-up, so
// pressed reads LOW.
const int BUTTON_SENSE = 4;

// ATX PS_ON# is active LOW and idles at ~5V (pulled up inside the PSU). Q2
// sinks it to ground when its gate is driven HIGH:
//   GPIO HIGH -> FET on  -> PS_ON# LOW -> PSU on
//   GPIO LOW  -> FET off -> PSU pull-up wins -> PSU off
// The gate pull-down keeps the FET off while the GPIO is still high-Z at boot.
const int PS_ON_PIN = 10;

// BC250 power-button input (PWRBTN#), pulled up on the board. Q1 shorts it to
// ground for the duration of a "tap"; the OS handles that as an ACPI power
// event (graceful shutdown). Same gate-drive polarity as PS_ON:
//   GPIO HIGH -> FET on  -> PWRBTN# LOW -> button pressed
//   GPIO LOW  -> FET off -> released
const int PWR_BTN_PIN = 5;

// BC250 TPMS1 (pin 9): reads ~3.3V while the board is powered/booted, 0 when
// off. In practice it's a higher-impedance source that settles near ~2.9V and
// hovers close to the ESP's digital logic threshold, so digitalRead() flickers.
// We read it as an ADC voltage with hysteresis instead (see thresholds below).
const int BOARD_SENSE = 3;

// Hysteresis thresholds for the analog board-sense reading. The gap between
// them keeps a noisy signal sitting near the threshold from chattering:
//   reading rises above HIGH -> treat as "board up"
//   reading falls below LOW  -> treat as "board down"
//   in between               -> hold previous state
const int SENSE_HIGH_MV = 2000;
const int SENSE_LOW_MV  = 800;

// TPMS1 is high-impedance and the ESP32 single-shot ADC is noisy, so an isolated
// analogReadMilliVolts() can spike hundreds of mV above the true level. Once the
// board powers off the line floats near 0V but still throws the occasional spike
// past SENSE_HIGH_MV. A single such spike flips the hysteresis HIGH for one loop,
// which restarts the BOARD_OFF_DEBOUNCE_MS countdown -> shutdown detection stalls
// for an unbounded, random time. Averaging this many samples per reading is a
// low-pass that keeps a lone spike from ever crossing a threshold.
const int SENSE_OVERSAMPLE = 16;

//*******  Logic levels  ***************

// Both switched lines are driven through a low-side MOSFET: GPIO HIGH turns the
// FET on and pulls the external line to ground.
const int PS_ON_ASSERT  = HIGH;  // FET on  -> PS_ON# LOW -> PSU on
const int PS_ON_RELEASE = LOW;   // FET off -> PSU off

const int PWR_BTN_PRESS   = HIGH;  // FET on  -> PWRBTN# LOW -> pressed
const int PWR_BTN_RELEASE = LOW;   // FET off -> released

//*******  Timing (milliseconds)  ***************

// Switch debounce window.
const unsigned long DEBOUNCE_MS = 30;

// Hold the button this long while the board is ON to ask it to shut down: we
// tap the board's power button (PWRBTN#) and the OS performs an ACPI soft-off.
// The PSU is released once TPMS1 drops, exactly as for an OS-initiated shutdown.
const unsigned long LONG_PRESS_MS = 5000;

// Keep holding to this long (from any non-OFF state) to force the PSU off
// regardless of what the board is doing. This is the escape hatch for a hung
// OS that ignores the ACPI request.
const unsigned long FORCE_OFF_HOLD_MS = 10000;

// How long PWRBTN# is held LOW to register as a tap. Real buttons register at a
// few tens of ms; keep this well under ~4 s, which is where the board's own
// "hold power button to hard-off" override kicks in.
const unsigned long PWR_BTN_PULSE_MS = 500;

// After tapping PWRBTN#, wait this long for TPMS1 to drop. If the board is
// still up when this expires we assume the request was ignored (or cancelled at
// the OS level), log it and go back to plain ON so the button can try again.
const unsigned long SHUTDOWN_TIMEOUT_MS = 90000;

// Hold the button this long while OFF to enter WiFi setup mode (reconfigure the
// bound controller / password). Longer than LONG_PRESS_MS and only armed for
// presses that begin while OFF, so it never collides with force-off.
const unsigned long SETUP_HOLD_MS = 8000;

// TPMS1 must stay LOW continuously for this long before we treat the board as
// having shut itself down. Filters out brief dips/transients during boot/reset.
const unsigned long BOARD_OFF_DEBOUNCE_MS = 1500;

// The BC250's auto-power-on jumper is left off, so after the PSU comes on the
// board sits in S5 until its power button is pressed. After asserting PS_ON#,
// give the PSU rails and the board's standby logic this long to settle, then
// press PWRBTN# to start it. (If the board did boot on AC its 3V rail would be
// up within milliseconds and the press would never fire.)
const unsigned long BOOT_KICK_DELAY_MS = 1000;

// How long the boot-time PWRBTN# press is held. Longer than the shutdown tap for
// margin against the board's own debounce, still well under the ~4s hard-off.
const unsigned long BOOT_KICK_PULSE_MS = 500;

// If TPMS1 is still LOW this long after a boot press, press again.
const unsigned long BOOT_KICK_RETRY_MS = 3000;

// How long to wait for TPMS1 to go HIGH after asserting PS_ON#. If the board
// hasn't signalled UP by then we assume the boot failed, release the PSU and
// return to idle (OFF). Leaves room for several boot presses.
const unsigned long BOOT_TIMEOUT_MS = 15000;

// Periodic heartbeat log interval.
const unsigned long HEARTBEAT_MS = 1000;

//*******  WiFi setup portal  ***************

// SoftAP name shown when the device is in setup mode (open network).
const char *const AP_SSID = "BC250 Switch Setup";

// WiFi TX power for the SoftAP. These ESP32-C3 mini boards have an RF/power
// design flaw (arduino-esp32 #6551): at full power the AP emits no usable
// beacons, so the portal is invisible. A low value fixes it. WIFI_POWER_8_5dBm
// is confirmed working on this board.
#define AP_TX_POWER WIFI_POWER_8_5dBm

//*******  BLE wake  ***************

// The bound controller's BLE MAC is configured via the setup portal and stored
// in NVS (see config.h: config.wakeAddr). When the machine is OFF and that
// controller is advertising, we power on ("machine follows controller").

// The controller counts as "present" while it has been seen within this window.
// While OFF, presence => the machine powers on ("machine follows controller").
const unsigned long BLE_PRESENCE_TIMEOUT_MS = 4000;

// Guard window after any power-off during which BLE presence is ignored. This is
// your chance to also switch the controller off (it then goes absent and the
// machine stays down). If you leave the controller on, once this elapses the
// machine follows it back on. It also rides out the brief reconnect-advertising
// burst the controller emits when it loses its host at shutdown.
const unsigned long BLE_WAKE_COOLDOWN_MS = 15000;
