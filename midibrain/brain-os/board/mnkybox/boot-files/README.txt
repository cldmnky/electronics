# MNKYBOX boot partition — first-boot material

This directory on the FAT boot partition is read once, on the first boot, by
`mnkybox-firstboot.service`. It installs the files it finds and renames them to
`*.applied`. Nothing here is a secret baked into the image: the files ship as
templates.

## WiFi (optional)

1. Mount the FAT partition on a computer.
2. Edit `mnkybox/wpa_supplicant.conf` and set a country and your network:

   ```
   country=GB
   network={
       ssid="studio"
       psk="your-passphrase"
   }
   ```

   The `country=` line is mandatory. Without it the regulatory database refuses
   to transmit and the first-boot unit leaves WiFi off, with a message in the
   journal. WiFi stays off until a country is set.
3. Unmount, put the card in the Pi 5, power on.

## SSH (optional)

Put your public key in `mnkybox/authorized_keys` (one key per line, OpenSSH
format). It is installed as `/root/.ssh/authorized_keys` with mode 0600.

Dropbear runs with `-s`: password logins are disabled. Without a key in this
file, SSH login is impossible — the wired network still gets a DHCP address,
but there is no other way in except re-flashing the card.

## Afterwards

Both files are renamed to `*.applied` on the boot partition. To change the WiFi
configuration later, log in over SSH (or wired Ethernet) and edit
`/etc/wpa_supplicant/wpa_supplicant-wlan0.conf`, or put a new
`wpa_supplicant.conf` here and remove the `.applied` suffix from the old one.

## Serial console

There is none. UART0 and UART1 are reserved for MIDI (see
`midibrain/docs/pin-map.md`), so `cmdline.txt` deliberately has no UART console
and `config.txt` enables no serial function.
