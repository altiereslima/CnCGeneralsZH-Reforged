# Fetches the third-party sources the build needs and this repository does not carry.
#
# EA stripped them from the source release and they are not ours to commit, so every fresh clone
# has to get them once. build.bat runs this before it configures; running it again when everything
# is in place costs one directory check per library and nothing else.
#
#   -Force   re-fetch even what is already there
#
# What it cannot get is the game itself: the .big files from a Zero Hour install go next to
# generals.exe in GeneralsMD\Run, and the base game's in Run\ZH_Generals. The game says so on
# startup when they are missing.
[CmdletBinding()]
param([switch] $Force)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$codeRoot = Split-Path -Parent $PSScriptRoot
$libraries = Join-Path $codeRoot 'Libraries'
$runFolder = Join-Path (Split-Path -Parent $codeRoot) 'Run'
$work = Join-Path $env:TEMP 'zhr-vendor'

function Step($message) { Write-Host "[vendor] $message" }

function Get-File($url, $destination) {
  if (Test-Path $destination) { return $destination }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Step "downloading $(Split-Path -Leaf $destination)"
  Invoke-WebRequest -Uri $url -OutFile $destination -UseBasicParsing
  return $destination
}

# Unpacks into a folder of its own and hands back whatever single directory the archive contained,
# which for a GitHub source zip is the repository at that commit.
function Expand-Source($archive, $name) {
  $target = Join-Path $work $name
  if (Test-Path $target) { Remove-Item -Recurse -Force $target }
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Step "unpacking $name"
  if ($archive.EndsWith('.tar.gz')) {
    tar -xf $archive -C $target
  } else {
    Expand-Archive -Path $archive -DestinationPath $target -Force
  }
  $entries = @(Get-ChildItem $target)
  if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { return $entries[0].FullName }
  return $target
}

function Copy-Files($sourceFiles, $destination) {
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  foreach ($file in $sourceFiles) { Copy-Item $file.FullName (Join-Path $destination $file.Name) -Force }
}

# The files directly in a folder with one of these extensions. Get-ChildItem -Include is not that:
# without -Recurse it silently matches nothing, which is how the first run of this copied no zlib.
function Get-TopLevel($folder, [string[]] $extensions) {
  Get-ChildItem $folder -File | Where-Object { $extensions -contains $_.Extension }
}

# --- zlib 1.1.4, flat. maketree.c is a generator with its own main() and does not belong in the lib.
function Install-Zlib {
  $destination = Join-Path $libraries 'Source\Compression\ZLib'
  if ((Test-Path (Join-Path $destination 'deflate.c')) -and -not $Force) { return }
  $archive = Get-File 'https://zlib.net/fossils/zlib-1.1.4.tar.gz' (Join-Path $work 'zlib-1.1.4.tar.gz')
  $source = Expand-Source $archive 'zlib'
  Copy-Files (Get-TopLevel $source @('.c', '.h') | Where-Object { $_.Name -ne 'maketree.c' }) $destination
  Step "zlib 1.1.4 -> Libraries\Source\Compression\ZLib"
}

