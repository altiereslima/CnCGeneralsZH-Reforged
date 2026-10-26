# README still of 2026-10-23, the centred command bar in Turkish mid-match on Tournament Desert: the
# local American seat's base round its command center at start0 (1005,584), every structure on a
# spot the spawn guard accepts (the barracks, war factory, strategy center and the first two power
# plants are the README gallery's of 2026-09-25), against a Hard Chinese AI that plays the whole
# match. The army forms up south of the base just before 7:20 and attack-moves north through it at
# 7:20. Nothing is queued: the production list at the left edge shows through the build card's
# translucent background wherever the two overlap, and the card always opens over it.
#
#   generals.exe -win -borderless -xres 1920 -yres 1080 -quickstart -noshellmap -multiInstance
#     -fps 30 -noFPSLimit -particlecap 2500 -language turkish
#     -map "Maps\Tournament Desert\Tournament Desert.map" -autoskirmish 2 -aidiff hard
#     -side 0 FactionAmerica -side 1 FactionChina -seed 7 -peacetime 5 -scenario readme_hud
#     -jumpToFrame 26400 -camera 1205 584 -control 8841
#   with OpenAtMaxZoom = yes, then over the socket: click 960 540 (the barracks), a posted hover on
#   its grid buttons. -fps 120 runs the logic at 240 a second and the build card's production time
#   reads 1 s for the Crusader; -fps 30 is the real speed.
#   A vehicle's build card at the war factory carries the Composite Armor line, and its arrow draws
#   as three wrong letters: a UTF-8 literal in ControlBarPopupDescription.cpp read as code page 1252.

10    spawn      0  AmericaBarracks                 1   1205 584
10    spawn      0  AmericaWarFactory               1   1005 384
10    spawn      0  AmericaStrategyCenter           1   805 584
10    spawn      0  AmericaPowerPlant               2   1300 380  90
10    spawn      0  AmericaPowerPlant               2   700 820   90
10    spawn      0  AmericaSupplyCenter             1   760 290
10    spawn      0  AmericaAirfield                 1   1260 820
10    spawn      0  AmericaPatriotBattery           1   900 980
10    spawn      0  AmericaPatriotBattery           1   1120 980

# the army, south of the base just before the frame is drawn
# (a scenario file counts 30 Hz frames: 12900 is logic frame 25800, 13200 the jump's 26400)
12900 spawn      0  AmericaTankCrusader             6   1150 200  40
12900 spawn      0  AmericaTankPaladin              2   1250 220  45
12900 spawn      0  AmericaVehicleHumvee            3   880 200   40
12900 spawn      0  AmericaInfantryRanger           8   1000 230  16
12900 spawn      0  AmericaInfantryMissileDefender  4   1050 260  16
12900 spawn      0  AmericaVehicleTomahawk          2   1300 260  45
13230 attackmove 0  AmericaTankCrusader                 1150 1600
13230 attackmove 0  AmericaTankPaladin                  1150 1600
13230 attackmove 0  AmericaVehicleHumvee                1150 1600
13230 attackmove 0  AmericaInfantryRanger               1150 1600
13230 attackmove 0  AmericaInfantryMissileDefender      1150 1600
13230 attackmove 0  AmericaVehicleTomahawk              1150 1600
