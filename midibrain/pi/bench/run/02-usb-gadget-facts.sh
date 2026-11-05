#!/bin/sh
# Read-only inventory: is there any USB device controller and is the gadget stack
# present? Writes nothing to the Pi.
set -eu
ssh magnus@studio '
  set +e
  echo "=== date ==="; date -Is
  echo "=== udc ==="; ls -l /sys/class/udc/; echo "udc_count=$(ls /sys/class/udc | wc -l)"
  echo "=== gadget modules present ==="; ls /lib/modules/$(uname -r)/kernel/drivers/usb/gadget/ 2>&1 | head
  echo "=== loaded dwc/gadget modules ==="; lsmod | grep -iE "dwc|gadget|composite"; echo "(end)"
  echo "=== platform drivers ==="; ls /sys/bus/platform/drivers/ | grep -iE "dwc|udc|usb"
  echo "=== dmesg dwc/otg ==="; dmesg 2>&1 | grep -iE "dwc|otg|gadget" | head -20
  echo "=== kernel config ==="; grep -E "^CONFIG_USB_(GADGET|LIBCOMPOSITE|CONFIGFS|F_UAC2|DWC2|DWC3)" /boot/config-$(uname -r)
  echo "=== config.txt (read-only) ==="; cat /boot/firmware/config.txt
'
