#include "app.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <fstream>
#include <filesystem>

using namespace slack;
namespace {
enum Control : int { List = 10, Picker, Refresh, UseWindow, Browse, Name, Path, ToggleKey, WatchKey, BeforeKey,
    Delay, SaveRule, NewRule, DeleteRule, RestoreAll, StopWatch, Startup, RestoreKey, SaveGeneral, Elevate, Status };
constexpr UINT_PTR PendingTimer = 1;
constexpr UINT_PTR ResumeTimer = 2;
constexpr int MenuOpen = 400, MenuRestore = 401, MenuStop = 402, MenuStartup = 403, MenuExit = 404, MenuRule = 500;
struct App {
    HWND window = nullptr;
    Settings settings;
    Config config;
    Hotkeys* hotkeys = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96, taskbarCreated = 0;
    int selected = -1, watchRule = -1, pendingRule = -1;
    HWND watchWindow = nullptr, pendingWindow = nullptr;
    RECT watchBounds{};
    HHOOK mouseHook = nullptr;
    HWINEVENTHOOK events = nullptr, foregroundEvents = nullptr;
    bool inside = false, exitQueued = false, pendingSent = false, keyConfirmed = true;
    bool testInstance = false, updatingList = false;
    ULONGLONG pendingStarted = 0, hideAt = 0;
    ULONGLONG resumeStarted = 0;
    std::vector<std::pair<std::wstring, std::wstring>> choices;
    std::vector<HWND> controls;
    std::wstring message = L"设置程序和快捷键后点击保存。第三方托盘图标：未支持。";
    HWND C(int id) const { return GetDlgItem(window, id); }
    void Error(const std::wstring& text) { MessageBoxW(window, text.c_str(), L"SlackOff", MB_OK | MB_ICONWARNING); }
    void Notify(const std::wstring& text) { message = text; SetWindowTextW(C(Status), text.c_str()); }
    void CreateUI();
    void Layout();
    void RefreshChoices();
    void RefreshList();
    void Edit(int index);
    bool Save(bool generalOnly);
    void AddTray();
    void TrayMenu();
    HWND BestWindow(size_t index);
    bool HideWindows(size_t index);
    void BeginHide(size_t index);
    void FinishHide();
    bool Restore(size_t index);
    bool RestoreEverything();
    void Toggle(size_t index);
    void SetWatch(int index);
    void StopMonitoring(bool forget);
    void ResumeMonitoring();
    void MouseMove(POINT point);
    void OnWindowEvent(HWND target, DWORD event);
    void Command(int id);
};
App* app = nullptr;
int Scale(int value) { return MulDiv(value, static_cast<int>(app->dpi), 96); }
std::wstring Text(HWND control) { int n = GetWindowTextLengthW(control); std::wstring text(n + 1, L'\0'); GetWindowTextW(control, text.data(), n + 1); text.resize(n); return text; }
Key UnpackKey(LPARAM packed) {
    return {static_cast<UINT>((packed >> 8) & 0xFF), static_cast<UINT>(packed & 0xFF), static_cast<UINT>((packed >> 16) & 0xFF)};
}
void ShowSideKey(HWND control, Key key) {
    *reinterpret_cast<Key*>(GetPropW(control, SideKeyProperty)) = key;
    SetWindowTextW(control, KeyText(key).c_str());
}
LRESULT CALLBACK SideKeyProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclass, DWORD_PTR) {
    if (message == RecordKeyMessage) {
        Key key = UnpackKey(lParam);
        if (key.vk == VK_BACK || key.vk == VK_DELETE) key = {};
        if (ValidKey(key)) ShowSideKey(control, key);
        return 0;
    }
    if (message == WM_GETDLGCODE) {
        auto input = reinterpret_cast<MSG*>(lParam);
        if (input && input->wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
            return DLGC_WANTCHARS | DLGC_WANTARROWS;
        return DLGC_WANTALLKEYS;
    }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (wParam == VK_BACK || wParam == VK_DELETE) ShowSideKey(control, {});
        return 0;
    }
    if (message == WM_CHAR || message == WM_SYSCHAR || message == WM_SYSKEYUP) return 0;
    if (message == WM_NCDESTROY) {
        delete reinterpret_cast<Key*>(RemovePropW(control, SideKeyProperty));
        RemoveWindowSubclass(control, SideKeyProc, subclass);
    }
    return DefSubclassProc(control, message, wParam, lParam);
}
Key ReadKey(HWND control) {
    if (auto key = reinterpret_cast<Key*>(GetPropW(control, SideKeyProperty))) return *key;
    WORD value = static_cast<WORD>(SendMessageW(control, HKM_GETHOTKEY, 0, 0));
    UINT mods = 0; BYTE flags = HIBYTE(value);
    if (flags & HOTKEYF_CONTROL) mods |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) mods |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT) mods |= MOD_SHIFT;
    return {mods, LOBYTE(value)};
}
void WriteKey(HWND control, Key key) {
    if (GetPropW(control, SideKeyProperty)) { ShowSideKey(control, key); return; }
    BYTE flags = 0;
    if (key.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (key.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    if (key.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    SendMessageW(control, HKM_SETHOTKEY, MAKEWORD(key.vk, flags), 0);
}
void CALLBACK EventCallback(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (!app || !window) return;
    if (event != EVENT_SYSTEM_FOREGROUND && event != EVENT_SYSTEM_MINIMIZESTART && event != EVENT_OBJECT_SHOW &&
        event != EVENT_OBJECT_HIDE && event != EVENT_OBJECT_DESTROY && event != EVENT_OBJECT_LOCATIONCHANGE) return;
    if (event != EVENT_SYSTEM_FOREGROUND && (object != OBJID_WINDOW || child != CHILDID_SELF)) return;
    PostMessageW(app->window, WindowEventMessage, reinterpret_cast<WPARAM>(window), event);
}
LRESULT CALLBACK MouseCallback(int code, WPARAM kind, LPARAM data) {
    if (code == HC_ACTION && kind == WM_MOUSEMOVE && app) {
        auto& event = *reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        app->MouseMove(event.pt);
    }
    return CallNextHookEx(nullptr, code, kind, data);
}
void App::MouseMove(POINT point) {
    if (!watchWindow || exitQueued) return;
    bool current = PtInRect(&watchBounds, point) != FALSE;
    if (current) inside = true;
    else if (inside) { exitQueued = true; PostMessageW(window, MouseExitMessage, 0, 0); }
}
void App::CreateUI() {
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        HWND child = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
            WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        controls.push_back(child); return child;
    };
    make(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, List);
    ListView_SetExtendedListViewStyle(C(List), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* columns[] = {L"程序", L"隐藏 / 恢复", L"看剧模式", L"状态"};
    for (int i = 0; i < 4; ++i) { LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = const_cast<LPWSTR>(columns[i]); col.cx = 160; ListView_InsertColumn(C(List), i, &col); }
    make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, Picker);
    make(L"BUTTON", L"刷新窗口", WS_TABSTOP, Refresh);
    make(L"BUTTON", L"选用程序", WS_TABSTOP, UseWindow);
    make(L"BUTTON", L"浏览 EXE…", WS_TABSTOP, Browse);
    make(L"STATIC", L"名称", 0, 1000);
    make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Name);
    make(L"STATIC", L"程序路径", 0, 1001);
    make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Path);
    make(L"STATIC", L"隐藏 / 恢复快捷键", 0, 1002);
    make(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, ToggleKey);
    make(L"STATIC", L"看剧模式快捷键", 0, 1003);
    make(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, WatchKey);
    for (int id : {ToggleKey, WatchKey}) {
        SetPropW(C(id), SideKeyProperty, reinterpret_cast<HANDLE>(new Key{}));
        SetWindowSubclass(C(id), SideKeyProc, 1, 0);
        SendMessageW(C(id), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"按下组合键，区分左右 Ctrl / Alt"));
    }
    make(L"STATIC", L"隐藏前按键（可留空）", 0, 1004);
    make(HOTKEY_CLASSW, L"", WS_TABSTOP, BeforeKey);
    make(L"STATIC", L"按键后延迟（0–500 ms）", 0, 1005);
    make(L"EDIT", L"50", ES_NUMBER | WS_TABSTOP, Delay);
    make(L"BUTTON", L"保存程序", WS_TABSTOP | BS_DEFPUSHBUTTON, SaveRule);
    make(L"BUTTON", L"新增", WS_TABSTOP, NewRule);
    make(L"BUTTON", L"删除", WS_TABSTOP, DeleteRule);
    make(L"BUTTON", L"恢复全部", WS_TABSTOP, RestoreAll);
    make(L"BUTTON", L"关闭看剧模式", WS_TABSTOP, StopWatch);
    make(L"BUTTON", L"登录时自动启动并驻留托盘", WS_TABSTOP | BS_AUTOCHECKBOX, Startup);
    make(L"STATIC", L"恢复全部快捷键", 0, 1006);
    make(HOTKEY_CLASSW, L"", WS_TABSTOP, RestoreKey);
    make(L"BUTTON", L"保存全局设置", WS_TABSTOP, SaveGeneral);
    make(L"BUTTON", L"以管理员身份重启", WS_TABSTOP, Elevate);
    make(L"STATIC", L"光标移出目标窗口即隐藏；先将窗口手动缩小。恢复时不自动播放。\r\n工具自身驻留托盘；其他程序的托盘图标目前无法通用隐藏。", 0, 1007);
    make(L"STATIC", message.c_str(), 0, Status);
    SendMessageW(C(Startup), BM_SETCHECK, config.autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    WriteKey(C(RestoreKey), config.restore);
    Layout(); RefreshChoices(); RefreshList(); Edit(-1);
}
void App::Layout() {
    RECT area; GetClientRect(window, &area);
    int width = MulDiv(area.right, 96, static_cast<int>(dpi));
    if (font) DeleteObject(font);
    font = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    for (HWND child : controls) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    auto place = [&](int id, int x, int y, int w, int h) { MoveWindow(C(id), Scale(x), Scale(y), Scale(w), Scale(h), TRUE); };
    place(List, 16, 16, width - 32, 180);
    ListView_SetColumnWidth(C(List), 0, Scale(160)); ListView_SetColumnWidth(C(List), 1, Scale(160));
    ListView_SetColumnWidth(C(List), 2, Scale(160)); ListView_SetColumnWidth(C(List), 3, Scale(width - 530));
    place(Picker, 16, 208, width - 340, 250); place(Refresh, width - 314, 208, 94, 28);
    place(UseWindow, width - 212, 208, 94, 28); place(Browse, width - 110, 208, 94, 28);
    place(1000, 16, 252, 60, 24); place(Name, 82, 248, 190, 28);
    place(1001, 288, 252, 72, 24); place(Path, 362, 248, width - 378, 28);
    int half = (width - 48) / 2;
    place(1002, 16, 292, 165, 24); place(ToggleKey, 182, 288, half - 166, 28);
    place(1003, half + 32, 292, 154, 24); place(WatchKey, half + 188, 288, half - 156, 28);
    place(1004, 16, 332, 165, 24); place(BeforeKey, 182, 328, half - 166, 28);
    place(1005, half + 32, 332, 180, 24); place(Delay, half + 222, 328, 110, 28);
    place(SaveRule, 16, 372, 110, 30); place(NewRule, 134, 372, 72, 30); place(DeleteRule, 214, 372, 72, 30);
    place(RestoreAll, 310, 372, 110, 30); place(StopWatch, 428, 372, 144, 30);
    place(Startup, 16, 420, 320, 28); place(Elevate, width - 200, 420, 184, 28);
    place(1006, 16, 464, 155, 24); place(RestoreKey, 182, 460, 200, 28); place(SaveGeneral, 398, 460, 144, 28);
    place(1007, 16, 508, width - 32, 52); place(Status, 16, 572, width - 32, 48);
}
void App::RefreshChoices() {
    choices.clear(); SendMessageW(C(Picker), CB_RESETCONTENT, 0, 0);
    EnumWindows([](HWND target, LPARAM) -> BOOL {
        if (!IsWindowVisible(target) || WindowText(target).empty()) return TRUE;
        Identity identity;
        if (!GetIdentity(target, identity) || identity.pid == GetCurrentProcessId()) return TRUE;
        auto& choices = app->choices;
        for (const auto& choice : choices) if (SamePath(choice.second, identity.path)) return TRUE;
        choices.emplace_back(WindowText(target), identity.path);
        SendMessageW(app->C(Picker), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choices.back().first.c_str()));
        return TRUE;
    }, 0);
    if (!choices.empty()) SendMessageW(C(Picker), CB_SETCURSEL, 0, 0);
}
void App::RefreshList() {
    updatingList = true;
    ListView_DeleteAllItems(C(List));
    for (size_t i = 0; i < config.rules.size(); ++i) {
        auto& rule = config.rules[i];
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = rule.name.data();
        ListView_InsertItem(C(List), &item);
        auto toggle = KeyText(rule.toggle), watch = KeyText(rule.watch);
        auto state = rule.status;
        if (static_cast<int>(i) == watchRule && !rule.hidden) state = L"看剧模式：" + std::wstring(inside ? L"正在监测" : L"等待光标进入") + L" · 托盘隐藏未支持";
        ListView_SetItemText(C(List), static_cast<int>(i), 1, toggle.data());
        ListView_SetItemText(C(List), static_cast<int>(i), 2, watch.data());
        ListView_SetItemText(C(List), static_cast<int>(i), 3, state.data());
    }
    if (selected >= 0 && selected < static_cast<int>(config.rules.size())) ListView_SetItemState(C(List), selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    updatingList = false;
}
void App::Edit(int index) {
    selected = index;
    bool wasUpdating = updatingList; updatingList = true;
    ListView_SetItemState(C(List), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (index >= 0) ListView_SetItemState(C(List), index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    updatingList = wasUpdating;
    Rule blank;
    const auto& rule = index >= 0 && index < static_cast<int>(config.rules.size()) ? config.rules[index] : blank;
    SetWindowTextW(C(Name), rule.name.c_str()); SetWindowTextW(C(Path), rule.path.c_str());
    WriteKey(C(ToggleKey), rule.toggle); WriteKey(C(WatchKey), rule.watch); WriteKey(C(BeforeKey), rule.before);
    SetWindowTextW(C(Delay), std::to_wstring(rule.delay).c_str());
}
bool App::Save(bool generalOnly) {
    if (pendingRule >= 0) { Error(L"正在执行隐藏，请稍后保存。"); return false; }
    Config next = config;
    if (generalOnly) {
        next.restore = ReadKey(C(RestoreKey));
        next.autoStart = SendMessageW(C(Startup), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!next.restore.vk) { Error(L"请设置恢复全部快捷键。"); return false; }
    } else {
        Rule rule;
        rule.name = Text(C(Name)); rule.path = FullPath(Text(C(Path)));
        rule.toggle = ReadKey(C(ToggleKey)); rule.watch = ReadKey(C(WatchKey)); rule.before = ReadKey(C(BeforeKey));
        auto delayText = Text(C(Delay));
        wchar_t* end = nullptr; long delay = wcstol(delayText.c_str(), &end, 10);
        if (delayText.empty() || !end || *end || delay < 0 || delay > 500) { Error(L"延迟必须为 0–500 毫秒。"); return false; }
        rule.delay = static_cast<UINT>(delay);
        DWORD attrs = GetFileAttributesW(rule.path.c_str());
        auto extension = std::filesystem::path(rule.path).extension().wstring();
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) || !SamePath(extension, L".exe")) { Error(L"请选择有效的 EXE 文件。"); return false; }
        wchar_t own[32768]; GetModuleFileNameW(nullptr, own, 32768);
        if (SamePath(rule.path, own)) { Error(L"不能将本工具作为隐藏目标。"); return false; }
        if (!rule.toggle.vk) { Error(L"请设置隐藏 / 恢复快捷键。"); return false; }
        if (rule.before.vk && (KeyOverlaps(rule.before, rule.toggle) || KeyOverlaps(rule.before, rule.watch) || KeyOverlaps(rule.before, config.restore))) { Error(L"隐藏前按键不能与本工具的全局快捷键相同。"); return false; }
        if (rule.name.empty()) rule.name = std::filesystem::path(rule.path).stem().wstring();
        for (size_t i = 0; i < next.rules.size(); ++i) if (static_cast<int>(i) != selected && SamePath(next.rules[i].path, rule.path)) { Error(L"这个程序已有配置，请选择列表中的现有规则。"); return false; }
        if (selected >= 0) {
            if (config.rules[selected].hidden && !Restore(static_cast<size_t>(selected))) return false;
            next = config;
            next.rules[selected] = rule;
        } else { if (next.rules.size() >= 128) { Error(L"最多支持 128 条规则。"); return false; } next.rules.push_back(rule); }
    }
    // A pre-hide key must never recursively invoke any registered action.
    for (const auto& rule : next.rules) if (rule.before.vk) {
        if (KeyOverlaps(rule.before, next.restore)) { Error(L"隐藏前按键与恢复全部快捷键冲突。"); return false; }
        for (const auto& other : next.rules) if (KeyOverlaps(rule.before, other.toggle) || KeyOverlaps(rule.before, other.watch)) { Error(L"隐藏前按键与全局快捷键冲突。"); return false; }
    }
    std::wstring error;
    if (!hotkeys->Apply(next, error)) { Error(error); return false; }
    bool startupChanged = generalOnly && next.autoStart != config.autoStart;
    if (startupChanged && !testInstance && !settings.SetAutoStart(next.autoStart)) { hotkeys->Apply(config, error); Error(L"无法更新开机自启，配置未保存。"); return false; }
    if (!settings.Save(next)) {
        hotkeys->Apply(config, error);
        if (startupChanged && !testInstance) settings.SetAutoStart(config.autoStart);
        Error(L"无法写入设置，原快捷键已保留。"); return false;
    }
    if (!generalOnly && watchRule == selected) StopMonitoring(true);
    config = std::move(next);
    if (!generalOnly) Edit(selected >= 0 ? selected : static_cast<int>(config.rules.size()) - 1);
    RefreshList();
    Notify(L"已保存。第三方托盘图标：未支持。");
    return true;
}
void App::AddTray() {
    NOTIFYICONDATAW tray{sizeof(tray)}; tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; tray.uCallbackMessage = TrayMessage;
    tray.hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM)); wcscpy_s(tray.szTip, L"SlackOff · 快捷隐藏");
    Shell_NotifyIconW(NIM_ADD, &tray);
}
void App::TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MenuOpen, L"打开设置");
    AppendMenuW(menu, MF_STRING, MenuRestore, L"恢复全部窗口");
    AppendMenuW(menu, MF_STRING | (watchRule < 0 ? MF_GRAYED : 0), MenuStop, L"关闭看剧模式");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < config.rules.size(); ++i) {
        auto text = config.rules[i].name + (config.rules[i].hidden ? L" — 已隐藏 / 点击恢复" : L" — 点击隐藏");
        AppendMenuW(menu, MF_STRING, MenuRule + static_cast<UINT>(i), text.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (config.autoStart ? MF_CHECKED : 0), MenuStartup, L"开机自启");
    AppendMenuW(menu, MF_STRING, MenuExit, L"退出（先恢复全部窗口）");
    POINT cursor; GetCursorPos(&cursor); SetForegroundWindow(window);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
    DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
    if (choice) Command(choice);
}
HWND App::BestWindow(size_t index) {
    auto windows = TargetWindows(config.rules[index].path);
    auto foreground = GetForegroundWindow();
    if (std::find(windows.begin(), windows.end(), foreground) != windows.end()) return foreground;
    auto recent = config.rules[index].recent;
    if (std::find(windows.begin(), windows.end(), recent) != windows.end()) return recent;
    for (auto target : windows) if (!IsIconic(target) && !GetWindow(target, GW_OWNER)) return target;
    return windows.empty() ? nullptr : windows.front();
}
bool App::HideWindows(size_t index) {
    auto& rule = config.rules[index];
    bool success = true;
    auto windows = TargetWindows(rule.path);
    for (auto target : windows) {
        auto existing = std::find_if(rule.snapshots.begin(), rule.snapshots.end(), [&](const Snapshot& snap) { return snap.window == target && ValidSnapshot(snap); });
        if (existing != rule.snapshots.end()) { ShowWindowAsync(target, SW_HIDE); continue; }
        Snapshot snap;
        if (!CaptureWindow(target, snap)) { success = false; continue; }
        rule.snapshots.push_back(snap);
        // Commit the recovery identity and placement before touching visibility.
        if (!settings.SaveJournal(config.rules)) {
            rule.snapshots.pop_back(); RemovePropW(target, RecoveryProperty); success = false; continue;
        }
        ShowWindowAsync(target, SW_HIDE);
    }
    rule.hidden = !rule.snapshots.empty();
    rule.status = success ? L"已隐藏 · 托盘隐藏未支持" : L"部分窗口隐藏失败 · 托盘隐藏未支持";
    if (!rule.hidden) rule.status = L"未找到可隐藏窗口或权限不足 · 托盘隐藏未支持";
    if (rule.hidden && !keyConfirmed) rule.status = L"隐藏成功，按键发送未确认 · 托盘隐藏未支持";
    return success && rule.hidden;
}
void App::BeginHide(size_t index) {
    if (pendingRule >= 0) { Notify(L"正在执行隐藏，请稍候。"); return; }
    auto target = BestWindow(index);
    if (!target) { Notify(L"目标程序没有可见窗口。"); if (watchRule == static_cast<int>(index)) StopMonitoring(true); return; }
    pendingRule = static_cast<int>(index); pendingWindow = target;
    config.rules[index].recent = target;
    pendingSent = false; keyConfirmed = true; pendingStarted = GetTickCount64(); hideAt = 0;
    if (watchRule == pendingRule) StopMonitoring(false);
    if (!config.rules[index].before.vk) { FinishHide(); return; }
    SetTimer(window, PendingTimer, 15, nullptr);
}
void App::FinishHide() {
    if (pendingRule < 0) return;
    KillTimer(window, PendingTimer);
    int index = pendingRule; pendingRule = -1;
    HideWindows(static_cast<size_t>(index)); RefreshList(); Notify(config.rules[index].status);
}
bool App::Restore(size_t index) {
    auto& rule = config.rules[index];
    std::vector<Snapshot> failed;
    for (const auto& snap : rule.snapshots) if (!RestoreWindow(snap)) failed.push_back(snap);
    rule.snapshots = std::move(failed); rule.hidden = !rule.snapshots.empty();
    if (!settings.SaveJournal(config.rules)) Notify(L"恢复记录更新失败，已恢复的窗口不会重复操作。");
    rule.status = rule.hidden ? L"部分窗口恢复失败 · 托盘隐藏未支持" : L"已恢复 · 托盘隐藏未支持";
    if (!rule.hidden) {
        HWND target = BestWindow(index);
        if (target) SetForegroundWindow(target);
        if (watchRule == static_cast<int>(index)) {
            resumeStarted = GetTickCount64(); SetTimer(window, ResumeTimer, 15, nullptr);
        }
    }
    RefreshList(); return !rule.hidden;
}
bool App::RestoreEverything() {
    KillTimer(window, PendingTimer); pendingRule = -1;
    StopMonitoring(true);
    bool ok = true;
    for (size_t i = 0; i < config.rules.size(); ++i) if (config.rules[i].hidden) ok = Restore(i) && ok;
    ok = settings.Recover() && ok;
    Notify(ok ? L"已恢复全部窗口。" : L"部分窗口恢复失败，请检查权限后重试。"); return ok;
}
void App::Toggle(size_t index) { if (pendingRule >= 0) return; if (config.rules[index].hidden) Restore(index); else BeginHide(index); }
void App::StopMonitoring(bool forget) {
    KillTimer(window, ResumeTimer);
    if (mouseHook) UnhookWindowsHookEx(mouseHook);
    mouseHook = nullptr; watchWindow = nullptr; inside = false; exitQueued = false;
    if (forget) watchRule = -1;
    RefreshList();
}
void App::ResumeMonitoring() {
    if (watchRule < 0) return;
    if (config.rules[watchRule].hidden) return;
    watchWindow = BestWindow(static_cast<size_t>(watchRule));
    inside = false; exitQueued = false;
    if (!watchWindow || IsIconic(watchWindow) || !IsWindowVisible(watchWindow)) { StopMonitoring(true); return; }
    if (FAILED(DwmGetWindowAttribute(watchWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &watchBounds, sizeof(watchBounds)))) GetWindowRect(watchWindow, &watchBounds);
    if (mouseHook) UnhookWindowsHookEx(mouseHook);
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseCallback, GetModuleHandleW(nullptr), 0);
    if (!mouseHook) { StopMonitoring(true); Notify(L"鼠标监测安装失败。"); return; }
    // Arming waits for the first movement inside, also after a restore.
    RefreshList(); Notify(L"看剧模式已开启：光标进入目标窗口后开始监测。");
}
void App::SetWatch(int index) {
    if (pendingRule >= 0) return;
    if (watchRule == index) { StopMonitoring(true); Notify(L"看剧模式已关闭。"); return; }
    StopMonitoring(true);
    if (config.rules[index].hidden) {
        watchRule = index;
        if (!Restore(static_cast<size_t>(index))) StopMonitoring(true);
        return;
    }
    watchRule = index; ResumeMonitoring();
}
void App::OnWindowEvent(HWND target, DWORD event) {
    if (target == watchWindow && event == EVENT_OBJECT_LOCATIONCHANGE) {
        if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &watchBounds, sizeof(watchBounds)))) GetWindowRect(target, &watchBounds);
    }
    if (event == EVENT_SYSTEM_FOREGROUND) {
        Identity identity;
        if (GetIdentity(target, identity)) for (auto& rule : config.rules) if (SamePath(rule.path, identity.path)) rule.recent = target;
    }
    if (event == EVENT_OBJECT_SHOW) {
        Identity identity;
        if (IsWindowVisible(target) && GetIdentity(target, identity)) for (size_t i = 0; i < config.rules.size(); ++i)
            if (config.rules[i].hidden && SamePath(config.rules[i].path, identity.path)) { keyConfirmed = true; HideWindows(i); RefreshList(); break; }
    }
    if (watchWindow == target && (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE || event == EVENT_SYSTEM_MINIMIZESTART)) StopMonitoring(true);
    if (event == EVENT_OBJECT_DESTROY) {
        bool changed = false, stopWatch = false;
        for (size_t i = 0; i < config.rules.size(); ++i) {
            auto& rule = config.rules[i];
            auto size = rule.snapshots.size();
            rule.snapshots.erase(std::remove_if(rule.snapshots.begin(), rule.snapshots.end(), [&](const Snapshot& snap) { return snap.window == target && !ValidSnapshot(snap); }), rule.snapshots.end());
            if (size != rule.snapshots.size()) { changed = true; if (rule.snapshots.empty()) { rule.hidden = false; rule.status = L"目标窗口已关闭 · 托盘隐藏未支持"; if (watchRule == static_cast<int>(i)) stopWatch = true; } }
        }
        if (stopWatch) StopMonitoring(true);
        if (changed) { settings.SaveJournal(config.rules); RefreshList(); }
    }
}
void App::Command(int id) {
    if (id >= MenuRule && id < MenuRule + static_cast<int>(config.rules.size())) { Toggle(static_cast<size_t>(id - MenuRule)); return; }
    switch (id) {
    case MenuOpen: ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); break;
    case Refresh: RefreshChoices(); break;
    case UseWindow: {
        int index = static_cast<int>(SendMessageW(C(Picker), CB_GETCURSEL, 0, 0));
        if (index >= 0 && index < static_cast<int>(choices.size())) {
            Edit(-1); SetWindowTextW(C(Path), choices[index].second.c_str());
            auto name = std::filesystem::path(choices[index].second).stem().wstring(); SetWindowTextW(C(Name), name.c_str());
        } break;
    }
    case Browse: {
        wchar_t file[32768]{}; OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = window;
        dialog.lpstrFilter = L"应用程序 (*.exe)\0*.exe\0\0"; dialog.lpstrFile = file; dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) { Edit(-1); SetWindowTextW(C(Path), file); auto name = std::filesystem::path(file).stem().wstring(); SetWindowTextW(C(Name), name.c_str()); } break;
    }
    case NewRule: Edit(-1); SetFocus(C(Name)); break;
    case SaveRule: Save(false); break;
    case SaveGeneral: Save(true); break;
    case DeleteRule: {
        if (selected < 0 || pendingRule >= 0) break;
        int index = selected;
        if (config.rules[index].hidden && !Restore(static_cast<size_t>(index))) break;
        Config next = config; next.rules.erase(next.rules.begin() + index);
        std::wstring error;
        if (!hotkeys->Apply(next, error)) { Error(error); break; }
        if (!settings.Save(next)) { hotkeys->Apply(config, error); Error(L"删除保存失败，原规则已保留。"); break; }
        StopMonitoring(true); config = std::move(next); Edit(-1); RefreshList(); break;
    }
    case MenuRestore: case RestoreAll: RestoreEverything(); break;
    case MenuStop: case StopWatch: StopMonitoring(true); Notify(L"看剧模式已关闭。"); break;
    case MenuStartup:
        SendMessageW(C(Startup), BM_SETCHECK, config.autoStart ? BST_UNCHECKED : BST_CHECKED, 0);
        Save(true); break;
    case Elevate: {
        if (!RestoreEverything()) break;
        wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768);
        SHELLEXECUTEINFOW info{sizeof(info)}; info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.hwnd = window; info.lpVerb = L"runas"; info.lpFile = exe; info.lpParameters = L"--takeover"; info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) { if (info.hProcess) CloseHandle(info.hProcess); DestroyWindow(window); }
        else Notify(L"管理员重启未完成。"); break;
    }
    case MenuExit: if (RestoreEverything()) DestroyWindow(window); else Error(L"还有窗口未恢复，工具继续驻留以便重试。"); break;
    }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    if (message == app->taskbarCreated && app->taskbarCreated) { app->AddTray(); return 0; }
    switch (message) {
    case WM_CREATE: app->window = window; app->dpi = GetDpiForWindow(window); app->CreateUI(); return 0;
    case WM_SIZE: if (wParam == SIZE_MINIMIZED) ShowWindow(window, SW_HIDE); else if (app->C(List)) app->Layout(); return 0;
    case WM_DPICHANGED: { app->dpi = HIWORD(wParam); auto rect = reinterpret_cast<RECT*>(lParam); SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); app->Layout(); return 0; }
    case WM_GETMINMAXINFO: { auto info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = {Scale(840), Scale(680)}; return 0; }
    case WM_COMMAND: app->Command(LOWORD(wParam)); return 0;
    case WM_NOTIFY: {
        auto notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification->idFrom == List && notification->code == LVN_ITEMCHANGED && !app->updatingList) {
            auto change = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED)) app->Edit(change->iItem);
        }
        if (notification->idFrom == List && notification->code == NM_CLICK) {
            int index = ListView_GetNextItem(app->C(List), -1, LVNI_SELECTED); if (index >= 0) app->Edit(index);
        }
        if (notification->idFrom == List && notification->code == NM_DBLCLK) { int index = ListView_GetNextItem(app->C(List), -1, LVNI_SELECTED); if (index >= 0) app->Toggle(static_cast<size_t>(index)); }
        return 0;
    }
    case SidedHotkeyMessage:
    case WM_HOTKEY: {
        Key key = message == SidedHotkeyMessage ? app->hotkeys->ResolveSided(static_cast<int>(wParam), UnpackKey(lParam)) : app->hotkeys->Resolve(static_cast<int>(wParam));
        if (!key.vk) return 0;
        if (key == app->config.restore) app->RestoreEverything();
        else for (size_t i = 0; i < app->config.rules.size(); ++i) {
            if (key == app->config.rules[i].toggle) { app->Toggle(i); break; }
            if (key == app->config.rules[i].watch) { app->SetWatch(static_cast<int>(i)); break; }
        } return 0;
    }
    case WM_TIMER:
        if (wParam == ResumeTimer) {
            if (app->watchRule < 0) { KillTimer(window, ResumeTimer); return 0; }
            HWND target = app->BestWindow(static_cast<size_t>(app->watchRule));
            if (target && IsWindowVisible(target) && !IsIconic(target)) {
                KillTimer(window, ResumeTimer); app->ResumeMonitoring();
            } else if (GetTickCount64() - app->resumeStarted >= 2000) app->StopMonitoring(true);
            return 0;
        }
        if (wParam == PendingTimer && app->pendingRule >= 0) {
            auto now = GetTickCount64();
            if (!app->pendingSent) {
                if (!KeysReleased() && now - app->pendingStarted < 500) return 0;
                auto& rule = app->config.rules[app->pendingRule];
                app->keyConfirmed = SendBeforeKey(app->pendingWindow, rule.before);
                app->pendingSent = true; app->hideAt = now + rule.delay;
            }
            if (now >= app->hideAt) app->FinishHide();
        } return 0;
    case MouseExitMessage: if (app->watchRule >= 0 && app->exitQueued) app->BeginHide(static_cast<size_t>(app->watchRule)); return 0;
    case WM_APP + 100:
        // An isolated integration-test instance can feed the same boundary handler
        // without relying on physical input access in a headless runner.
        if (app->testInstance) app->MouseMove({static_cast<LONG>(wParam), static_cast<LONG>(lParam)});
        return 0;
    case WM_APP + 101: return app->testInstance ? reinterpret_cast<LRESULT>(app->watchWindow) : 0;
    case WindowEventMessage: app->OnWindowEvent(reinterpret_cast<HWND>(wParam), static_cast<DWORD>(lParam)); return 0;
    case OpenMessage: app->Command(MenuOpen); return 0;
    case TrayMessage: if (lParam == WM_LBUTTONDBLCLK) app->Command(MenuOpen); else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) app->TrayMenu(); return 0;
    case WM_CLOSE: ShowWindow(window, SW_HIDE); app->Notify(L"已驻留托盘，双击托盘图标打开设置。"); return 0;
    case WM_QUERYENDSESSION: return app->RestoreEverything() ? TRUE : FALSE;
    case WM_ENDSESSION: if (wParam) app->RestoreEverything(); return 0;
    case WM_DESTROY: {
        app->StopMonitoring(true);
        if (app->events) UnhookWinEvent(app->events); if (app->foregroundEvents) UnhookWinEvent(app->foregroundEvents);
        NOTIFYICONDATAW tray{sizeof(tray)}; tray.hWnd = window; tray.uID = 1; Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_APP + 10) {
        HWND dialog = CreateWindowW(L"SlackOff.Fixture", L"Fixture new owned window", WS_OVERLAPPEDWINDOW,
            120, 140, 320, 220, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        ShowWindow(dialog, SW_SHOWNORMAL); return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_SPACE) { SetWindowTextW(window, L"Paused"); return 0; }
    if (message == WM_CLOSE) { DestroyWindow(window); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
int Fixture(HINSTANCE instance) {
    WNDCLASSW cls{}; cls.lpfnWndProc = FixtureProc; cls.hInstance = instance; cls.lpszClassName = L"SlackOff.Fixture"; cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
    HWND first = CreateWindowW(cls.lpszClassName, L"Fixture A", WS_OVERLAPPEDWINDOW, 80, 80, 400, 300, nullptr, nullptr, instance, nullptr);
    HWND second = CreateWindowW(cls.lpszClassName, L"Fixture B", WS_OVERLAPPEDWINDOW, 520, 80, 400, 300, nullptr, nullptr, instance, nullptr);
    CreateWindowW(cls.lpszClassName, L"Fixture originally hidden", WS_OVERLAPPEDWINDOW, 80, 420, 400, 200, nullptr, nullptr, instance, nullptr);
    ShowWindow(first, SW_SHOWNORMAL); ShowWindow(first, SW_SHOWNORMAL); ShowWindow(second, SW_SHOWNORMAL);
    MSG message; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
int SelfTest(HINSTANCE instance) {
    wchar_t temp[MAX_PATH], exe[32768]; GetTempPathW(MAX_PATH, temp); GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring directory = std::wstring(temp) + L"SlackOff-test-" + std::to_wstring(GetCurrentProcessId());
    Settings settings(directory); Config config;
    Rule rule; rule.name = L"测试程序"; rule.path = exe; rule.toggle = {MOD_CONTROL | MOD_ALT, VK_F9}; rule.watch = {MOD_CONTROL | MOD_ALT, VK_F10}; rule.before = {0, VK_SPACE}; config.rules.push_back(rule); config.autoStart = false;
    std::ofstream report(std::filesystem::path(directory + L"\\results.txt"));
    int failures = 0;
    auto check = [&](bool ok, const char* name) { report << (ok ? "PASS " : "FAIL ") << name << "\n"; if (!ok) ++failures; };
    check(settings.Save(config), "atomic config save"); Config loaded; check(settings.Load(loaded) && loaded.rules.size() == 1 && loaded.rules[0].name == rule.name && loaded.rules[0].before == rule.before, "Unicode config roundtrip");
    Key left{MOD_CONTROL | MOD_ALT, VK_F9, LeftCtrl | LeftAlt};
    Key right{MOD_CONTROL | MOD_ALT, VK_F9, RightCtrl | RightAlt};
    check(KeyText(left).find(L"左Ctrl+左Alt+") == 0 && KeyText(right).find(L"右Ctrl+右Alt+") == 0, "side-specific labels");
    bool combinationsMatch = true;
    for (UINT ctrl : {LeftCtrl, RightCtrl, CtrlSides}) for (UINT altSide : {LeftAlt, RightAlt, AltSides}) {
        Key pressed{left.modifiers, left.vk, ctrl | altSide};
        combinationsMatch = combinationsMatch && (KeyMatches(left, pressed) == (ctrl == LeftCtrl && altSide == LeftAlt));
        combinationsMatch = combinationsMatch && (KeyMatches(right, pressed) == (ctrl == RightCtrl && altSide == RightAlt));
        combinationsMatch = combinationsMatch && KeyMatches(rule.toggle, pressed);
    }
    check(combinationsMatch, "left/right/both modifier matrix; legacy accepts either side");
    check(!KeyOverlaps(left, right) && KeyOverlaps(left, rule.toggle) &&
        KeyOverlaps({left.modifiers, left.vk, LeftCtrl}, {left.modifiers, left.vk, RightAlt}), "overlapping wildcard sides rejected; disjoint sides allowed");
    check(!KeyMatches(left, {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F9, left.sides}), "additional Shift does not trigger shortcut");
    Config sided = config; sided.rules[0].toggle = left; sided.rules[0].watch = right;
    check(settings.Save(sided), "side-specific config save"); Config sidedLoaded;
    check(settings.Load(sidedLoaded) && sidedLoaded.rules.size() == 1 && sidedLoaded.rules[0].toggle == left && sidedLoaded.rules[0].watch == right, "side-specific settings roundtrip");
    auto settingsFile = directory + L"\\settings.ini";
    WritePrivateProfileStringW(L"Rule0", L"Toggle", L"3,120", settingsFile.c_str());
    WritePrivateProfileStringW(L"Rule0", L"Watch", L"3,121", settingsFile.c_str());
    Config legacy; check(settings.Load(legacy) && legacy.rules[0].toggle == rule.toggle && legacy.rules[0].watch == rule.watch, "two-field legacy settings remain compatible");
    WritePrivateProfileStringW(L"Rule0", L"Toggle", L"0,120,1", settingsFile.c_str());
    Config invalid; check(settings.Load(invalid) && !invalid.rules[0].toggle.vk, "invalid modifier side setting rejected");
    check(settings.Save(config), "restore generic test config");
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = DefWindowProcW; cls.lpszClassName = L"SlackOff.TestHost"; RegisterClassW(&cls);
    HWND host = CreateWindowW(cls.lpszClassName, L"TestHost", 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    {
        Hotkeys hotkeys(host); std::wstring error;
        check(hotkeys.Apply(config, error), "hotkey registration");
        Config duplicate = config; duplicate.rules[0].watch = duplicate.rules[0].toggle;
        check(!hotkeys.Apply(duplicate, error), "duplicate rejected");
        check(!RegisterHotKey(host, 900, rule.toggle.modifiers, rule.toggle.vk), "failed transaction keeps old hotkey");
        Config occupied = config; occupied.rules[0].toggle = {MOD_CONTROL | MOD_SHIFT, VK_F8};
        bool reserved = RegisterHotKey(host, 901, occupied.rules[0].toggle.modifiers, occupied.rules[0].toggle.vk) != FALSE;
        check(reserved && !hotkeys.Apply(occupied, error), "external conflict rejected"); UnregisterHotKey(host, 901);
        check(hotkeys.Apply(config, error), "unchanged registration reusable");
        check(hotkeys.Apply(sided, error), "left/right actions share a single native reservation");
        check(!hotkeys.Resolve(101).vk && hotkeys.ResolveSided(101, left) == left && hotkeys.ResolveSided(101, right) == right, "sided dispatch requires captured modifier state");
        Config overlap = sided; overlap.rules[0].watch = rule.toggle;
        check(!hotkeys.Apply(overlap, error) && hotkeys.ResolveSided(101, left) == left, "wildcard conflict preserves previous side bindings");
        check(!hotkeys.ResolveSided(101, {left.modifiers, left.vk, LeftCtrl | RightAlt}).vk, "wrong modifier sides do not dispatch");
        check(hotkeys.Apply(config, error) && hotkeys.Resolve(101) == rule.toggle, "generic/sided transitions reuse reservation");
    }
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --fixture";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    bool created = CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE;
    check(created, "fixture process launch");
    if (created) {
        WaitForInputIdle(process.hProcess, 5000);
        std::vector<HWND> windows;
        for (int i = 0; i < 40; ++i) { windows = TargetWindows(exe); if (windows.size() == 2) break; Sleep(50); }
        check(windows.size() == 2, "multiple visible windows; originally hidden excluded");
        std::vector<Snapshot> snapshots;
        for (auto target : windows) { Snapshot snap; if (CaptureWindow(target, snap)) snapshots.push_back(snap); }
        check(snapshots.size() == windows.size() && !snapshots.empty(), "capture identity and placement");
        if (!snapshots.empty()) {
            HWND target = snapshots[0].window;
            RECT rect; GetWindowRect(target, &rect);
            check(CursorInside(target, {rect.left + 80, rect.top + 80}) && !CursorInside(target, {rect.right + 30, rect.top}), "window boundary detection");
            SetForegroundWindow(target); Sleep(50);
            // The automated runner may start without foreground permission. Give the
            // fixture a test-only Alt input before testing the production focus check.
            INPUT alt[2]{}; alt[0].type = alt[1].type = INPUT_KEYBOARD;
            alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU; alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, alt, sizeof(INPUT)); SetForegroundWindow(target); Sleep(80);
            bool injected = SendBeforeKey(target, rule.before); Sleep(80);
            if (GetForegroundWindow() != target) {
                report << "SKIP pre-hide input delivery: runner cannot give target foreground focus\n";
                check(!injected, "input refuses delivery without foreground confirmation");
            } else {
                check(injected, "pre-hide key input accepted");
                check(WindowText(target) == L"Paused", "pre-hide key reaches fixture");
            }
            config.rules[0].snapshots = snapshots;
            check(settings.SaveJournal(config.rules), "journal committed before hide");
            for (const auto& snap : snapshots) ShowWindowAsync(snap.window, SW_HIDE);
            Sleep(100);
            check(TargetWindows(exe).empty(), "all fixture windows hidden");
            auto journal = settings.ReadJournal();
            check(journal.size() == snapshots.size() && ValidSnapshot(journal[0]), "journal identity roundtrip");
            Snapshot wrong = journal[0]; ++wrong.identity.started;
            check(!ValidSnapshot(wrong), "reused process identity rejected");
            wrong = journal[0]; ++wrong.token; check(!ValidSnapshot(wrong), "reused window token rejected");
            check(settings.Recover(), "crash recovery restore"); Sleep(100);
            check(TargetWindows(exe).size() == 2, "recovery leaves originally hidden window hidden");
            WINDOWPLACEMENT after{sizeof(after)}; GetWindowPlacement(target, &after);
            check(EqualRect(&after.rcNormalPosition, &snapshots[0].placement.rcNormalPosition) != FALSE, "original placement restored");
        }
        for (auto target : windows) PostMessageW(target, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 3000) == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    DestroyWindow(host);
    report << "Failures: " << failures << "\n";
    report.close();
    // Test artifacts intentionally stay in TEMP for inspection, never in user settings.
    return failures ? 1 : 0;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    std::wstring args = command;
    if (args == L"--fixture") return Fixture(instance);
    if (args == L"--self-test") return SelfTest(instance);
    bool test = args.find(L"--test-instance") != std::wstring::npos;
    const wchar_t* mutexName = test ? L"Local\\SlackOff.Test.Instance.v1" : L"Local\\SlackOff.Instance.v1";
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName);
    bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
    if (!mutex) return 1;
    if (existing && args == L"--takeover") {
        DWORD waited = WaitForSingleObject(mutex, 10000);
        if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) existing = false;
    }
    if (existing) {
        HWND other = FindWindowW(WindowClass, nullptr); if (other) PostMessageW(other, OpenMessage, 0, 0);
        CloseHandle(mutex); return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_HOTKEY_CLASS}; InitCommonControlsEx(&controls);
    App state;
    app = &state; state.testInstance = test;
    if (test) { wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp); state.settings = Settings(std::wstring(temp) + L"SlackOff-UI-test"); }
    bool first = !state.settings.Load(state.config);
    bool recovered = state.settings.Recover();
    if (!first && !test) state.config.autoStart = state.settings.IsAutoStart();
    bool startupOk = true;
    if (first) {
        if (test) state.config.autoStart = false;
        else startupOk = state.settings.SetAutoStart(true);
        if (!startupOk) state.config.autoStart = false;
        state.settings.Save(state.config);
    }
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance = instance; cls.lpfnWndProc = WindowProc; cls.lpszClassName = WindowClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_SLACKOFF), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_SLACKOFF), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassExW(&cls);
    HWND window = CreateWindowExW(0, WindowClass, L"SlackOff · 轻量快捷隐藏", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(940, static_cast<int>(GetDpiForSystem()), 96), MulDiv(700, static_cast<int>(GetDpiForSystem()), 96), nullptr, nullptr, instance, nullptr);
    if (!window) { app = nullptr; ReleaseMutex(mutex); CloseHandle(mutex); return 1; }
    Hotkeys hotkeys(window); state.hotkeys = &hotkeys;
    std::wstring error;
    if (!hotkeys.Apply(state.config, error)) { state.Notify(error); ShowWindow(window, SW_SHOW); state.Error(error + L"\n请修改配置后重新保存；托盘菜单仍可恢复窗口。"); }
    state.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    CHANGEFILTERSTRUCT filter{sizeof(filter)};
    ChangeWindowMessageFilterEx(window, OpenMessage, MSGFLT_ALLOW, &filter);
    ChangeWindowMessageFilterEx(window, state.taskbarCreated, MSGFLT_ALLOW, &filter);
    state.AddTray();
    state.events = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, EventCallback, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    state.foregroundEvents = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, nullptr, EventCallback, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!state.events || !state.foregroundEvents) state.Notify(L"窗口事件监测失败，新窗口自动隐藏可能不可用。");
    if (!recovered) state.Notify(L"部分历史窗口恢复失败，恢复记录已保留。请以原权限重启。");
    if (!startupOk) state.Notify(L"开机自启设置失败，可在设置中重试。");
    if (args != L"--tray" || first || !error.empty()) { ShowWindow(window, SW_SHOW); ShowWindow(window, SW_SHOW); }
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (state.font) DeleteObject(state.font);
    app = nullptr; ReleaseMutex(mutex); CloseHandle(mutex);
    return 0;
}
