#include "GameInput.h"
#include "Graphics.h"
#include "ImGui/imgui.h"

#include <windowsx.h>
#include <bitset>

namespace
{
    uint8_t sMouseButtonDown = 0;
    uint64_t sKeyboardHold[256] = { 0 };
    std::bitset<256> sKeyboardDown;

    POINT sLastMousePos;
    POINT sMouseMoveDelta;
    INT sMouseScroll;
    bool sIsMouseTracked;
    GameInput::eInputLayer sCurrentTouchLayer;
    GameInput::eInputLayer sCurrentKeyboardLayer;
}

static ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
{
    switch (wParam)
    {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;
    case VK_NUMPAD0: return ImGuiKey_Keypad0;
    case VK_NUMPAD1: return ImGuiKey_Keypad1;
    case VK_NUMPAD2: return ImGuiKey_Keypad2;
    case VK_NUMPAD3: return ImGuiKey_Keypad3;
    case VK_NUMPAD4: return ImGuiKey_Keypad4;
    case VK_NUMPAD5: return ImGuiKey_Keypad5;
    case VK_NUMPAD6: return ImGuiKey_Keypad6;
    case VK_NUMPAD7: return ImGuiKey_Keypad7;
    case VK_NUMPAD8: return ImGuiKey_Keypad8;
    case VK_NUMPAD9: return ImGuiKey_Keypad9;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    //case IM_VK_KEYPAD_ENTER: return ImGuiKey_KeypadEnter;
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;
    case '0': return ImGuiKey_0;
    case '1': return ImGuiKey_1;
    case '2': return ImGuiKey_2;
    case '3': return ImGuiKey_3;
    case '4': return ImGuiKey_4;
    case '5': return ImGuiKey_5;
    case '6': return ImGuiKey_6;
    case '7': return ImGuiKey_7;
    case '8': return ImGuiKey_8;
    case '9': return ImGuiKey_9;
    case 'A': return ImGuiKey_A;
    case 'B': return ImGuiKey_B;
    case 'C': return ImGuiKey_C;
    case 'D': return ImGuiKey_D;
    case 'E': return ImGuiKey_E;
    case 'F': return ImGuiKey_F;
    case 'G': return ImGuiKey_G;
    case 'H': return ImGuiKey_H;
    case 'I': return ImGuiKey_I;
    case 'J': return ImGuiKey_J;
    case 'K': return ImGuiKey_K;
    case 'L': return ImGuiKey_L;
    case 'M': return ImGuiKey_M;
    case 'N': return ImGuiKey_N;
    case 'O': return ImGuiKey_O;
    case 'P': return ImGuiKey_P;
    case 'Q': return ImGuiKey_Q;
    case 'R': return ImGuiKey_R;
    case 'S': return ImGuiKey_S;
    case 'T': return ImGuiKey_T;
    case 'U': return ImGuiKey_U;
    case 'V': return ImGuiKey_V;
    case 'W': return ImGuiKey_W;
    case 'X': return ImGuiKey_X;
    case 'Y': return ImGuiKey_Y;
    case 'Z': return ImGuiKey_Z;
    case VK_F1: return ImGuiKey_F1;
    case VK_F2: return ImGuiKey_F2;
    case VK_F3: return ImGuiKey_F3;
    case VK_F4: return ImGuiKey_F4;
    case VK_F5: return ImGuiKey_F5;
    case VK_F6: return ImGuiKey_F6;
    case VK_F7: return ImGuiKey_F7;
    case VK_F8: return ImGuiKey_F8;
    case VK_F9: return ImGuiKey_F9;
    case VK_F10: return ImGuiKey_F10;
    case VK_F11: return ImGuiKey_F11;
    case VK_F12: return ImGuiKey_F12;
    default: return ImGuiKey_None;
    }
}

static void ImGui_ImplWin32_AddKeyEvent(ImGuiKey key, bool down, int native_keycode, int native_scancode = -1)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, native_scancode); // To support legacy indexing (<1.87 user code)
    IM_UNUSED(native_scancode);
}


