#!/bin/sh

wayland-scanner server-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml desktop-server.h
wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml desktop-server.c


# Generate the DMA files
XML_PATH="/usr/share/wayland-protocols/stable/linux-dmabuf/linux-dmabuf-v1.xml"

wayland-scanner server-header $XML_PATH linux-dmabuf.h

wayland-scanner public-code $XML_PATH linux-dmabuf.c
