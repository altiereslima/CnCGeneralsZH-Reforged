# Photograph the same eight views twice, once with the stencil volumes and once with the shadow
# map, and put the two pictures of each side by side.  tree-check.ps1 answers "does this build draw
# what the last one drew"; this answers the other question, which is what the two shadow mechanisms
# make of the same frame.  SHADOW-MAP-PLAN.md phase 4.
#
#   .\shadow-check.ps1            # eight views, both ways, a sheet at the end
#   .\shadow-check.ps1 -Only 3    # one view, while a knob is being turned

param([int]$Only = 0)

Add-Type -AssemblyName System.Drawing
$run = Join-Path $PSScriptRoot "GeneralsMD\Run"
$shots = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data"
$out = Join-Path $env:TEMP "shadowcheck"
if (-not (Test-Path $out)) { $null = New-Item -ItemType Directory $out }

# The same views tree-check uses, for the same reason: they are the ones with something in them.
$cases = @(
  @{map='Flash Effect';       x='1200'; y='945';  f=400},
  @{map='Flash Effect';       x='1816'; y='1861'; f=1200},
  @{map='Flash Effect';       x='1378'; y='1384'; f=2400},
  @{map='ForgottenForestZH';  x='1620'; y='1470'; f=600},
  @{map='ForgottenForestZH';  x='1543'; y='1636'; f=1800},
  @{map='Golden Oasis';       x='2643'; y='3580'; f=900},
  @{map='Alpine Assault';     x='760';  y='920';  f=700},
  @{map='Killing Fields';     x='1024'; y='1024'; f=1500}
)

function Shoot($case, $tag, $extra) {
  Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue | Remove-Item -Force
  $arguments = @('-win','-xres','1280','-yres','720','-quickstart','-noshellmap','-multiInstance',
    '-msaa','0','-dx11post','off','-map',"`"Maps\$($case.map)\$($case.map).map`"",
    '-autoskirmish','4','-aidiff','easy','-seed','5','-maxframes',($case.f+80),
    '-screenshot',$case.f,'-camera',$case.x,$case.y,'-logPrefix',"shd_$tag`_")
  if ($extra) { $arguments += $extra }
  try {
    $process = Start-Process (Join-Path $run "generals.exe") -ArgumentList $arguments `
      -WorkingDirectory $run -PassThru
    $null = $process.WaitForExit(900000)
  }
  finally {
    Get-Process -Name generals -ErrorAction SilentlyContinue | Stop-Process -Force
  }
  # The picture is written on the frame it was asked for and the process runs on to its frame
  # limit, so the file can land a moment after the wait returns.  A view that still has none is
  # reported and skipped rather than ending the sweep: seven views say more than one exception.
  $bmp = $null
  for ($attempt = 0; $attempt -lt 5 -and $null -eq $bmp; $attempt++) {
    Start-Sleep -Milliseconds 500
    $bmp = Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime | Select-Object -Last 1
  }
  if ($null -eq $bmp) {
    Write-Host "no screenshot for $tag"
    return $null
  }
  $image = [System.Drawing.Image]::FromFile($bmp.FullName)
  $path = "$out\$tag.png"
  $image.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $image.Dispose()
  return $path
}

$index = 0
foreach ($case in $cases) {
  $index++
  if ($Only -gt 0 -and $index -ne $Only) { continue }
  $name = "{0:d2}" -f $index
  # The map is how the game draws now, so the pair is the frame as it ships against the frame with
  # both shadow mechanisms in it, which is the only way left to see the volumes at all.
  $map = Shoot $case "$name`_map" $null
  $volumes = Shoot $case "$name`_volumes" @('-shadowmapboth')
  Write-Host "$name $($case.map) $($case.x),$($case.y): $volumes | $map"
}

Write-Host "pictures in $out"
