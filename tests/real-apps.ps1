param([string]$Exe = (Join-Path $PSScriptRoot '..\release\SlackOff.exe'))
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;using System.Collections.Generic;using System.Runtime.InteropServices;using System.Text;
public static class RealNative {
 public delegate bool Callback(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h,int a,out uint v,int n);
 static bool DesktopVisible(IntPtr h){uint cloaked;return IsWindowVisible(h) && (DwmGetWindowAttribute(h,14,out cloaked,4)!=0 || cloaked==0);}
 [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h,int c);
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr h,int i);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,string t);
 public static IntPtr Main(){return FindWindow("SlackOff.Main.v1",null);}
 public static IntPtr[] Windows(uint[] ids){var windows=new List<IntPtr>();EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);if(Array.IndexOf(ids,p)>=0 && DesktopVisible(h))windows.Add(h);return true;},IntPtr.Zero);return windows.ToArray();}
 public static IntPtr[] HiddenMainWindows(uint[] ids){var windows=new List<IntPtr>();EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var style=GetWindowLongPtr(h,-16).ToInt64();var ex=GetWindowLongPtr(h,-20).ToInt64();var title=new StringBuilder(256);GetWindowText(h,title,256);if(Array.IndexOf(ids,p)>=0 && !IsWindowVisible(h) && (style&0x40000000)==0 && (style&0x00C00000)!=0 && (ex&0x80)==0 && title.Length>0)windows.Add(h);return true;},IntPtr.Zero);return windows.ToArray();}
}
'@
if ([RealNative]::Main() -ne [IntPtr]::Zero) { throw 'Close SlackOff before real-app tests.' }
$taskDir = Join-Path $env:TEMP 'SlackOff-UI-test'
New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
$taskVideo = Join-Path $taskDir 'sample.mp4'
& ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc=size=320x180:rate=10' -t 3 -pix_fmt yuv420p -y $taskVideo
if ($LASTEXITCODE) { throw 'Test video generation failed.' }
$taskNote = Join-Path $taskDir 'SlackOff-validation.txt'
'SlackOff window hide and restore validation.' | Set-Content -LiteralPath $taskNote -Encoding UTF8
$taskResults = [Collections.Generic.List[string]]::new()
$taskCandidates = @(
    @{name='Edge'; process='msedge'; path='C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'; arguments=@("--user-data-dir=`"$taskDir\edge-profile`"",'--no-first-run','--no-default-browser-check','--app=about:blank')},
    @{name='Notepad'; process='notepad'; path='C:\Windows\System32\notepad.exe'; arguments=@("`"$taskNote`"")},
    @{name='Windows Media Player'; process='wmplayer'; path='C:\Program Files\Windows Media Player\wmplayer.exe'; arguments=@("`"$taskVideo`"")}
)
$taskMediaPackage = Get-AppxPackage -Name Microsoft.ZuneMusic -ErrorAction SilentlyContinue
if ($taskMediaPackage) {
    $taskModernPlayer = Join-Path $taskMediaPackage.InstallLocation 'Microsoft.Media.Player.exe'
    if (Test-Path -LiteralPath $taskModernPlayer) { $taskCandidates += @{name='Windows 11 Media Player'; process='Microsoft.Media.Player'; path=$taskModernPlayer; arguments=@("`"$taskVideo`"")} }
}
$taskFfplay = Get-Command ffplay -ErrorAction SilentlyContinue
if ($taskFfplay) { $taskCandidates += @{name='FFplay video player'; process='ffplay'; path=$taskFfplay.Source; arguments=@('-loop','0','-window_title','SlackOffVideoValidation',"`"$taskVideo`"")} }
foreach ($taskCandidate in $taskCandidates) {
    if (!(Test-Path -LiteralPath $taskCandidate.path)) { $taskResults.Add("SKIP $($taskCandidate.name): not installed"); continue }
    if (Get-Process -Name $taskCandidate.process -ErrorAction SilentlyContinue) { $taskResults.Add("SKIP $($taskCandidate.name): existing user process, leaving it untouched"); continue }
    $taskApp = $null; $taskTarget = $null; $taskWindows = @(); $taskProcesses = @()
    try {
        $taskTarget = Start-Process -FilePath $taskCandidate.path -ArgumentList $taskCandidate.arguments -WindowStyle Hidden -PassThru
        for ($i=0;$i -lt 80;$i++) {
            $taskProcesses = @(Get-Process -Name $taskCandidate.process -ErrorAction SilentlyContinue)
            $taskIds = [uint[]]@($taskProcesses | ForEach-Object { $_.Id })
            $taskWindows = @([RealNative]::Windows($taskIds))
            if (!$taskWindows.Count -and $i -ge 10) {
                foreach ($taskHiddenMain in [RealNative]::HiddenMainWindows($taskIds)) { [void][RealNative]::ShowWindowAsync($taskHiddenMain,1) }
            }
            if ($taskWindows.Count) { break }; Start-Sleep -Milliseconds 100
        }
        if (!$taskWindows.Count) { $taskResults.Add("SKIP $($taskCandidate.name): no visible desktop window"); continue }
        # Resolve the running image; modern Notepad may redirect its launcher.
        $taskImage = ($taskProcesses | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1).Path
        if (!$taskImage) { $taskImage = $taskCandidate.path }
        $taskConfig = "[General]`r`nAutoStart=0`r`nRestore=7,123`r`nCount=1`r`n[Rule0]`r`nName=$($taskCandidate.name)`r`nPath=$taskImage`r`nToggle=3,120`r`nWatch=3,121`r`nBefore=0,0`r`nDelay=50`r`n"
        [IO.File]::WriteAllText((Join-Path $taskDir 'settings.ini'),$taskConfig,[Text.Encoding]::Unicode)
        $taskApp = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 1000
        $taskHwnd = [RealNative]::Main()
        if ($taskHwnd -eq [IntPtr]::Zero) { throw 'SlackOff failed to start.' }
        [void][RealNative]::SendMessage($taskHwnd,0x312,[IntPtr]101,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 350
        $taskHidden = @($taskWindows | Where-Object { [RealNative]::IsWindowVisible($_) }).Count -eq 0
        $taskResults.Add("$(if($taskHidden){'PASS'}else{'FAIL'}) $($taskCandidate.name): visible windows hidden")
        [void][RealNative]::SendMessage($taskHwnd,0x312,[IntPtr]101,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 350
        $taskRestored = @($taskWindows | Where-Object { ![RealNative]::IsWindowVisible($_) }).Count -eq 0
        $taskResults.Add("$(if($taskRestored){'PASS'}else{'FAIL'}) $($taskCandidate.name): original windows restored")
    } finally {
        if ($taskApp -and !$taskApp.HasExited) { [void][RealNative]::SendMessage([RealNative]::Main(),0x111,[IntPtr]404,[IntPtr]::Zero); if (!$taskApp.WaitForExit(3000)) { Stop-Process -Id $taskApp.Id -Force } }
        foreach ($taskWindow in $taskWindows) { [void][RealNative]::PostMessage($taskWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        Start-Sleep -Milliseconds 500
        # Only processes captured for this isolated launch are eligible for cleanup.
        foreach ($taskProcess in $taskProcesses) { if (!$taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id -Force -ErrorAction SilentlyContinue } }
    }
}
$taskResults | Set-Content -LiteralPath (Join-Path $taskDir 'real-app-results.txt') -Encoding UTF8
$taskResults
if (@($taskResults | Where-Object { $_ -like 'FAIL*' }).Count) { throw 'Real application window checks failed.' }
