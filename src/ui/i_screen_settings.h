#pragma once
#include <lvgl.h>

class SettingsStore;

// Common interface shared by vertical (ScreenSettings) and horizontal
// (ScreenSettingsH) settings screens.
class IScreenSettings {
public:
    virtual ~IScreenSettings() = default;
    virtual void update(const SettingsStore& store) = 0;
    virtual void refreshClock() = 0;
    virtual lv_obj_t* screen() const = 0;
};
