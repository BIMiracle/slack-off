param([string]$Exe = (Join-Path $PSScriptRoot '..\release\SlackOff.exe'), [int]$Seconds = 10)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class MeasureNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static IntPtr Main(){return FindWindow("SlackOff.Main.v1",null);}
}
'@
if ([MeasureNative]::Main() -ne [IntPtr]::Zero) { throw 'Close SlackOff before measuring.' }
$taskDir = Join-Path $env:TEMP 'SlackOff-UI-test'
New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
$taskFixtureExe = Join-Path $taskDir 'TestPlayer.exe'
Copy-Item -LiteralPath $Exe -Destination $taskFixtureExe -Force
[IO.File]::WriteAllText((Join-Path $taskDir 'settings.ini'),"[General]`r`nAutoStart=0`r`nRestore=7,123`r`nCount=1`r`n[Rule0]`r`nName=TestPlayer`r`nPath=$taskFixtureExe`r`nToggle=3,120`r`nWatch=3,121`r`nBefore=0,0`r`nDelay=50`r`n",[Text.Encoding]::Unicode)
$taskFixture = $null; $taskApp = $null
function Sample([string]$mode) {
    $taskApp.Refresh(); $startCpu = $taskApp.TotalProcessorTime.TotalMilliseconds
    $clock = [Diagnostics.Stopwatch]::StartNew(); $private = 0L; $working = 0L
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 500; $taskApp.Refresh()
        $private = [Math]::Max($private,$taskApp.PrivateMemorySize64)
        $working = [Math]::Max($working,$taskApp.WorkingSet64)
    }
    $taskApp.Refresh()
    $cpu = $taskApp.TotalProcessorTime.TotalMilliseconds - $startCpu
    [pscustomobject]@{ mode=$mode; seconds=[Math]::Round($clock.Elapsed.TotalSeconds,3); privateBytes=$private; privateMiB=[Math]::Round($private/1MB,3); workingSetBytes=$working; cpuMilliseconds=$cpu; oneCoreCpuPercent=[Math]::Round($cpu/$clock.Elapsed.TotalMilliseconds*100,4) }
}
try {
    $taskFixture = Start-Process -FilePath $taskFixtureExe -ArgumentList '--fixture' -WindowStyle Hidden -PassThru
    $taskApp = Start-Process -FilePath $Exe -ArgumentList '--test-instance' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1500
    $taskHwnd = [MeasureNative]::Main()
    if ($taskHwnd -eq [IntPtr]::Zero) { throw 'Application did not start.' }
    [void][MeasureNative]::SendMessage($taskHwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    $idle = Sample 'idle-tray'
    [void][MeasureNative]::SendMessage($taskHwnd,0x312,[IntPtr]102,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    if ([MeasureNative]::SendMessage($taskHwnd,0x8065,[IntPtr]::Zero,[IntPtr]::Zero) -eq [IntPtr]::Zero) { throw 'Watching mode failed to start.' }
    $watch = Sample 'watching-hook-installed'
    $result = [pscustomobject]@{ measuredAt=(Get-Date -Format 'o'); exeBytes=(Get-Item -LiteralPath $Exe).Length; windows=[Environment]::OSVersion.VersionString; logicalProcessors=[Environment]::ProcessorCount; samples=@($idle,$watch) }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskDir 'performance.json') -Encoding UTF8
    $result | ConvertTo-Json -Depth 5
} finally {
    if ($taskApp -and !$taskApp.HasExited) { [void][MeasureNative]::SendMessage([MeasureNative]::Main(),0x111,[IntPtr]404,[IntPtr]::Zero); if (!$taskApp.WaitForExit(3000)) { Stop-Process -Id $taskApp.Id -Force } }
    if ($taskFixture -and !$taskFixture.HasExited) { Stop-Process -Id $taskFixture.Id -Force }
}
