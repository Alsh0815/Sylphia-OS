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
    return Ok<uint32_t>{static_cast<uint32_t>(_displays.Size() - 1)};
}

bool DisplayManager::Flush()
{
    if (_displays.Size() == 0)
        return false;
    _displays[_active_display]->Flush();
    return true;
}

void DisplayManager::FlushAll()
{
    for (size_t i = 0; i < _displays.Size(); ++i)
    {
        _displays[i]->Flush();
    }
}

uint32_t *DisplayManager::GetBuffer()
{
    if (_displays.Size() == 0)
        return nullptr;
    return _displays[_active_display]->GetBuffer();
}

uint64_t DisplayManager::GetWidth()
{
    if (_displays.Size() == 0)
        return 0;
    return _displays[_active_display]->GetWidth();
}

uint64_t DisplayManager::GetHeight()
{
    if (_displays.Size() == 0)
        return 0;
    return _displays[_active_display]->GetHeight();
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
