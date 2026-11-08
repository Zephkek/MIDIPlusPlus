#pragma once
#include <windows.h>
#include <functional>

void ShowLegitWindow(HWND owner, HFONT font, std::function<void()> onApply);
