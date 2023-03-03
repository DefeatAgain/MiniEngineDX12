#pragma once
#include "CoreHeader.h"

namespace GameInput
{
    enum eInputLayer
    {
        kNone,
        kEngineCore,
        kImGui,
        kNumTouchLayer
    };

    typedef bool (*MouseEventHandler)(int di, bool isDownOrUp);
    typedef bool (*KeyEventHandler)(uint8_t mi, bool isDownOrUp);
    typedef bool (*ScrollEventHandler)(uint8_t mi, bool isDownOrUp);

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void LateUpdate(float deltaTime);

    bool IsAnyPressed();
    bool IsPressed(int di);
    bool IsFirstPressed(int di);
    bool IsReleased(int di);
    bool IsFirstReleased(int di);
    bool GetAsyncKeyPressed(int di);

    uint64_t GetDurationPressed(int di);

    INT GetMouseInputX(eInputLayer layer = kEngineCore);
    INT GetMouseInputY(eInputLayer layer = kEngineCore);
    INT GetMouseScroll(eInputLayer layer = kEngineCore);
}
