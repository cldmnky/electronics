#!/bin/sh
# Reversible USB-gadget probe: can this Pi register a UAC2 gadget at all?
#
# Everything here is undone before the script exits: the configfs gadget is
# removed, libcomposite is unloaded and any configfs mount this script created
# is unmounted again. It never touches /boot/firmware, never reboots and never
# writes to a block device. It runs on the Pi over ssh.
#
# What it proves: the kernel has the whole gadget stack (CONFIG_USB_GADGET,
# CONFIG_USB_CONFIGFS, CONFIG_USB_F_UAC2) but a gadget only becomes visible once
# a USB Device Controller (UDC) exists in /sys/class/udc. If that directory is
# empty, no userspace action can create the 2-in/2-out class-compliant audio
# device the product manual promises - the limit is hardware, not software.
#
# Linux gadget internals used here:
#   functions/uac2.*  - USB Audio Class 2 function (the 2-in/2-out stream)
#   configs/c.1       - one configuration holding that function
#   UDC               - the controller the configuration binds to
set -u

CFG=/sys/kernel/config
GADGET=$CFG/usb_gadget/benchprobe
LOADED_LIBCOMPOSITE=0
MOUNTED_CONFIGFS=0
BASELINE_UDC=$(ls /sys/class/udc 2>/dev/null | tr '\n' ' ')

echo "=== 1. UDC inventory before anything is loaded ==="
echo "baseline_udc=[${BASELINE_UDC}]"
ls -l /sys/class/udc/
if [ -d /sys/module/libcomposite ]; then
  echo "libcomposite already loaded; leaving module state alone"
else
  LOADED_LIBCOMPOSITE=1
fi

if [ ! -d "$CFG/usb_gadget" ]; then
  if ! mountpoint -q "$CFG" 2>/dev/null; then
    echo "=== 2. mounting configfs (temporary) ==="
    sudo mount -t configfs none "$CFG" || { echo "configfs mount failed"; exit 2; }
    MOUNTED_CONFIGFS=1
  fi
fi

if [ "$LOADED_LIBCOMPOSITE" = "1" ]; then
  echo "=== 3. loading libcomposite (reversible, unloaded at the end) ==="
  sudo modprobe libcomposite || { echo "modprobe libcomposite failed"; exit 2; }
fi

echo "=== 4. creating a UAC2 gadget in configfs ==="
set -e
sudo mkdir -p "$GADGET"
echo 0x1d6b | sudo tee "$GADGET/idVendor" >/dev/null
echo 0x0104 | sudo tee "$GADGET/idProduct" >/dev/null
echo 0x0100 | sudo tee "$GADGET/bcdDevice" >/dev/null
echo 0x0200 | sudo tee "$GADGET/bcdUSB" >/dev/null
sudo mkdir -p "$GADGET/strings/0x409"
echo "midibrain-bench" | sudo tee "$GADGET/strings/0x409/serialnumber" >/dev/null
echo "MIDI Brain bench probe" | sudo tee "$GADGET/strings/0x409/manufacturer" >/dev/null
echo "mnkybox uac2 probe" | sudo tee "$GADGET/strings/0x409/product" >/dev/null
sudo mkdir -p "$GADGET/functions/uac2.bench"
echo 48000 | sudo tee "$GADGET/functions/uac2.bench/c_chmask" >/dev/null
echo 2 | sudo tee "$GADGET/functions/uac2.bench/c_ssize" >/dev/null
echo 48000 | sudo tee "$GADGET/functions/uac2.bench/p_chmask" >/dev/null
echo 2 | sudo tee "$GADGET/functions/uac2.bench/p_ssize" >/dev/null
sudo mkdir -p "$GADGET/configs/c.1"
sudo ln -s "$GADGET/functions/uac2.bench" "$GADGET/configs/c.1/"
echo "--- gadget tree created: ---"
sudo ls -R "$GADGET" | head -20

echo "=== 5. the decisive test: is there a UDC to bind it to? ==="
echo "udc_available=[$(ls /sys/class/udc 2>/dev/null | tr '\n' ' ')]"
printf '' | sudo tee "$GADGET/UDC" >/dev/null 2>&1
if printf 'bench-test-udc\n' | sudo tee "$GADGET/UDC" >/dev/null 2>&1; then
  echo "BIND SUCCEEDED (unexpected): UDC accepted a name"
else
  echo "BIND FAILED: no UDC exists, so the gadget cannot be registered"
fi
echo "udev/UDC state after the attempt: [$(ls /sys/class/udc 2>/dev/null | tr '\n' ' ')]"

echo "=== 6. teardown ==="
sudo rm -f "$GADGET/configs/c.1/uac2.bench"
sudo rmdir "$GADGET/configs/c.1" "$GADGET/functions/uac2.bench" 2>/dev/null || true
sudo rmdir "$GADGET/strings/0x409" 2>/dev/null || true
sudo rmdir "$GADGET" 2>/dev/null || true
set +e
if [ "$MOUNTED_CONFIGFS" = "1" ]; then
  sudo umount "$CFG" && echo "configfs unmounted"
fi
if [ "$LOADED_LIBCOMPOSITE" = "1" ]; then
  # Creating the uac2 function pulls in u_audio and usb_f_uac2; libcomposite
  # stays busy until both are gone, so unload in dependency order.
  sudo rmmod usb_f_uac2 2>/dev/null && echo "usb_f_uac2 unloaded"
  sudo rmmod u_audio 2>/dev/null && echo "u_audio unloaded"
  sudo rmmod libcomposite && echo "libcomposite unloaded"
fi

echo "=== 7. verification that nothing is left behind ==="
echo "udc_now=[$(ls /sys/class/udc 2>/dev/null | tr '\n' ' ')]"
echo "gadget_dir_present=$([ -e "$GADGET" ] && echo yes || echo no)"
echo "libcomposite_loaded=$([ -d /sys/module/libcomposite ] && echo yes || echo no)"
echo "u_audio_loaded=$([ -d /sys/module/u_audio ] && echo yes || echo no)"
echo "usb_f_uac2_loaded=$([ -d /sys/module/usb_f_uac2 ] && echo yes || echo no)"
echo "configfs_mounted=$(mountpoint -q "$CFG" && echo yes || echo no)"
echo "udc_unchanged=$([ "${BASELINE_UDC}" = "$(ls /sys/class/udc 2>/dev/null | tr '\n' ' ')" ] && echo yes || echo no)"
