#pragma once

#include <stdint.h>

namespace Arch
{
namespace AArch64
{
namespace Timer
{

void Initialize();
void SetIntervalMs(uint32_t ms);
void Enable();
void Disable();

} // namespace Timer
} // namespace AArch64
} // namespace Arch