# --- LZH-Light 1.0. Lzhl_tcp.cpp is a socket layer nothing calls; Test.c has its own main().
function Install-Lzhl {
  $header = Join-Path $libraries 'Source\Compression\LZHCompress\CompLibHeader'
  $sourceFolder = Join-Path $libraries 'Source\Compression\LZHCompress\CompLibSource'
  if ((Test-Path (Join-Path $sourceFolder 'Lzhl.cpp')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/lzhl-1.0/archive/dfd96e2.zip' (Join-Path $work 'lzhl.zip')
  $source = Expand-Source $archive 'lzhl'
  Copy-Files (Get-ChildItem $source -File -Filter *.h) $header
  Copy-Files (Get-TopLevel $source @('.cpp', '.tbl') |
    Where-Object { $_.Name -notin @('Lzhl_tcp.cpp', 'Test.c') }) $sourceFolder
  Step "LZH-Light 1.0 -> Libraries\Source\Compression\LZHCompress"
}

# --- DirectX 8 headers and import libraries. extra\ is not wholesale-copyable: basetsd.h, d3d.h,
# ddraw.h and dsound.h there shadow the modern Windows SDK and break winnt.h. Three files from it
# are needed, because ww3d2\pointgr.cpp includes D3DXMath.h.
function Install-DirectX {
  $include = Join-Path $libraries 'DirectX\Include'
  $lib = Join-Path $libraries 'DirectX\Lib'
  if ((Test-Path (Join-Path $include 'd3d8.h')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/min-dx8-sdk/archive/7bddff8.zip' (Join-Path $work 'dx8.zip')
  $source = Expand-Source $archive 'dx8'
  Copy-Files (Get-TopLevel $source @('.h', '.inl')) $include
  Copy-Files (Get-ChildItem $source -File -Filter *.lib) $lib
  Copy-Files (Get-ChildItem (Join-Path $source 'extra') -File |
    Where-Object { $_.Name -in @('d3dxmath.h', 'd3dxmath.inl', 'd3dxerr.h') }) $include
  Step "min-dx8-sdk -> Libraries\DirectX"
}

# --- GameSpy SDK, whole repository. It brings its own CMakeLists, which CMakeLists.txt adds.
function Install-GameSpy {
  $destination = Join-Path $libraries 'Source\GameSpy'
  if ((Test-Path (Join-Path $destination 'CMakeLists.txt')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/GamespySDK/archive/b1b77d8.zip' (Join-Path $work 'gamespy.zip')
  $source = Expand-Source $archive 'gamespy'
  # Every one of these folders holds a committed .gitignore that keeps the code out of the
  # repository. Emptying the folder first takes that with it, and then the whole SDK shows up as
  # untracked - which is how 780 files of third-party source nearly went into a commit.
  $keep = Join-Path $destination '.gitignore'
  $kept = if (Test-Path $keep) { Get-Content $keep -Raw } else { $null }
  if (Test-Path $destination) { Remove-Item -Recurse -Force $destination }
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  Copy-Item (Join-Path $source '*') $destination -Recurse -Force
  if ($null -ne $kept) { Set-Content -Path $keep -Value $kept -NoNewline }
  Step "GamespySDK -> Libraries\Source\GameSpy"
}

# --- The fork's own upscaled art: every 3D texture at twice its size, the normal maps and the
# ground. It is not in git because one of the three is a gigabyte, ten times what GitHub takes in a
# file, and LFS on a fork bills the parent repository. It hangs off a release instead.
#
# The game plays without it, at the textures it shipped with, so a download that is not there yet
# is a note rather than a failure.
$artRelease = 'https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/data-v1'
$art = [ordered]@{
  'ReforgedTextures.big' = '638baa84af2a3cec326932e1923921c462c6dbf8a6a0d8d53d473a1a46bca670'
  'ReforgedNormals.big'  = 'f7408d4010187509c7083640ddbe346ec273f1c8efdfcb30b2d9fc3a5830ef42'
  'ReforgedTerrain.big'  = '8e8081dcefbd8dbd004585bbd38f62de0a8e729fc80134c632f4f22f1a3a2f0d'
}

function Install-Art {
  New-Item -ItemType Directory -Force -Path $runFolder | Out-Null
  foreach ($name in $art.Keys) {
    $target = Join-Path $runFolder $name
    if ((Test-Path $target) -and -not $Force) { continue }
    $partial = "$target.part"
    try {
      Step "downloading $name (this one is large)"
      Invoke-WebRequest -Uri "$artRelease/$name" -OutFile $partial -UseBasicParsing
    } catch {
      Remove-Item $partial -ErrorAction SilentlyContinue
      Step "$name is not on the release yet; the game will use the textures it shipped with"
      continue
    }
    $hash = (Get-FileHash $partial -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $art[$name]) {
      Remove-Item $partial -ErrorAction SilentlyContinue
      throw "$name downloaded with hash $hash, expected $($art[$name])"
    }
    Move-Item $partial $target -Force
    Step "$name -> Run"
  }
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
Install-Zlib
Install-Lzhl
Install-DirectX
Install-GameSpy
Install-Art
Step 'everything the build needs is in place'
