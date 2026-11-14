# qutaq-notify

## Overview

`qutaq-notify` is technology for Qutaq Desktop.

## Build

**Arch Linux**

```sh
sudo pacman -S --needed base-devel git cmake qt6-base qt6-wayland qt6-svg layer-shell-qt wayland
```

**Debian / Ubuntu**

```sh
sudo apt install build-essential git cmake pkg-config libwayland-dev \
  qt6-base-dev qt6-base-private-dev qt6-wayland-dev qt6-wayland-private-dev \
  liblayershellqtinterface-dev qt6-svg-plugins
```

**Fedora**

```sh
sudo dnf install gcc-c++ git cmake wayland-devel \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtwayland-devel layer-shell-qt-devel qt6-qtsvg
```

Building:
```sh
git clone https://github.com/Qutaq-Linux/qutaq-notify.git
cd qutaq-notify
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cp build/qutaq-notify ~/.local/bin/
```

## Run in labwc

`~/.config/labwc/autostart`:

```sh
~/.local/bin/qutaq-notify >/tmp/qutaq-notify.log 2>&1 &
```
