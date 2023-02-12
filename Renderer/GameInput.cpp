#include "GameInput.h"
#include "Graphics.h"

#include <windowsx.h>
#include <bitset>

namespace
{
     uint8_t sMouseButtonDown = 0;
     uint64_t sKeyboardHold[256] = { 0 };
     std::bitset<512> sKeyboardDown;

    POINT sLastMousePos;
    POINT sMouseMoveDelta;
    INT sMouseScroll;
}

LRESULT CALLBACK GameInput::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        if (wParam != SIZE_MINIMIZED)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            Graphics::ResizeSwapChain(width, height);
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_MOUSEWHEEL:
    {
        sMouseScroll = GET_WHEEL_DELTA_WPARAM(wParam);
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        SetCapture(hWnd);

        sMouseButtonDown |= static_cast<uint8_t>(wParam);
        sLastMousePos.x = GET_X_LPARAM(lParam);
        sLastMousePos.y = -GET_Y_LPARAM(lParam);
        break;
    }
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    {
        ReleaseCapture();

        sMouseButtonDown &= static_cast<uint8_t>(wParam);
        break;
    }
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = -GET_Y_LPARAM(lParam);
        sMouseMoveDelta.x = x - sLastMousePos.x;
        sMouseMoveDelta.y = y - sLastMousePos.y;

        sLastMousePos.x = x;
        sLastMousePos.y = y;
        break;
    }
    case WM_KEYDOWN:
    {
        ASSERT(wParam > 0);
        if (sKeyboardDown.test(wParam))
            sKeyboardDown.set(wParam + 256);
        else
            sKeyboardDown.set(wParam);
        sKeyboardHold[wParam] += 1;
        break;
    }
    case WM_KEYUP:
    {
        sKeyboardDown.reset(wParam);
        sKeyboardDown.reset(wParam + 256);
        sKeyboardHold[wParam] = 0;
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

void GameInput::Update(float deltaTime)
{
    sMouseMoveDelta.x = 0;
    sMouseMoveDelta.y = 0;
}

bool GameInput::IsAnyPressed()
{
    return sKeyboardDown.any();
}

bool GameInput::IsPressed(int di)
{
    //Utility::PrintMessage("%d", (bool)sKeyboardDown[di]);
    return sKeyboardDown[di];
}

bool GameInput::IsFirstPressed(int di)
{
    ASSERT(di >= 0);
    return sKeyboardDown[di] && !sKeyboardDown[di + 256];
}

bool GameInput::IsReleased(int di)
{
    ASSERT(di >= 0);
    return !sKeyboardDown[di];
}

uint64_t GameInput::GetDurationPressed(int di)
{
    ASSERT(di >= 0);
    return sKeyboardHold[di];
}

INT GameInput::GetMouseInputX()
{
    if (sMouseButtonDown & MK_LBUTTON)
        return sMouseMoveDelta.x;
    else
        return 0;
}

INT GameInput::GetMouseInputY()
{
    if (sMouseButtonDown & MK_LBUTTON)
        return sMouseMoveDelta.y;
    else
        return 0;
}

INT GameInput::GetMouseScroll()
{
    return sMouseScroll;
}
