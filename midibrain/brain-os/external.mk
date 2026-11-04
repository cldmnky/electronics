################################################################################
# MNKYBOX external tree
#
# This tree has no packages of its own: the application ships as a rootfs
# overlay (prebuilt binaries + systemd units), never compiled by Buildroot.
# The file exists because Buildroot requires it, and it carries the one
# upstream package adjustment this board needs.
################################################################################

# PipeWire's JACK-compatible client library (libjack.so.0) must be built, and
# JACK2 must NOT be: the owner decision is "PipeWire's libjack, never JACK2".
#
# Buildroot 2025.02 package/pipewire/pipewire.mk only passes
# -Dpipewire-jack=enabled when BR2_PACKAGE_JACK2=y, which would drag JACK2
# into the image. PipeWire's own meson project builds src/pipewire-jack (the
# libjack.so.0 client plus the jack headers) without any JACK2 dependency; the
# separate -Djack option is the JACK *bridge* (module-jack-source/sink) and
# stays disabled. This file is included after package/*/*.mk, so the option
# list can be rewritten here.
#
# The resulting library is installed by PipeWire into
# /usr/lib/pipewire-0.3/jack/libjack.so.0 (see `pw-jack`); check-image.sh
# asserts that file exists and that no JACK2 library is present.
PIPEWIRE_CONF_OPTS := $(filter-out -Dpipewire-jack=disabled -Djack=disabled,$(PIPEWIRE_CONF_OPTS)) \
	-Dpipewire-jack=enabled -Djack=disabled
