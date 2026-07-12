# Build the Debug Standalone and tee all output to build_log.txt so it can be
# inspected after the console closes. Used to verify the amp-face redesign.
$ErrorActionPreference = "Continue"
$log = "C:\Dev\NAM-Rig\build_log.txt"
"BUILD START $(Get-Date -Format o)" | Out-File $log
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$out = cmd /c "`"$vcvars`" >nul 2>&1 && `"$cmake`" --build C:\Dev\NAM-Rig\build-clang --config Debug --target NAM_Rig_Standalone 2>&1"
$out | Out-File -Append $log
if ($out | Select-String -Pattern "error:|error C|FAILED|ninja: build stopped") {
  "RESULT: BUILD FAILED" | Out-File -Append $log
} else {
  "RESULT: BUILD OK" | Out-File -Append $log
}
"BUILD END $(Get-Date -Format o)" | Out-File -Append $log
