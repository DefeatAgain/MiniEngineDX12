#pragma once
#include "CoreHeader.h"

namespace GameInput
{
    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void Update(float deltaTime);

    bool IsAnyPressed();
    bool IsPressed(int di);
    bool IsFirstPressed(int di);
    bool IsReleased(int di);
    //bool IsFirstReleased(int di);

    uint64_t GetDurationPressed(int di);

    INT GetMouseInputX();
    INT GetMouseInputY();
    INT GetMouseScroll();
}
