#include "DisplayManager.hpp"
#include "Error.hpp"

namespace Graphic
{
Result<uint32_t, DisplayManager::Error>
DisplayManager::AddDisplay(UniquePtr<Display> display)
{
    if (_displays.Size() >= MAX_DISPLAYS)
        return Err<DisplayManager::Error>{Error::DisplayManagerFull};
    _displays.PushBack(std::move(display));
    return Ok<uint32_t>{
        static_cast<uint32_t>(_raw_display_count + _displays.Size() - 1)};
}

Result<uint32_t, DisplayManager::Error>
DisplayManager::AddDisplayRaw(Display *display)
{
    if (_raw_display_count >= MAX_DISPLAYS)
        return Err<DisplayManager::Error>{Error::DisplayManagerFull};
    _raw_displays[_raw_display_count] = display;
    return Ok<uint32_t>{_raw_display_count++};
}

bool DisplayManager::Flush()
{
    Display *disp = GetDisplay(_active_display);
    if (!disp)
        return false;
    disp->Flush();
    return true;
}

void DisplayManager::FlushAll()
{
    // raw displays
    for (size_t i = 0; i < _raw_display_count; ++i)
    {
        if (_raw_displays[i])
            _raw_displays[i]->Flush();
    }
    // unique ptr displays
    for (size_t i = 0; i < _displays.Size(); ++i)
    {
        _displays[i]->Flush();
    }
}

uint32_t *DisplayManager::GetBuffer()
{
    Display *disp = GetDisplay(_active_display);
    if (!disp)
        return nullptr;
    return disp->GetBuffer();
}

uint64_t DisplayManager::GetWidth()
{
    Display *disp = GetDisplay(_active_display);
    if (!disp)
        return 0;
    return disp->GetWidth();
}

uint64_t DisplayManager::GetHeight()
{
    Display *disp = GetDisplay(_active_display);
    if (!disp)
        return 0;
    return disp->GetHeight();
}

Result<uint32_t, DisplayManager::Error>
DisplayManager::SetActiveDisplay(uint32_t display_id)
{
    if (display_id >= _displays.Size())
        return Err<DisplayManager::Error>{Error::DisplayNotFound};
    _active_display = display_id;
    return Ok<uint32_t>{display_id};
}
} // namespace Graphic
