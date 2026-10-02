#include "app.h"
#include <algorithm>

namespace slack {
namespace {
Key Generic(Key key) { key.sides = 0; return key; }
}
bool ValidKey(Key key) {
    return key.modifiers <= 15 && key.vk <= 255 && key.sides <= (CtrlSides | AltSides) &&
        (!(key.sides & CtrlSides) || (key.modifiers & MOD_CONTROL)) &&
        (!(key.sides & AltSides) || (key.modifiers & MOD_ALT)) &&
        (key.vk || (!key.modifiers && !key.sides));
}
bool KeyOverlaps(Key a, Key b) {
    if (!a.vk || !b.vk || Generic(a) != Generic(b)) return false;
    for (UINT mask : {CtrlSides, AltSides})
        if ((a.sides & mask) && (b.sides & mask) && (a.sides & mask) != (b.sides & mask)) return false;
    return true;
}
bool KeyMatches(Key binding, Key pressed) {
    if (!binding.vk || Generic(binding) != Generic(pressed)) return false;
    for (UINT mask : {CtrlSides, AltSides})
        if ((binding.sides & mask) && (binding.sides & mask) != (pressed.sides & mask)) return false;
    return true;
}

Hotkeys* Hotkeys::hooked_ = nullptr;
Hotkeys::~Hotkeys() {
    if (keyboardHook_) { UnhookWindowsHookEx(keyboardHook_); hooked_ = nullptr; }
    for (const auto& item : registered_) UnregisterHotKey(owner_, item.second);
}
bool Hotkeys::Apply(const Config& config, std::wstring& error) {
    error.clear();
    std::vector<Key> desired{config.restore};
    for (const auto& rule : config.rules) { if (rule.toggle.vk) desired.push_back(rule.toggle); if (rule.watch.vk) desired.push_back(rule.watch); }
    for (size_t i = 0; i < desired.size(); ++i) {
        if (!desired[i].vk || !ValidKey(desired[i])) { error = L"快捷键无效：" + KeyText(desired[i]); return false; }
        for (size_t j = 0; j < i; ++j) if (KeyOverlaps(desired[i], desired[j])) {
            error = L"快捷键重复或左右键匹配范围重叠：" + KeyText(desired[i]); return false;
        }
    }
    std::map<Key, int> candidate;
    std::vector<int> added;
    for (auto binding : desired) {
        Key key = Generic(binding);
        if (candidate.count(key)) continue;
        if (auto found = registered_.find(key); found != registered_.end()) candidate[key] = found->second;
        else {
            int id = nextId_++;
            if (id > 0xBFFF) { nextId_ = 100; error = L"快捷键注册次数过多，请重启工具。"; goto rollback; }
            if (!RegisterHotKey(owner_, id, key.modifiers | MOD_NOREPEAT, key.vk)) { error = L"快捷键被系统或其他程序占用：" + KeyText(binding); goto rollback; }
            added.push_back(id); candidate[key] = id;
        }
    }
    // Also record reserved combinations while either shortcut input has focus.
    if (!keyboardHook_) {
        if (hooked_) { error = L"左右快捷键监测已被使用。"; goto rollback; }
        keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardCallback, GetModuleHandleW(nullptr), 0);
        if (!keyboardHook_) { error = L"无法安装左右快捷键监测，原快捷键已保留。"; goto rollback; }
        for (int vk = 0; vk < 256; ++vk) down_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        hooked_ = this;
    }
    for (const auto& item : registered_) if (!candidate.count(item.first)) UnregisterHotKey(owner_, item.second);
    registered_ = std::move(candidate);
    bindings_ = std::move(desired);
    return true;
rollback:
    for (int id : added) UnregisterHotKey(owner_, id);
    return false;
}
Key Hotkeys::Resolve(int id) const {
    for (const auto& item : registered_) if (item.second == id) {
        for (auto binding : bindings_) if (Generic(binding) == item.first && !binding.sides) return binding;
        // Sided groups are dispatched only with the state captured at key-down.
        break;
    }
    return {};
}
Key Hotkeys::ResolveSided(int id, Key pressed) const {
    auto found = registered_.find(Generic(pressed));
    if (found == registered_.end() || found->second != id) return {};
    for (auto binding : bindings_) if (binding.sides && KeyMatches(binding, pressed)) return binding;
    return {};
}
LRESULT CALLBACK Hotkeys::KeyboardCallback(int code, WPARAM kind, LPARAM data) {
    if (code == HC_ACTION && hooked_) {
        auto& self = *hooked_;
        const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        UINT vk = event.vkCode;
        if (vk == VK_CONTROL) vk = (event.flags & LLKHF_EXTENDED) ? VK_RCONTROL : VK_LCONTROL;
        if (vk == VK_MENU) vk = (event.flags & LLKHF_EXTENDED) ? VK_RMENU : VK_LMENU;
        if (vk == VK_SHIFT) vk = MapVirtualKeyW(event.scanCode, MAPVK_VSC_TO_VK_EX);
        if (vk < 256) {
            bool down = kind == WM_KEYDOWN || kind == WM_SYSKEYDOWN;
            bool repeat = self.down_[vk];
            self.down_[vk] = down;
            if (!down && self.captured_[vk]) { self.captured_[vk] = false; return 1; }
            if (down) {
                Key pressed{0, vk, 0};
                if (self.down_[VK_LCONTROL]) pressed.sides |= LeftCtrl;
                if (self.down_[VK_RCONTROL]) pressed.sides |= RightCtrl;
                if (self.down_[VK_LMENU]) pressed.sides |= LeftAlt;
                if (self.down_[VK_RMENU]) pressed.sides |= RightAlt;
                if (pressed.sides & CtrlSides) pressed.modifiers |= MOD_CONTROL;
                if (pressed.sides & AltSides) pressed.modifiers |= MOD_ALT;
                if (self.down_[VK_LSHIFT] || self.down_[VK_RSHIFT]) pressed.modifiers |= MOD_SHIFT;
                if (self.down_[VK_LWIN] || self.down_[VK_RWIN]) pressed.modifiers |= MOD_WIN;
                HWND focus = GetFocus();
                bool recording = GetForegroundWindow() == self.owner_ && GetPropW(focus, SideKeyProperty);
                bool modifier = vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
                    vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LWIN || vk == VK_RWIN;
                bool navigate = vk == VK_TAB && !(pressed.modifiers & (MOD_CONTROL | MOD_ALT));
                LPARAM packed = pressed.vk | (pressed.modifiers << 8) | (pressed.sides << 16);
                if (recording && !modifier && !navigate && !(pressed.modifiers & MOD_WIN)) {
                    if (!repeat) PostMessageW(focus, RecordKeyMessage, 0, packed);
                    self.captured_[vk] = true;
                    return 1; // Prevent an existing global registration from firing.
                }
                if (self.captured_[vk]) return 1;
                auto found = self.registered_.find(Generic(pressed));
                if (!repeat && !recording && found != self.registered_.end() && self.ResolveSided(found->second, pressed).vk) {
                    // No window operations or I/O in the hook. Preserve the state even
                    // if the user releases modifiers before the UI handles the message.
                    PostMessageW(self.owner_, SidedHotkeyMessage, found->second, packed);
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, kind, data);
}
}
