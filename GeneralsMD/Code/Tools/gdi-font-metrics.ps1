#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
# gdi-font-metrics.ps1: GDI's TEXTMETRIC for the game's fonts, as render2dsentence.cpp's Windows branch asks
# for them: CreateFont(-ppem, width, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
# CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, face) into a memory DC, then GetTextMetrics.
# Writes gdi-metrics.csv and fonts.txt beside itself.  Reads C:\Windows\Fonts only.
# Run on Windows: powershell -NoProfile -ExecutionPolicy Bypass -File gdi-font-metrics.ps1
# Then, anywhere: python3 gdi-font-metrics-header.py <that folder> > Libraries/Source/WWVegas/WW3D2/gdifontmetrics.h
# (L1b, 2026-09-27: Windows 11 build 26200.6584; see that header for the fonts and the cross-checks.)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Gdi {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct TEXTMETRICW {
    public int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth,
      tmMaxCharWidth, tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    public char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    public byte tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
  }
  [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr CreateFontW(int h, int w, int esc,
    int orient, int weight, uint italic, uint underline, uint strike, uint charset, uint outPrec, uint clipPrec,
    uint quality, uint pitch, string face);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
  [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
  [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern int GetDeviceCaps(IntPtr dc, int index);
  [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern bool GetTextMetricsW(IntPtr dc, out TEXTMETRICW tm);
  [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern int GetTextFaceW(IntPtr dc, int n, System.Text.StringBuilder s);
}
"@

$dc = [Gdi]::CreateCompatibleDC([IntPtr]::Zero)
if ($dc -eq [IntPtr]::Zero) { throw 'CreateCompatibleDC failed' }
$rows = New-Object System.Collections.Generic.List[string]
$rows.Add('face,bold,width,ppem,tmHeight,tmAscent,tmDescent,tmOverhang,selected')
$faces = @('Arial', 'Times New Roman', 'Courier New')
foreach ($face in $faces) {
  foreach ($bold in @(0, 1)) {
    $widths = @('0')
    if ($face -eq 'Arial') { $widths = @('0', 'generals') }	# "Generals": Arial at lfWidth = (int)(ppem * 0.40f)
    foreach ($wmode in $widths) {
      for ($ppem = 6; $ppem -le 48; $ppem++) {
        $w = 0
        if ($wmode -eq 'generals') { $w = [int][Math]::Truncate([single]$ppem * [single]0.40) }
        $weight = 400; if ($bold) { $weight = 700 }
        $font = [Gdi]::CreateFontW(-$ppem, $w, 0, 0, $weight, 0, 0, 0, 1, 0, 0, 4, 2, $face)
        $old = [Gdi]::SelectObject($dc, $font)
        $tm = New-Object Gdi+TEXTMETRICW
        [void][Gdi]::GetTextMetricsW($dc, [ref]$tm)
        $sb = New-Object System.Text.StringBuilder 64
        [void][Gdi]::GetTextFaceW($dc, 64, $sb)
        [void][Gdi]::SelectObject($dc, $old)
        [void][Gdi]::DeleteObject($font)
        $rows.Add(('{0},{1},{2},{3},{4},{5},{6},{7},{8}' -f $face, $bold, $w, $ppem, $tm.tmHeight, $tm.tmAscent,
          $tm.tmDescent, $tm.tmOverhang, $sb.ToString()))
      }
    }
  }
}
$logpixelsy = [Gdi]::GetDeviceCaps($dc, 90)
[void][Gdi]::DeleteDC($dc)
[System.IO.File]::WriteAllLines((Join-Path $here 'gdi-metrics.csv'), $rows)

# Which files those faces are, and their versions
Add-Type -AssemblyName PresentationCore
$info = New-Object System.Collections.Generic.List[string]
$os = Get-CimInstance Win32_OperatingSystem
$info.Add(('Windows: {0} build {1}.{2}, LOGPIXELSY of the memory DC {3}' -f $os.Caption, $os.BuildNumber,
  (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR, $logpixelsy))
foreach ($file in @('arial.ttf', 'arialbd.ttf', 'times.ttf', 'timesbd.ttf', 'cour.ttf', 'courbd.ttf')) {
  $path = Join-Path $env:WINDIR ('Fonts\' + $file)
  $g = New-Object System.Windows.Media.GlyphTypeface (New-Object System.Uri $path)
  $version = $g.VersionStrings[[System.Globalization.CultureInfo]::GetCultureInfo('en-US')]
  $hash = (Get-FileHash -Algorithm SHA256 $path).Hash
  $info.Add(('{0}: {1}, {2} bytes, sha256 {3}' -f $file, $version, (Get-Item $path).Length, $hash))
}
[System.IO.File]::WriteAllLines((Join-Path $here 'fonts.txt'), $info)
Write-Output ('rows: {0}' -f ($rows.Count - 1))
Get-Content (Join-Path $here 'fonts.txt')
