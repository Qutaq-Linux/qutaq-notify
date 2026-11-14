#pragma once

#include <QString>

// Volume and brightness keys: change the level, then show it in a gauge bubble (through
// org.freedesktop.Notifications, so it works with the qutaq-notify daemon running).
//   qutaq-notify --volume up|down|mute [STEP]     (wpctl, or pactl)
//   qutaq-notify --brightness up|down [STEP]      (brightnessctl)
//   qutaq-notify --osd ICON VALUE                  (just show a gauge)
namespace Osd {
int volume(const QString &action, int step);
int brightness(const QString &action, int step);
int show(const QString &kind, const QString &icon, int value);
}