LRESULT CALLBACK GameInput::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!Graphics::gApplicationInited)
        return DefWindowProc(hWnd, message, wParam, lParam);

    ImGuiIO& io = ImGui::GetIO();

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
        sMouseScroll = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

        if (io.WantCaptureMouse)
            io.AddMouseWheelEvent(0.0f, (float)sMouseScroll);
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

        int button = 0;
        if (message == WM_LBUTTONDOWN) { button = 0; }
        else if (message == WM_RBUTTONDOWN) { button = 1; }
        else if (message == WM_MBUTTONDOWN) { button = 2; }
        io.AddMouseButtonEvent(button, true);

        if (io.WantCaptureMouse)
            sCurrentTouchLayer = kImGui;
        else
            sCurrentTouchLayer = kEngineCore;
        break;
    }
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    {
        ReleaseCapture();

        sMouseButtonDown &= static_cast<uint8_t>(wParam);

        int button = 0;
        if (message == WM_LBUTTONUP) { button = 0; }
        else if (message == WM_RBUTTONUP) { button = 1; }
        else if (message == WM_MBUTTONUP) { button = 2; }
        io.AddMouseButtonEvent(button, false);

        sCurrentTouchLayer = kNone;
        break;
    }
    case WM_MOUSEMOVE:
    {
        if (!sIsMouseTracked)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            sIsMouseTracked = true;
        }

        int x = GET_X_LPARAM(lParam);
        int y = -GET_Y_LPARAM(lParam);
        sMouseMoveDelta.x = x - sLastMousePos.x;
        sMouseMoveDelta.y = y - sLastMousePos.y;

        sLastMousePos.x = x;
        sLastMousePos.y = y;

        io.AddMousePosEvent((float)sLastMousePos.x, (float)-sLastMousePos.y);
        break;
    }
    case WM_MOUSELEAVE:
    {
        sIsMouseTracked = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        sCurrentTouchLayer = kNone;
        break;
    }
    case WM_KEYDOWN:
    {
        //Utility::PrintMessage("WM_KEYDOWN %d", wParam);
        sKeyboardDown.set(wParam);
        sKeyboardHold[wParam] = 1;

        const ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
        const int scancode = (int)LOBYTE(HIWORD(lParam));
        if (key != ImGuiKey_None)
            ImGui_ImplWin32_AddKeyEvent(key, true, wParam, scancode);

        if (io.WantCaptureKeyboard)
            sCurrentKeyboardLayer = kImGui;
        else
            sCurrentKeyboardLayer = kEngineCore;
        break;
    }
    case WM_KEYUP:
    {
        //Utility::PrintMessage("WM_KEYUP %d", wParam);
        sKeyboardDown.reset(wParam);

        const ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
        const int scancode = (int)LOBYTE(HIWORD(lParam));
        if (key != ImGuiKey_None)
            ImGui_ImplWin32_AddKeyEvent(key, false, wParam, scancode);

        sCurrentKeyboardLayer = kNone;
        break;
    }
    case WM_CHAR:
        if (::IsWindowUnicode(hWnd))
        {
            // You can also use ToAscii()+GetKeyboardState() to retrieve characters.
            if (wParam > 0 && wParam < 0x10000)
                io.AddInputCharacterUTF16((unsigned short)wParam);
        }
        else
        {
            wchar_t wch = 0;
            ::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (char*)&wParam, 1, &wch, 1);
            io.AddInputCharacter(wch);
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

void GameInput::LateUpdate(float deltaTime)
{
    sMouseMoveDelta.x = 0;
    sMouseMoveDelta.y = 0;

    for (size_t i = 0; i < 256; i++)
    {
        if (!sKeyboardDown[i])
            sKeyboardHold[i] = 0;
        else if (sKeyboardHold[i] > 0)
            sKeyboardHold[i]++;
    }
}

bool GameInput::IsAnyPressed()
{
    return sKeyboardDown.any();
}

bool GameInput::IsPressed(int di)
{
    return sKeyboardDown[di];
}

bool GameInput::IsFirstPressed(int di)
{
    ASSERT(di >= 0 && di < 256);
    return sKeyboardDown[di] && sKeyboardHold[di] == 1;
}

bool GameInput::IsReleased(int di)
{
    ASSERT(di >= 0 && di < 256);
    return !sKeyboardDown[di];
}

bool GameInput::IsFirstReleased(int di)
{
    ASSERT(di >= 0 && di < 256);
    return !sKeyboardDown[di] && sKeyboardHold[di] > 0;
}

bool GameInput::GetAsyncKeyPressed(int di)
{
    return GetAsyncKeyState(di) & 0x8000;
}

uint64_t GameInput::GetDurationPressed(int di)
{
    ASSERT(di >= 0 && di < 256);
    return sKeyboardHold[di];
}

INT GameInput::GetMouseInputX(eInputLayer layer)
{
    if (layer != sCurrentTouchLayer)
        return 0;

    if (sMouseButtonDown & MK_LBUTTON)
        return sMouseMoveDelta.x;
    else
        return 0;
}

INT GameInput::GetMouseInputY(eInputLayer layer)
{
    if (layer != sCurrentTouchLayer)
        return 0;

    if (sMouseButtonDown & MK_LBUTTON)
        return sMouseMoveDelta.y;
    else
        return 0;
}

INT GameInput::GetMouseScroll(eInputLayer layer)
{
    if (layer != sCurrentTouchLayer)
        return 0;

    return sMouseScroll;
}
