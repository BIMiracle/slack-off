param([string]$Exe = (Join-Path $PSScriptRoot '..\release\SlackOff.exe'))
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class ShortcutTest {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code,uint type);
 public static IntPtr Main() { return FindWindow("SlackOff.Main.v1",null); }
 public static string Text(IntPtr h) { var b=new StringBuilder(512);ReadText(h,0xD,(IntPtr)b.Capacity,b);return b.ToString(); }
 public static int Visible(uint pid) { int count=0;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid && IsWindowVisible(h))count++;return true;},IntPtr.Zero);return count; }
 public static IntPtr Watched(IntPtr h) { return SendMessage(h,0x8065,IntPtr.Zero,IntPtr.Zero); }
 public static void Key(byte key,bool up) {
  uint flags=up?2u:0u;if(key==0xA3 || key==0xA5)flags|=1;
  keybd_event(key,(byte)MapVirtualKey(key,0),flags,UIntPtr.Zero);
 }
 public static void Chord(byte ctrl,byte alt,byte key) {
  Key(ctrl,false);Key(alt,false);Key(key,false);Key(key,true);Key(alt,true);Key(ctrl,true);
 }
 public static void Exit(IntPtr h) { SendMessage(h,0x111,(IntPtr)404,IntPtr.Zero); }
}
'@
if ([ShortcutTest]::Main() -ne [IntPtr]::Zero) { throw 'Close SlackOff before shortcut tests.' }
$taskDir = Join-Path $env:TEMP 'SlackOff-UI-test'
New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
$taskFixtureExe = Join-Path $taskDir 'ShortcutPlayer.exe'
Copy-Item -LiteralPath $Exe -Destination $taskFixtureExe -Force
$taskResults = [Collections.Generic.List[string]]::new()
$taskFixture = $null
$taskApp = $null
$taskHwnd = [IntPtr]::Zero
function Check([bool]$ok,[string]$label) {
    $taskResults.Add("$(if ($ok) { 'PASS' } else { 'FAIL' }) $label")
    if (!$ok) { Write-Warning $label }
}
function Wait-Visible([int]$expected) {
    for ($i=0; $i -lt 40; $i++) {
        if ([ShortcutTest]::Visible($taskFixture.Id) -eq $expected) { return $true }
        Start-Sleep -Milliseconds 50
    }
    return $false
}
function Stop-App {
    if ($taskApp -and !$taskApp.HasExited) {
        [ShortcutTest]::Exit($taskHwnd)
        if (!$taskApp.WaitForExit(3000)) { throw 'Test application did not exit normally.' }
    }
}
function Start-App([int]$watchKey, [int]$watchModifiers = 3, [int]$watchSides = 10, [int]$toggleKey = 120, [int]$toggleSides = 5) {
    $taskConfig = @"
[General]
Version=2
AutoStart=0
Restore=7,123,0
Count=1
[Rule0]
Name=ShortcutPlayer
Path=$taskFixtureExe
Toggle=3,$toggleKey,$toggleSides
Watch=$watchModifiers,$watchKey,$watchSides
Before=0,0,0
Delay=0
"@
    if ($watchKey -ge 0) { [IO.File]::WriteAllText((Join-Path $taskDir 'settings.ini'),$taskConfig,[Text.Encoding]::Unicode) }
    $script:taskApp = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
    for ($i=0; $i -lt 40; $i++) {
        $script:taskHwnd = [ShortcutTest]::Main()
        if ($taskHwnd -ne [IntPtr]::Zero -and [ShortcutTest]::GetDlgItem($taskHwnd,30) -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    }
    Start-Sleep -Milliseconds 300
    if ($taskHwnd -eq [IntPtr]::Zero) { throw 'Test application did not start.' }
    $taskList = [ShortcutTest]::GetDlgItem($taskHwnd,10)
    [void][ShortcutTest]::SendMessage($taskList,0x100,[IntPtr]0x24,[IntPtr]::Zero)
    $taskExpectedToggle = if ($toggleSides -eq 10) { '右Ctrl+右Alt+*' } else { '左Ctrl+左Alt+*' }
    Check ([ShortcutTest]::Text([ShortcutTest]::GetDlgItem($taskHwnd,17)) -like $taskExpectedToggle) 'toggle field displays saved modifier sides'
    $taskExpectedWatch = if ($watchKey -lt 0) { '左Ctrl+右Alt+*' } elseif ($watchModifiers -eq 1) { '右Alt+*' } else { '右Ctrl+右Alt+*' }
    Check ([ShortcutTest]::Text([ShortcutTest]::GetDlgItem($taskHwnd,18)) -like $taskExpectedWatch) 'watch field displays saved modifier sides'
    # Keep recording fields out of focus while exercising real global input.
    [void][ShortcutTest]::SendMessage($taskHwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
}
try {
    $taskFixture = Start-Process -FilePath $taskFixtureExe -ArgumentList '--fixture' -WindowStyle Hidden -PassThru
    Check (Wait-Visible 2) 'fixture has two visible windows'
    Start-App 121
    foreach ($taskSides in @(@(0xA3,0xA4),@(0xA2,0xA5),@(0xA3,0xA5))) {
        [ShortcutTest]::Chord($taskSides[0],$taskSides[1],0x78)
        Start-Sleep -Milliseconds 150
        Check ([ShortcutTest]::Visible($taskFixture.Id) -eq 2) "wrong toggle sides $($taskSides -join ',') do not hide"
    }
    [ShortcutTest]::Chord(0xA2,0xA4,0x78)
    Check (Wait-Visible 0) 'left Ctrl + left Alt hides through actual keyboard hook'
    [ShortcutTest]::Chord(0xA3,0xA5,0x78)
    Start-Sleep -Milliseconds 150
    Check ([ShortcutTest]::Visible($taskFixture.Id) -eq 0) 'right modifiers do not restore left shortcut'
    [ShortcutTest]::Chord(0xA2,0xA4,0x78)
    Check (Wait-Visible 2) 'left shortcut restores through actual keyboard hook'
    foreach ($taskSides in @(@(0xA2,0xA4),@(0xA3,0xA4),@(0xA2,0xA5))) {
        [ShortcutTest]::Chord($taskSides[0],$taskSides[1],0x79)
        Start-Sleep -Milliseconds 150
        Check ([ShortcutTest]::Watched($taskHwnd) -eq [IntPtr]::Zero) "wrong watch sides $($taskSides -join ',') do not enable watching"
    }
    [ShortcutTest]::Key(0xA3,$false); [ShortcutTest]::Key(0xA5,$false)
    [ShortcutTest]::Key(0x79,$false); [ShortcutTest]::Key(0x79,$false)
    Start-Sleep -Milliseconds 200
    Check ([ShortcutTest]::Watched($taskHwnd) -ne [IntPtr]::Zero) 'held/repeated right watch shortcut enables watching only once'
    [ShortcutTest]::Key(0x79,$true); [ShortcutTest]::Key(0xA5,$true); [ShortcutTest]::Key(0xA3,$true)
    [ShortcutTest]::Chord(0xA3,0xA5,0x79)
    Start-Sleep -Milliseconds 200
    Check ([ShortcutTest]::Watched($taskHwnd) -eq [IntPtr]::Zero) 'right watch shortcut disables watching on the next press'
    # Restart with identical base combinations and disjoint side constraints.
    Stop-App
    Start-App 120
    [ShortcutTest]::Chord(0xA3,0xA5,0x78)
    Start-Sleep -Milliseconds 200
    Check ([ShortcutTest]::Watched($taskHwnd) -ne [IntPtr]::Zero -and [ShortcutTest]::Visible($taskFixture.Id) -eq 2) 'right F9 toggles watching without hiding when F9 is shared'
    [ShortcutTest]::Chord(0xA3,0xA5,0x78)
    Start-Sleep -Milliseconds 150
    [ShortcutTest]::Chord(0xA2,0xA4,0x78)
    Check (Wait-Visible 0) 'left F9 hides when F9 is shared'
    # The unchanged generic restore action still accepts either modifier side.
    [ShortcutTest]::Key(0xA1,$false)
    [ShortcutTest]::Chord(0xA3,0xA5,0x7B)
    [ShortcutTest]::Key(0xA1,$true)
    Check (Wait-Visible 2) 'generic restore-all accepts right Ctrl/Alt'
    # Record the already reserved toggle in its real UI field, then change watch.
    [void][ShortcutTest]::SendMessage($taskHwnd,0x8004,[IntPtr]::Zero,[IntPtr]::Zero)
    [ShortcutTest]::Key(0xA4,$false); [ShortcutTest]::Key(0xA4,$true)
    [void][ShortcutTest]::SetForegroundWindow($taskHwnd)
    Start-Sleep -Milliseconds 100
    if ([ShortcutTest]::GetForegroundWindow() -eq $taskHwnd) {
        $taskToggle = [ShortcutTest]::GetDlgItem($taskHwnd,17)
        [void][ShortcutTest]::SendMessage($taskToggle,0x201,[IntPtr]1,[IntPtr]0x000A000A)
        [void][ShortcutTest]::SendMessage($taskToggle,0x202,[IntPtr]::Zero,[IntPtr]0x000A000A)
        [ShortcutTest]::Chord(0xA2,0xA4,0x78)
        Start-Sleep -Milliseconds 150
        Check ([ShortcutTest]::Text($taskToggle) -eq '左Ctrl+左Alt+F9' -and [ShortcutTest]::Visible($taskFixture.Id) -eq 2) 'recording an existing shortcut captures sides without executing it'
        $taskWatch = [ShortcutTest]::GetDlgItem($taskHwnd,18)
        [void][ShortcutTest]::SendMessage($taskWatch,0x201,[IntPtr]1,[IntPtr]0x000A000A)
        [void][ShortcutTest]::SendMessage($taskWatch,0x202,[IntPtr]::Zero,[IntPtr]0x000A000A)
        [ShortcutTest]::Key(0x08,$false); [ShortcutTest]::Key(0x08,$true)
        Start-Sleep -Milliseconds 100
        Check ([ShortcutTest]::Text($taskWatch) -eq '未设置') 'Backspace clears the watch shortcut'
        [ShortcutTest]::Chord(0xA2,0xA5,0x79)
        Start-Sleep -Milliseconds 150
        Check ([ShortcutTest]::Text($taskWatch) -eq '左Ctrl+右Alt+F10') 'watch field records mixed left/right modifiers'
        [void][ShortcutTest]::SendMessage($taskHwnd,0x111,[IntPtr]21,[IntPtr]::Zero)
        $taskSaved = [IO.File]::ReadAllText((Join-Path $taskDir 'settings.ini'))
        Check ($taskSaved.Contains('Toggle=3,120,5') -and $taskSaved.Contains('Watch=3,121,9')) 'saving the actual UI fields persists modifier sides'
        Stop-App
        Start-App -1
        [ShortcutTest]::Chord(0xA3,0xA5,0x78)
        Start-Sleep -Milliseconds 150
        Check ([ShortcutTest]::Watched($taskHwnd) -eq [IntPtr]::Zero) 'previous watch combination no longer triggers after restart'
        [ShortcutTest]::Chord(0xA2,0xA5,0x79)
        Start-Sleep -Milliseconds 200
        Check ([ShortcutTest]::Watched($taskHwnd) -ne [IntPtr]::Zero) 'recorded mixed-side watch shortcut triggers after restart'
    } else {
        $taskResults.Add('SKIP shortcut recording: runner cannot give settings foreground focus')
    }
    # Reproduce the scrcpy rule: right Alt+M watches, right Ctrl+right Alt+M hides.
    Stop-App
    Start-App 77 1 8 77 10
    [void][ShortcutTest]::SetForegroundWindow([ShortcutTest]::FindWindow('SlackOff.Fixture','Fixture A'))
    [ShortcutTest]::Key(0xA4,$false); [ShortcutTest]::Key(0x4D,$false); [ShortcutTest]::Key(0x4D,$true); [ShortcutTest]::Key(0xA4,$true)
    Start-Sleep -Milliseconds 150
    Check ([ShortcutTest]::Watched($taskHwnd) -eq [IntPtr]::Zero) 'left Alt+M does not enable right-Alt watching'
    [ShortcutTest]::Key(0xA5,$false); [ShortcutTest]::Key(0x4D,$false); [ShortcutTest]::Key(0x4D,$true); [ShortcutTest]::Key(0xA5,$true)
    Start-Sleep -Milliseconds 200
    Check ([ShortcutTest]::Watched($taskHwnd) -ne [IntPtr]::Zero -and [ShortcutTest]::Visible($taskFixture.Id) -eq 2) 'right Alt+M enables watching with the target focused'
    [ShortcutTest]::Key(0xA5,$false); [ShortcutTest]::Key(0x4D,$false); [ShortcutTest]::Key(0x4D,$true); [ShortcutTest]::Key(0xA5,$true)
    Start-Sleep -Milliseconds 150
    Check ([ShortcutTest]::Watched($taskHwnd) -eq [IntPtr]::Zero) 'right Alt+M disables watching on the next press'
    [ShortcutTest]::Chord(0xA3,0xA5,0x4D)
    Check (Wait-Visible 0) 'right Ctrl+right Alt+M hides without enabling watching'
    [ShortcutTest]::Chord(0xA3,0xA5,0x4D)
    Check (Wait-Visible 2) 'right Ctrl+right Alt+M restores the shared M target'
} finally {
    foreach ($taskKey in @(0x4D,0x78,0x79,0x7B,0xA1,0xA2,0xA3,0xA4,0xA5)) { [ShortcutTest]::Key($taskKey,$true) }
    if ($taskApp -and !$taskApp.HasExited) {
        [ShortcutTest]::Exit($taskHwnd)
        if (!$taskApp.WaitForExit(3000)) { Stop-Process -Id $taskApp.Id -Force }
    }
    if ($taskFixture -and !$taskFixture.HasExited) { Stop-Process -Id $taskFixture.Id -Force }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskDir 'shortcut-results.txt') -Encoding UTF8
    $taskResults
}
if (@($taskResults | Where-Object { $_ -like 'FAIL*' }).Count) { throw 'Shortcut integration checks failed.' }
