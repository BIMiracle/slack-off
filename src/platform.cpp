#include "app.h"
#include <dwmapi.h>
#include <algorithm>

namespace slack {
namespace {
bool ProcessIntegrity(HANDLE process, DWORD& level) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    bool ok = bytes && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), bytes, &bytes);
    if (ok) {
        auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data())->Label.Sid;
        ok = IsValidSid(sid) && *GetSidSubAuthorityCount(sid) > 0;
        if (ok) level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    }
    CloseHandle(token);
    return ok;
}
}
std::wstring FullPath(const std::wstring& path) {
    wchar_t buffer[32768];
    DWORD n = GetFullPathNameW(path.c_str(), 32768, buffer, nullptr);
    return n && n < 32768 ? std::wstring(buffer, n) : path;
}
bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
std::wstring WindowText(HWND window) {
    wchar_t text[512]{};
    GetWindowTextW(window, text, 512);
    return text;
}
bool GetIdentity(HWND window, Identity& identity) {
    if (!IsWindow(window)) return false;
    GetWindowThreadProcessId(window, &identity.pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, identity.pid);
    if (!process) return false;
    wchar_t path[32768];
    DWORD count = 32768;
    FILETIME created{}, exited{}, kernel{}, user{};
    bool ok = QueryFullProcessImageNameW(process, 0, path, &count) && GetProcessTimes(process, &created, &exited, &kernel, &user);
    CloseHandle(process);
    if (!ok) return false;
    identity.path.assign(path, count);
    identity.started = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true;
}
bool WindowRequiresElevation(HWND window) {
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(window, &pid)) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false; // Unknown access is reported by the actual capture operation.
    DWORD targetLevel = 0, ownLevel = 0;
    bool higher = ProcessIntegrity(process, targetLevel) && ProcessIntegrity(GetCurrentProcess(), ownLevel) && targetLevel > ownLevel;
    CloseHandle(process);
    return higher;
}
std::vector<HWND> TargetWindows(const std::wstring& path) {
    struct Context { const std::wstring* path; std::vector<HWND> windows; } context{&path, {}};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& ctx = *reinterpret_cast<Context*>(value);
        if (!IsWindowVisible(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return TRUE;
        // Explorer also owns the desktop and taskbar. Only its ordinary application
        // windows are eligible, so the tool's own tray/recovery route remains usable.
        wchar_t cls[256]{}; GetClassNameW(window, cls, 256);
        if (window == GetShellWindow() || window == GetDesktopWindow() ||
            wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
            wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
            wcscmp(cls, L"DesktopWindowXamlSource") == 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        Identity identity;
        if (GetIdentity(window, identity) && SamePath(identity.path, *ctx.path)) ctx.windows.push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.windows;
}
bool CaptureWindow(HWND window, Snapshot& snapshot) {
    if (!IsWindowVisible(window) || GetPropW(window, RecoveryProperty)) return false;
    snapshot.window = window;
    if (!GetIdentity(window, snapshot.identity) || !GetWindowPlacement(window, &snapshot.placement)) return false;
    wchar_t name[256];
    GetClassNameW(window, name, 256);
    snapshot.windowClass = name;
    static ULONG_PTR sequence = 0;
    snapshot.token = static_cast<ULONG_PTR>(GetTickCount64()) ^ (static_cast<ULONG_PTR>(GetCurrentProcessId()) << 32) ^ ++sequence;
    if (!snapshot.token) snapshot.token = 1;
    return SetPropW(window, RecoveryProperty, reinterpret_cast<HANDLE>(snapshot.token)) != FALSE;
}
bool ValidSnapshot(const Snapshot& snapshot) {
    if (reinterpret_cast<ULONG_PTR>(GetPropW(snapshot.window, RecoveryProperty)) != snapshot.token) return false;
    Identity identity;
    wchar_t name[256]{};
    GetClassNameW(snapshot.window, name, 256);
    return GetIdentity(snapshot.window, identity) && identity.pid == snapshot.identity.pid &&
        identity.started == snapshot.identity.started && SamePath(identity.path, snapshot.identity.path) && snapshot.windowClass == name;
}
bool RestoreWindow(const Snapshot& snapshot) {
    if (!ValidSnapshot(snapshot)) return true; // A destroyed/reused handle is never touched.
    auto placement = snapshot.placement;
    if (!SetWindowPlacement(snapshot.window, &placement)) return false;
    ShowWindowAsync(snapshot.window, static_cast<int>(placement.showCmd));
    RemovePropW(snapshot.window, RecoveryProperty);
    return true;
}
bool KeysReleased() {
    for (UINT key : {UINT(VK_CONTROL), UINT(VK_MENU), UINT(VK_SHIFT), UINT(VK_LWIN), UINT(VK_RWIN)})
        if (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) return false;
    return true;
}
bool SendBeforeKey(HWND window, Key key) {
    if (!key.vk) return true;
    if (!IsWindowVisible(window) || IsIconic(window) || !KeysReleased()) return false;
    if (GetForegroundWindow() != window) SetForegroundWindow(window);
    if (GetForegroundWindow() != window) return false;
    INPUT inputs[10]{};
    UINT count = 0;
    auto append = [&](WORD vk, bool up) {
        auto& input = inputs[count++]; input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        if (vk >= VK_PRIOR && vk <= VK_DELETE) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    };
    std::vector<WORD> modifiers;
    if (key.modifiers & MOD_CONTROL) modifiers.push_back(VK_CONTROL);
    if (key.modifiers & MOD_ALT) modifiers.push_back(VK_MENU);
    if (key.modifiers & MOD_SHIFT) modifiers.push_back(VK_SHIFT);
    if (key.modifiers & MOD_WIN) modifiers.push_back(VK_LWIN);
    for (auto vk : modifiers) append(vk, false);
    append(static_cast<WORD>(key.vk), false); append(static_cast<WORD>(key.vk), true);
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) append(*it, true);
    UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (sent != count) {
        // Release only keys that this sequence actually pressed.
        INPUT releases[5]{}; UINT n = 0;
        for (UINT i = 0; i < sent; ++i) if (!(inputs[i].ki.dwFlags & KEYEVENTF_KEYUP)) {
            releases[n] = inputs[i]; releases[n++].ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        if (n) SendInput(n, releases, sizeof(INPUT));
        return false;
    }
    return true; // Injection succeeded; the target's interpretation cannot be acknowledged.
}
bool CursorInside(HWND window, POINT point) {
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) && !GetWindowRect(window, &rect)) return false;
    return PtInRect(&rect, point) != FALSE;
}
std::wstring KeyText(Key key) {
    if (!key.vk) return L"未设置";
    std::wstring text;
    if (key.modifiers & MOD_CONTROL) {
        if (! (key.sides & CtrlSides)) text += L"Ctrl+";
        else { if (key.sides & LeftCtrl) text += L"左Ctrl+"; if (key.sides & RightCtrl) text += L"右Ctrl+"; }
    }
    if (key.modifiers & MOD_ALT) {
        if (! (key.sides & AltSides)) text += L"Alt+";
        else { if (key.sides & LeftAlt) text += L"左Alt+"; if (key.sides & RightAlt) text += L"右Alt+"; }
    }
    if (key.modifiers & MOD_SHIFT) text += L"Shift+";
    if (key.modifiers & MOD_WIN) text += L"Win+";
    wchar_t name[64]{};
    LONG scan = static_cast<LONG>(MapVirtualKeyW(key.vk, MAPVK_VK_TO_VSC) << 16);
    if (key.vk >= VK_PRIOR && key.vk <= VK_DELETE) scan |= 1 << 24;
    if (GetKeyNameTextW(scan, name, 64)) text += name;
    else text += std::to_wstring(key.vk);
    return text;
}
}
