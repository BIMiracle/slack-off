#include "app.h"
#include <shlobj.h>
#include <sstream>
#include <algorithm>

namespace slack {
namespace {
std::wstring Read(const std::wstring& file, const std::wstring& section, const wchar_t* key, const wchar_t* fallback = L"") {
    wchar_t value[32768];
    GetPrivateProfileStringW(section.c_str(), key, fallback, value, 32768, file.c_str());
    return value;
}
uint64_t Number(const std::wstring& file, const std::wstring& section, const wchar_t* key, uint64_t fallback = 0) {
    auto value = Read(file, section, key);
    if (value.empty()) return fallback;
    wchar_t* end = nullptr;
    auto result = _wcstoui64(value.c_str(), &end, 10);
    return end && !*end ? result : fallback;
}
std::wstring SerializeKey(Key key) { return std::to_wstring(key.modifiers) + L"," + std::to_wstring(key.vk) + L"," + std::to_wstring(key.sides); }
Key ParseKey(const std::wstring& text, Key fallback = {}) {
    unsigned int modifiers = 0, vk = 0, sides = 0;
    int fields = swscanf_s(text.c_str(), L"%u,%u,%u", &modifiers, &vk, &sides);
    Key key{modifiers, vk, sides};
    return fields >= 2 && ValidKey(key) ? key : fallback;
}
bool AtomicWrite(const std::wstring& file, const std::wstring& text) {
    auto temp = file + L".tmp";
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    bool ok = WriteFile(handle, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
    DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    ok = ok && WriteFile(handle, text.data(), bytes, &written, nullptr) && written == bytes;
    ok = ok && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (ok) ok = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temp.c_str());
    // Invalidate the legacy profile API cache after an atomic replacement.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
    return ok;
}
std::wstring SafeValue(std::wstring value) {
    std::replace(value.begin(), value.end(), L'\r', L' ');
    std::replace(value.begin(), value.end(), L'\n', L' ');
    return value;
}
std::wstring Executable() {
    wchar_t buffer[32768];
    DWORD n = GetModuleFileNameW(nullptr, buffer, 32768);
    return std::wstring(buffer, n);
}
}
Settings::Settings(std::wstring directory) : directory_(std::move(directory)) {
    if (directory_.empty()) {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) { directory_ = std::wstring(local) + L"\\SlackOff"; CoTaskMemFree(local); }
    }
    if (!directory_.empty()) CreateDirectoryW(directory_.c_str(), nullptr);
}
bool Settings::Load(Config& config) {
    auto file = directory_ + L"\\settings.ini";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    config.autoStart = Number(file, L"General", L"AutoStart", 1) != 0;
    config.restore = ParseKey(Read(file, L"General", L"Restore"), config.restore);
    auto count = std::min<uint64_t>(Number(file, L"General", L"Count"), 128);
    for (uint64_t i = 0; i < count; ++i) {
        auto section = L"Rule" + std::to_wstring(i);
        Rule rule;
        rule.path = Read(file, section, L"Path");
        if (rule.path.empty()) continue;
        rule.name = Read(file, section, L"Name");
        rule.toggle = ParseKey(Read(file, section, L"Toggle"));
        rule.watch = ParseKey(Read(file, section, L"Watch"));
        rule.before = ParseKey(Read(file, section, L"Before"));
        rule.delay = static_cast<UINT>(std::min<uint64_t>(Number(file, section, L"Delay", 50), 500));
        bool duplicate = false;
        for (const auto& existing : config.rules) if (SamePath(existing.path, rule.path)) duplicate = true;
        if (!duplicate) config.rules.push_back(std::move(rule));
    }
    return true;
}
bool Settings::Save(const Config& config) {
    std::wostringstream text;
    text << L"[General]\r\nVersion=2\r\nAutoStart=" << config.autoStart << L"\r\nRestore=" << SerializeKey(config.restore) << L"\r\nCount=" << config.rules.size() << L"\r\n";
    for (size_t i = 0; i < config.rules.size(); ++i) {
        const auto& rule = config.rules[i];
        text << L"\r\n[Rule" << i << L"]\r\nName=" << SafeValue(rule.name) << L"\r\nPath=" << SafeValue(rule.path)
            << L"\r\nToggle=" << SerializeKey(rule.toggle) << L"\r\nWatch=" << SerializeKey(rule.watch)
            << L"\r\nBefore=" << SerializeKey(rule.before) << L"\r\nDelay=" << rule.delay << L"\r\n";
    }
    return AtomicWrite(directory_ + L"\\settings.ini", text.str());
}
bool Settings::SaveJournal(const std::vector<Rule>& rules) {
    std::vector<Snapshot> all;
    for (const auto& rule : rules) for (const auto& snap : rule.snapshots) all.push_back(snap);
    // Keep unresolved records from a previous crash, including windows for a deleted rule.
    for (const auto& old : ReadJournal()) if (ValidSnapshot(old)) {
        bool found = false;
        for (const auto& current : all) if (current.window == old.window && current.token == old.token) found = true;
        if (!found) all.push_back(old);
    }
    std::wostringstream text;
    text << L"[General]\r\nCount=" << all.size() << L"\r\n";
    size_t index = 0;
    for (const auto& snap : all) {
        const auto& p = snap.placement;
        text << L"\r\n[Window" << index++ << L"]\r\nHandle=" << reinterpret_cast<ULONG_PTR>(snap.window)
            << L"\r\nPid=" << snap.identity.pid << L"\r\nStarted=" << snap.identity.started
            << L"\r\nPath=" << SafeValue(snap.identity.path) << L"\r\nClass=" << SafeValue(snap.windowClass)
            << L"\r\nToken=" << snap.token << L"\r\nPlacement=" << p.flags << L"," << p.showCmd
            << L"," << p.ptMinPosition.x << L"," << p.ptMinPosition.y << L"," << p.ptMaxPosition.x << L"," << p.ptMaxPosition.y
            << L"," << p.rcNormalPosition.left << L"," << p.rcNormalPosition.top << L"," << p.rcNormalPosition.right << L"," << p.rcNormalPosition.bottom << L"\r\n";
    }
    return AtomicWrite(directory_ + L"\\recovery.ini", text.str());
}
std::vector<Snapshot> Settings::ReadJournal() {
    auto file = directory_ + L"\\recovery.ini";
    std::vector<Snapshot> snapshots;
    auto count = std::min<uint64_t>(Number(file, L"General", L"Count"), 4096);
    for (uint64_t i = 0; i < count; ++i) {
        auto section = L"Window" + std::to_wstring(i);
        Snapshot snap;
        snap.window = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(Number(file, section, L"Handle")));
        snap.identity.pid = static_cast<DWORD>(Number(file, section, L"Pid"));
        snap.identity.started = Number(file, section, L"Started");
        snap.identity.path = Read(file, section, L"Path");
        snap.windowClass = Read(file, section, L"Class");
        snap.token = static_cast<ULONG_PTR>(Number(file, section, L"Token"));
        auto value = Read(file, section, L"Placement");
        auto& p = snap.placement;
        if (swscanf_s(value.c_str(), L"%u,%u,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld", &p.flags, &p.showCmd,
            &p.ptMinPosition.x, &p.ptMinPosition.y, &p.ptMaxPosition.x, &p.ptMaxPosition.y,
            &p.rcNormalPosition.left, &p.rcNormalPosition.top, &p.rcNormalPosition.right, &p.rcNormalPosition.bottom) != 10) continue;
        if (snap.token && p.showCmd >= SW_SHOWNORMAL && p.showCmd <= SW_FORCEMINIMIZE) snapshots.push_back(std::move(snap));
    }
    return snapshots;
}
bool Settings::Recover() {
    Rule failed;
    for (const auto& snap : ReadJournal()) if (!RestoreWindow(snap)) failed.snapshots.push_back(snap);
    bool ok = failed.snapshots.empty();
    return SaveJournal({failed}) && ok;
}
bool Settings::SetAutoStart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS status;
    if (enable) {
        auto value = L"\"" + Executable() + L"\" --tray";
        status = RegSetValueExW(key, L"SlackOff", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else { status = RegDeleteValueW(key, L"SlackOff"); if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS; }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
bool Settings::IsAutoStart() const {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"SlackOff", RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}
}
