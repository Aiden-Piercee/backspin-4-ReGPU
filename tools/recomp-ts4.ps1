# Recompile Top Spin 4's three XEX images with XenonRecomp. See docs/TS4-RECOMPILE.md.
#
#   tools\recomp-ts4.ps1 -Prepare   dump each image, write the helper addresses and
#                                   exception data into config\*.toml, and generate
#                                   the switch tables with XenonAnalyse
#   tools\recomp-ts4.ps1            recompile into out\ppc, out\ppc_loader, out\ppc_swing
#
# Tools: $env:TS4_TOOLS (default _ext\fm4\build\tools) holds xexdump.exe, dis.exe and
# xenonrecomp\{XenonRecomp,XenonAnalyse}, built from XenonRecomp with the patches in
# patches\. $env:TS4_XENONRECOMP (default _ext\fm4\thirdparty\XenonRecomp) is its source.
param([switch]$Prepare)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot)

$tools = if ($env:TS4_TOOLS) { $env:TS4_TOOLS } else { '_ext\fm4\build\tools' }
$xr = if ($env:TS4_XENONRECOMP) { $env:TS4_XENONRECOMP } else { '_ext\fm4\thirdparty\XenonRecomp' }
$images = @(
    @{ name = 'ts4';        xex = 'default.xex';    sha = '2D72E84FEAB9A84AC0E1239785A9AF1ED7B6E693CF39041F206A92AC9454CE28'; out = 'ppc' },
    @{ name = 'ts4_loader'; xex = 'Loader_DLL.xex'; sha = '8F9ED530C9CF32CF2AE33534BA1CA99E3ABDCF2F5859BC8F68658E23187FD092'; out = 'ppc_loader' },
    @{ name = 'ts4_swing';  xex = 'Swing_DLL.xex';  sha = 'DFA71292F75A649773C280DE7BC2688E6E47E473ABA397C248FB4204C45FAC44'; out = 'ppc_swing' }
)

foreach ($i in $images) {
    $p = "game\$($i.xex)"
    if (-not (Test-Path $p)) { throw "$p is missing (mklink /J game <extracted disc>)" }
    if ((Get-FileHash $p).Hash -ne $i.sha) { throw "${p}: unsupported version (SHA-256 differs)" }
}
New-Item -ItemType Directory -Force out | Out-Null

if ($Prepare) {
    foreach ($i in $images) {
        $n = $i.name
        cmd /c "$tools\xexdump.exe game\$($i.xex) out\$n.bin > out\$n.sections"
        cmd /c "py -3 -I tools\xex_config.py out\$n.bin out\$n.sections --write config\$n.toml > out\$n.config.txt 2>&1"
        Get-Content "out\$n.config.txt"
        cmd /c "$tools\xenonrecomp\XenonAnalyse\XenonAnalyse.exe game\$($i.xex) config\${n}_switch_tables.toml > out\$n.analyse.log 2>&1"
        Get-Content "out\$n.analyse.log" | Select-Object -Last 8
        cmd /c "py -3 -I tools\find_jump_tables.py out\$n.bin out\$n.sections config\${n}_switch_tables.toml --append > nul"
    }
    'Review out\*.config.txt and the config diffs, then run without -Prepare.'
    exit 0
}

if (Select-String config\ts4.toml -Pattern '^restgprlr_14_address = 0x0$' -Quiet) { throw 'config\ts4.toml is not prepared: run with -Prepare first' }

$headers = @()
foreach ($i in $images) {
    $n = $i.name; $dir = "out\$($i.out)"
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null
    cmd /c "$tools\xenonrecomp\XenonRecomp\XenonRecomp.exe config\$n.toml $xr\XenonUtils\ppc_context.h > out\$n.recomp.log 2>&1"
    if ($LASTEXITCODE) { Get-Content "out\$n.recomp.log" -Tail 30; throw "XenonRecomp failed on $n" }
    Get-Content "out\$n.recomp.log" | Where-Object { $_ -notmatch '^Recompiling' } | Select-Object -Last 40
    cmd /c "py -3 -I tools\patch_mftb.py $dir"
    cmd /c "py -3 -I tools\fallthrough.py $dir > out\$n.fallthrough.txt"
    $funcs = (Select-String "$dir\ppc_func_mapping.cpp" -Pattern '^\s*\{ 0x').Count
    $ft = (Get-Content "out\$n.fallthrough.txt").Count
    "${n}: $funcs functions, $ft possible fallthroughs (out\$n.fallthrough.txt)"
    $headers += "$dir\ppc_recomp_shared.h"
}
cmd /c "py -3 -I tools\genstubs.py $($headers -join ' ') src\kernel\stubs.cpp"
cmd /c "py -3 -I tools\genmodules.py src\recomp_modules.cpp config\ts4.toml config\ts4_loader.toml config\ts4_swing.toml"
