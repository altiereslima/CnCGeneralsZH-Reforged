# README stills of 2026-10-23, Modern water and Volumetric hazard clouds on Flash Effect: the Toxin
# general's army against America on the derrick beach at 1700,1500, where the west channel, the
# north-east channel and the south channel all reach the sand. Two PT boats run the length of the
# west channel and one comes down the north-east channel from the first second (about 50 units a
# second), so the water carries wakes before the armies meet. The Anthrax Bomb is called on the
# beach at 2 s and its bomber drops at about 19 s; a Scud Storm on the east island launches at
# 28 s and lands at about 39 s; the Toxin Tractors spray their own fields as they fight. Frame
# numbers here count 30 a second, as every scenario file does. A trawler was refused at 2450,2120
# (BRIDGE_IMPASSABLE) and at 1800,950 (cliff), a PT boat at 700,2000 (dry ground).
#
#   generals.exe -win -xres 3840 -yres 2160 -msaa 0 -quickstart -noshellmap -multiInstance -observer
#     -fps 120 -noFPSLimit -particlecap 2500 -map "Maps\Flash Effect\Flash Effect.map"
#     -autoskirmish 2 -takeover -side 0 FactionAmerica -side 1 FactionGLAToxinGeneral -seed 7
#     -scenario readme_water -cinema readme_water -video 60 2460 readme_water -maxframes 2520
#   with WaterStyle = 1, CloudStyle = 1, WaterFoam = yes, TeamGlow = no, Bloom = 2 and
#   OpenAtMaxZoom = yes in the run's Options.ini

10    spawn      0  AmericaCommandCenter            1   start0:240:60
10    spawn      1  Chem_GLACommandCenter           1   start1:-260:60
10    spawn      1  Chem_GLAScudStorm               1   start1:250:-150

# boats, moving from the first second
20    spawn      0  CivilianVehiclePTBoat           2   1000 1990  70
20    spawn      1  CivilianVehiclePTBoat           1   2550 2230
40    move       0  CivilianVehiclePTBoat               1470 1930
40    move       1  CivilianVehiclePTBoat               2030 1830

# the two armies
30    spawn      0  AmericaTankCrusader             10  1180 1460  36
30    spawn      0  AmericaTankPaladin              4   1100 1520  40
30    spawn      0  AmericaInfantryRanger           12  1250 1420  18
30    spawn      0  AmericaVehicleHumvee            3   1150 1580  40
30    spawn      0  AmericaVehicleComanche          3   1080 1400  60
30    spawn      1  Chem_GLAVehicleToxinTruck       6   2200 1460  40
30    spawn      1  Chem_GLATankScorpion            8   2250 1380  40
30    spawn      1  Chem_GLATankMarauder            4   2300 1530  40
30    spawn      1  Chem_GLAInfantryRebel           14  2150 1520  18
30    spawn      1  Chem_GLAVehicleRocketBuggy      4   2330 1420  40
60    attackmove 0  AmericaTankCrusader                 1700 1500
60    attackmove 0  AmericaTankPaladin                  1700 1500
60    attackmove 0  AmericaInfantryRanger               1700 1500
60    attackmove 0  AmericaVehicleHumvee                1700 1500
60    attackmove 0  AmericaVehicleComanche              1700 1500
60    attackmove 1  Chem_GLAVehicleToxinTruck           1700 1500
60    attackmove 1  Chem_GLATankScorpion                1700 1500
60    attackmove 1  Chem_GLATankMarauder                1700 1500
60    attackmove 1  Chem_GLAInfantryRebel               1700 1500
60    attackmove 1  Chem_GLAVehicleRocketBuggy          1700 1500

# the powers
60    power      1  Chem_GLACommandCenter  1620 1490  SuperweaponAnthraxBomb
840   power      1  Chem_GLAScudStorm      1560 1500
