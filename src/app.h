#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace slack {
inline constexpr wchar_t WindowClass[] = L"SlackOff.Main.v1";
inline constexpr wchar_t RecoveryProperty[] = L"SlackOff.Recovery.v1";
inline constexpr UINT MouseExitMessage = WM_APP + 1;
inline constexpr UINT WindowEventMessage = WM_APP + 2;
inline constexpr UINT TrayMessage = WM_APP + 3;
inline constexpr UINT OpenMessage = WM_APP + 4;
inline constexpr UINT SidedHotkeyMessage = WM_APP + 5;
inline constexpr UINT RecordKeyMessage = WM_APP + 6;
inline constexpr UINT LeftCtrl = 1, RightCtrl = 2, LeftAlt = 4, RightAlt = 8;
inline constexpr UINT CtrlSides = LeftCtrl | RightCtrl, AltSides = LeftAlt | RightAlt;
inline constexpr wchar_t SideKeyProperty[] = L"SlackOff.SideKey";
struct Key {
    UINT modifiers = 0, vk = 0;
    UINT sides = 0; // Zero for a modifier family means either side (legacy settings).
    bool operator==(const Key& b) const { return modifiers == b.modifiers && vk == b.vk && sides == b.sides; }
    bool operator!=(const Key& b) const { return !(*this == b); }
    bool operator<(const Key& b) const { return modifiers < b.modifiers || (modifiers == b.modifiers && (vk < b.vk || (vk == b.vk && sides < b.sides))); }
};
bool ValidKey(Key key);
bool KeyOverlaps(Key a, Key b);
bool KeyMatches(Key binding, Key pressed);
struct Identity {
    DWORD pid = 0;
    uint64_t started = 0;
    std::wstring path;
};
struct Snapshot {
    HWND window = nullptr;
    Identity identity;
    std::wstring windowClass;
    ULONG_PTR token = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
};
struct Rule {
    std::wstring name, path;
    Key toggle, watch, before;
    UINT delay = 50;
    bool hidden = false;
    HWND recent = nullptr;
    std::vector<Snapshot> snapshots;
    std::wstring status = L"就绪 · 托盘隐藏未支持";
};
struct Config {
    std::vector<Rule> rules;
    Key restore{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F12};
    bool autoStart = true;
};
std::wstring FullPath(const std::wstring& path);
bool SamePath(const std::wstring& a, const std::wstring& b);
bool GetIdentity(HWND window, Identity& identity);
bool WindowRequiresElevation(HWND window);
bool ValidSnapshot(const Snapshot& snapshot);
std::vector<HWND> TargetWindows(const std::wstring& path);
std::wstring WindowText(HWND window);
bool CaptureWindow(HWND window, Snapshot& snapshot);
bool RestoreWindow(const Snapshot& snapshot);
bool SendBeforeKey(HWND window, Key key);
bool KeysReleased();
std::wstring KeyText(Key key);
bool CursorInside(HWND window, POINT point);
class Settings {
public:
    explicit Settings(std::wstring directory = L"");
    bool Load(Config& config);
    bool Save(const Config& config);
    bool SaveJournal(const std::vector<Rule>& rules);
    std::vector<Snapshot> ReadJournal();
    bool Recover();
    bool SetAutoStart(bool enable);
    bool IsAutoStart() const;
    const std::wstring& Directory() const { return directory_; }
private:
    std::wstring directory_;
};
class Hotkeys {
public:
    explicit Hotkeys(HWND owner) : owner_(owner) {}
    ~Hotkeys();
    bool Apply(const Config& config, std::wstring& error);
    Key Resolve(int id) const;
    Key ResolveSided(int id, Key pressed) const;
private:
    static LRESULT CALLBACK KeyboardCallback(int code, WPARAM kind, LPARAM data);
    static Hotkeys* hooked_;
    HWND owner_;
    int nextId_ = 100;
    HHOOK keyboardHook_ = nullptr;
    bool down_[256]{};
    bool captured_[256]{};
    std::vector<Key> bindings_;
    // RegisterHotKey reserves a generic combination once, even for several sides.
    std::map<Key, int> registered_;
};
}
