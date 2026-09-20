# Finds out which Chroma surfaces this machine actually has, and where they are.
# Needed because a frame sent to a device nobody owns is answered exactly like one
# that lit, so the only way to place a surface is to light it and have somebody
# look. What this one has left to settle is whether a BlackWidow V4 Pro's underglow
# strip and its five macro keys are addressable at all, and through which endpoint.
# Four colours go out at once so one look answers all of them:
#   RED     keyboard grid column 0, rows 0-5   (the left macro column, if that is what it is)
#   GREEN   keyboard grid row 0, columns 18-21 (above the numpad, undocumented on most boards)
#   BLUE    the chromalink endpoint            (where an underglow strip usually lands)
#   MAGENTA the keypad endpoint
# Everything else on the keyboard is left dark so the lit cells stand out.

$ErrorActionPreference = 'Stop'

$HOLD_SECONDS = 25
$RED = 255          # BGR
$GREEN = 65280
$BLUE = 16711680
$MAGENTA = 16711935

$init = @{
    title            = 'Zero Hour Reforged probe'
    description      = 'One-off surface probe'
    author           = @{ name = 'Zero Hour Reforged'; contact = 'local' }
    device_supported = @('keyboard', 'mouse', 'mousepad', 'keypad', 'headset', 'chromalink')
    category         = 'application'
} | ConvertTo-Json -Depth 5 -Compress

$session = Invoke-RestMethod -Uri 'http://localhost:54235/razer/chromasdk' -Method Post -Body $init -ContentType 'application/json'
"session: $($session.uri)"

function Send-Frame($endpoint, $body) {
    try {
        $answer = Invoke-RestMethod -Uri "$($session.uri)/$endpoint" -Method Put -Body $body -ContentType 'application/json'
        "$endpoint -> $($answer | ConvertTo-Json -Compress)"
    }
    catch {
        "$endpoint -> REFUSED: $($_.Exception.Message)"
    }
}

try {
    $rows = @()
    for ($r = 0; $r -lt 6; $r++) {
        $row = @()
        for ($c = 0; $c -lt 22; $c++) {
            $colour = 0
            if ($c -eq 0) { $colour = $RED }
            elseif ($r -eq 0 -and $c -ge 18) { $colour = $GREEN }
            $row += $colour
        }
        $rows += , $row
    }
    Send-Frame 'keyboard' (@{ effect = 'CHROMA_CUSTOM'; param = $rows } | ConvertTo-Json -Depth 5 -Compress)

    Send-Frame 'chromalink' (@{ effect = 'CHROMA_CUSTOM'; param = @($BLUE) * 5 } | ConvertTo-Json -Depth 5 -Compress)

    $keypadRows = @()
    for ($r = 0; $r -lt 4; $r++) { $keypadRows += , (@($MAGENTA) * 5) }
    Send-Frame 'keypad' (@{ effect = 'CHROMA_CUSTOM'; param = $keypadRows } | ConvertTo-Json -Depth 5 -Compress)

    # The session dies after about ten idle seconds, so the frames have to keep going
    # out or the lights drop halfway through the look.
    "holding for $HOLD_SECONDS seconds - look at the hardware now"
    $deadline = (Get-Date).AddSeconds($HOLD_SECONDS)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 2000
        Send-Frame 'keyboard' (@{ effect = 'CHROMA_CUSTOM'; param = $rows } | ConvertTo-Json -Depth 5 -Compress) | Out-Null
        Send-Frame 'chromalink' (@{ effect = 'CHROMA_CUSTOM'; param = @($BLUE) * 5 } | ConvertTo-Json -Depth 5 -Compress) | Out-Null
        Send-Frame 'keypad' (@{ effect = 'CHROMA_CUSTOM'; param = $keypadRows } | ConvertTo-Json -Depth 5 -Compress) | Out-Null
    }
}
finally {
    Invoke-RestMethod -Uri $session.uri -Method Delete | Out-Null
    'session closed'
}
