#!/bin/bash
# Interactively toggle MSS110 GPIOs one-by-one to find the relay/LED.
# Talks to the scanner+OTA firmware over serial (sends "t<Dnum>" commands).
# Flash-controller pins (PA9-PA12 = D7-D10) and serial pins (PA15/PA16) are
# deliberately NOT included - driving them would crash the chip.

PORT="${1:-/dev/cu.usbserial-A5069RR4}"

if [ ! -e "$PORT" ]; then
  echo "Port $PORT not found. Pass it as arg 1."
  exit 1
fi

# Candidate output pins: Dnum:name  (excludes flash D7-D10 and serial D13/D14)
PINS=(
  "5:PA7"  "6:PA8"
  "0:PA0"  "1:PA1"  "2:PA2"  "3:PA3"  "4:PA4"
  "11:PA13" "12:PA14"
  "15:PA17" "16:PA18" "17:PA19" "18:PA20"
)

# Configure the line and open a read/write fd.
stty -f "$PORT" 115200 cs8 -cstopb -parenb raw -echo 2>/dev/null
exec 3<>"$PORT" || { echo "Could not open $PORT (is cu/screen still attached?)"; exit 1; }

send() { printf '%s\r\n' "$1" >&3; }

cleanup() { send "r"; sleep 0.3; exec 3>&- 2>/dev/null; echo; echo "Done. All pins reset to input."; }
trap cleanup EXIT INT

echo "=== MSS110 pin toggler ==="
echo "For each pin: it goes HIGH, you listen/look, press Enter; then it goes LOW, press Enter for the next."
echo "Note which pin makes the relay CLICK or an LED change."
echo

for entry in "${PINS[@]}"; do
  d="${entry%%:*}"; name="${entry##*:}"
  echo "-------------------------------------------"
  send "r"; sleep 0.2                    # make sure we start from a clean state
  send "t$d"; echo ">>> $name (D$d) is now HIGH   -- listen for a click / watch for LED"
  read -r -p "    [Enter] to set it LOW... "
  send "t$d"; echo "    $name (D$d) is now LOW"
  read -r -p "    [Enter] for the next pin (or Ctrl-C to stop)... "
done

echo
echo "Swept all candidate pins."
