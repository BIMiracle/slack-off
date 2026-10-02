param([string]$Exe = (Join-Path $PSScriptRoot '..\release\SlackOff.exe'))
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SlackTest {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
 public static IntPtr Main() { return FindWindow("SlackOff.Main.v1",null); }
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,int a,out Rect r,int n);
 public static Rect Bounds(IntPtr h) {Rect r;if(DwmGetWindowAttribute(h,9,out r,16)!=0)GetWindowRect(h,out r);return r;}
 public static void Action(IntPtr h,int id) { SendMessage(h,0x0312,(IntPtr)id,IntPtr.Zero); }
 public static void Mouse(IntPtr h,int x,int y) { SendMessage(h,0x8064,(IntPtr)x,(IntPtr)y); }
 [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr CreateWindowEx(uint ex,string c,string t,uint s,int x,int y,int w,int h,IntPtr parent,IntPtr menu,IntPtr inst,IntPtr arg);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string s);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h,string t);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder t,int n);
 public static string Text(IntPtr h) { var b=new StringBuilder(1024); GetWindowText(h,b,b.Capacity); return b.ToString(); }
 public static IntPtr[] Windows(uint pid, bool visible) {
  var list=new List<IntPtr>(); EnumWindows((h,l)=>{uint p; GetWindowThreadProcessId(h,out p); if(p==pid && (!visible || IsWindowVisible(h))) list.Add(h); return true;},IntPtr.Zero); return list.ToArray();
 }
 public static int FixtureCount(uint pid) { int count=0;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var b=new StringBuilder(256);GetClassName(h,b,256);if(p==pid && b.ToString()=="SlackOff.Fixture")count++;return true;},IntPtr.Zero);return count; }
 public static void Hotkey(byte key) { keybd_event(0x11,0,0,UIntPtr.Zero); keybd_event(0x12,0,0,UIntPtr.Zero); keybd_event(key,0,0,UIntPtr.Zero); keybd_event(key,0,2,UIntPtr.Zero); keybd_event(0x12,0,2,UIntPtr.Zero); keybd_event(0x11,0,2,UIntPtr.Zero); }
}
'@
$taskDir = Join-Path $env:TEMP 'SlackOff-UI-test'
New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
if ([SlackTest]::Main() -ne [IntPtr]::Zero) { throw 'Close the existing SlackOff test/normal instance before integration tests.' }
$taskFixtureExe = Join-Path $taskDir 'TestPlayer.exe'
Copy-Item -LiteralPath $Exe -Destination $taskFixtureExe -Force
$taskConfig = @"
[General]
AutoStart=0
Restore=7,123
Count=1
[Rule0]
Name=测试播放器
Path=$taskFixtureExe
Toggle=3,120
Watch=3,121
Before=0,32
Delay=50
"@
[IO.File]::WriteAllText((Join-Path $taskDir 'settings.ini'),$taskConfig,[Text.Encoding]::Unicode)
$taskResults = [Collections.Generic.List[string]]::new()
function Check([bool]$ok,[string]$label) {
    $taskResults.Add("$(if($ok){'PASS'}else{'FAIL'}) $label")
    if (!$ok) { Write-Warning $label }
}
function Wait-Visible([int]$expected) {
    for ($i=0;$i -lt 50;$i++) { if (@([SlackTest]::Windows($taskFixture.Id,$true)).Count -eq $expected) { return $true }; Start-Sleep -Milliseconds 100 }
    return $false
}
$taskFixture = $null
$taskApp = $null
try {
    $taskFixture = Start-Process -FilePath $taskFixtureExe -ArgumentList '--fixture' -WindowStyle Hidden -PassThru
    Check (Wait-Visible 2) 'fixture has two visible windows'
    $taskOriginal = @([SlackTest]::Windows($taskFixture.Id,$true))
    $taskApp = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200
    $taskHwnd = [SlackTest]::Main()
    Check ($taskHwnd -ne [IntPtr]::Zero) 'settings window starts'
    [void][SlackTest]::SetForegroundWindow($taskOriginal[0])
    [SlackTest]::Action($taskHwnd,101)
    Check (Wait-Visible 0) 'registered hotkey dispatch hides all target windows'
    $taskStatus = [SlackTest]::Text([SlackTest]::GetDlgItem($taskHwnd,30))
    Check (([SlackTest]::Text($taskOriginal[0]) -eq 'Paused') -or ($taskStatus -like '*按键发送未确认*')) 'pre-hide delivery succeeds or reports unconfirmed without blocking hide'
    [void][SlackTest]::ShowWindowAsync($taskOriginal[0],1)
    Start-Sleep -Milliseconds 300
    Check (![SlackTest]::IsWindowVisible($taskOriginal[0])) 'reshown hidden window automatically hidden'
    [void][SlackTest]::PostMessage($taskOriginal[0],0x800A,[IntPtr]::Zero,[IntPtr]::Zero)
    for ($i=0;$i -lt 30;$i++) { if ([SlackTest]::FixtureCount($taskFixture.Id) -ge 4) { break }; Start-Sleep -Milliseconds 100 }
    Start-Sleep -Milliseconds 250
    Check (([SlackTest]::FixtureCount($taskFixture.Id) -eq 4) -and (Wait-Visible 0)) 'new owned target window is captured and hidden while rule is hidden'
    [SlackTest]::Action($taskHwnd,101)
    Check (Wait-Visible 3) 'same hotkey restores original and newly hidden windows; originally hidden remains hidden'
    [void][SlackTest]::SetForegroundWindow($taskOriginal[0])
    [SlackTest]::Action($taskHwnd,102)
    Start-Sleep -Milliseconds 300
    $taskWatched = [SlackTest]::SendMessage($taskHwnd,0x8065,[IntPtr]::Zero,[IntPtr]::Zero)
    Write-Output "Watch handle: $taskWatched; status: $([SlackTest]::Text([SlackTest]::GetDlgItem($taskHwnd,30)))"
    Check ($taskWatched -ne [IntPtr]::Zero) 'watch installs a mouse hook and selects a visible window'
    $taskBounds = [SlackTest]::Bounds($taskOriginal[0])
    $taskWatchBounds = [SlackTest]::Bounds($taskWatched)
    [SlackTest]::Mouse($taskHwnd,($taskWatchBounds.Left+80),($taskWatchBounds.Top+80))
    Start-Sleep -Milliseconds 200
    [SlackTest]::Mouse($taskHwnd,($taskBounds.Right+100),($taskBounds.Bottom+100))
    Check (Wait-Visible 0) 'simulated mouse entering then leaving uses production boundary handler to hide'
    [SlackTest]::Action($taskHwnd,101)
    Check (Wait-Visible 3) 'restore during watching mode'
    Start-Sleep -Milliseconds 300
    Check (@([SlackTest]::Windows($taskFixture.Id,$true)).Count -eq 3) 'restore outside window does not immediately re-hide'
    Check ([SlackTest]::SendMessage($taskHwnd,0x8065,[IntPtr]::Zero,[IntPtr]::Zero) -ne [IntPtr]::Zero) 'watch monitoring resumes after asynchronous restore'
    [SlackTest]::Action($taskHwnd,102)
    Start-Sleep -Milliseconds 200
    [SlackTest]::Mouse($taskHwnd,($taskBounds.Left+80),($taskBounds.Top+80)); Start-Sleep -Milliseconds 150
    [SlackTest]::Mouse($taskHwnd,($taskBounds.Right+100),($taskBounds.Bottom+100)); Start-Sleep -Milliseconds 250
    Check (@([SlackTest]::Windows($taskFixture.Id,$true)).Count -eq 3) 'disabled watch has no mouse hiding'
    # A crash between hide and restore must not strand the target windows.
    [void][SlackTest]::SetForegroundWindow($taskOriginal[0]); [SlackTest]::Action($taskHwnd,101)
    Check (Wait-Visible 0) 'hidden before simulated crash'
    [SlackTest]::Action($taskHwnd,102)
    Check (Wait-Visible 3) 'enabling watch on a hidden target restores it'
    Start-Sleep -Milliseconds 150
    Check ([SlackTest]::SendMessage($taskHwnd,0x8065,[IntPtr]::Zero,[IntPtr]::Zero) -ne [IntPtr]::Zero) 'watch arms after restoring a hidden target'
    [SlackTest]::Action($taskHwnd,102)
    [SlackTest]::Action($taskHwnd,101)
    Check (Wait-Visible 0) 'target rehidden for crash recovery'
    Stop-Process -Id $taskApp.Id -Force; $taskApp.WaitForExit()
    $taskApp = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
    Check (Wait-Visible 3) 'next startup recovers crash-hidden windows'
    Start-Sleep -Milliseconds 500
    $taskHwnd = [SlackTest]::Main()
    $taskSecond = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
    Check ($taskSecond.WaitForExit(3000)) 'second instance exits'
    Check ([SlackTest]::IsWindowVisible($taskHwnd)) 'second instance opens existing settings'
    [void][SlackTest]::SendMessage($taskHwnd,0x111,[IntPtr]404,[IntPtr]::Zero)
    Check ($taskApp.WaitForExit(3000)) 'tray exit terminates after restoration'
} finally {
    if ($taskApp -and !$taskApp.HasExited) {
        $taskHwnd = [SlackTest]::Main()
        if ($taskHwnd -ne [IntPtr]::Zero) { [void][SlackTest]::SendMessage($taskHwnd,0x111,[IntPtr]404,[IntPtr]::Zero) }
        if (!$taskApp.WaitForExit(3000)) { Stop-Process -Id $taskApp.Id -Force }
    }
    if ($taskFixture -and !$taskFixture.HasExited) { Stop-Process -Id $taskFixture.Id -Force }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskDir 'integration-results.txt') -Encoding UTF8
    $taskResults
}
if (@($taskResults | Where-Object { $_ -like 'FAIL*' }).Count) { throw 'Integration checks failed.' }
