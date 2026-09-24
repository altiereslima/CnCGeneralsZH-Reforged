#!/usr/bin/env python3
from pathlib import Path
import argparse, json

CORE_LOCALE_FILES=["Generals.str","Language.ini"]
MEDIA_LOCALE_FILES=[
    "Movies/EA_LOGO.BIK","Movies/EA_LOGO640.BIK",
    "Movies/sizzle_review.bik","Movies/sizzle_review640.bik",
    "Art/Textures/defeated.dds","Art/Textures/gameover.dds",
    "Art/Textures/GameOver.tga","Art/Textures/victorious.dds",
]

EXPECTED_MAP_TITLES = {'AlpineAssault': 'Assalto Alpino',
 'ArmoredFury': 'Fúria Blindada',
 'AustrianAmbush': 'Operação Market Garden',
 'Badlands': 'Fúria do Deserto',
 'BarrenBadlands': 'Terras Áridas',
 'Bavarian': 'Cruzada Final',
 'BigRivers': 'Raposa da Montanha',
 'BitterWinter': 'Inverno Rigoroso',
 'BoraBora': 'Ilha do Torneio',
 'Breakdown': 'Colapso',
 'BridgeBusters': 'Dragão de Ferro',
 'BulletMassacre': 'Massacre à Bala',
 'CLake': 'Lago do Torneio',
 'CarnageCityLimits': 'Carnificina Urbana',
 'ChemGeneral': 'Dr. Thrax',
 'CitySweep': 'Varredura Urbana',
 'Cityscape': 'Paisagem Urbana',
 'CivilUnrest': 'Distúrbios Civis',
 'CottageCheese': 'Queijo Cottage',
 'CraterCanyon': 'Cânion da Cratera',
 'DarkNight': 'Noite Sombria',
 'DeathValley': 'Vale da Morte',
 'Defcon6': 'DEFCON 6',
 'DesertEagle': 'Águia do Deserto',
 'DestructionStation': 'Estação da Destruição',
 'DogsOfWar': 'Cães de Guerra',
 'DoorToDoor': 'Porta a Porta',
 'DustDevil': 'Redemoinho de Poeira',
 'EasternEverglades': 'Everglades Orientais',
 'ElScorcho': 'El Scorcho',
 'FalloutEffect': 'Efeito Radioativo',
 'FarmersFrenzy': 'Frenesi dos Fazendeiros',
 'Firestorm': 'Tempestade de Fogo',
 'FlakAttack': 'Ataque Antiaéreo',
 'FlankTown': 'Cidade do Flanco',
 'FlashEffect': 'Efeito Relâmpago',
 'FlashFire': 'Incêndio Súbito',
 'Flashbang': 'Granada de Luz e Som',
 'FloodedPlains': 'Planícies Inundadas',
 'Floodplains': 'Planícies Alagáveis',
 'ForgottenForest': 'Floresta Esquecida',
 'FoxHunt': 'Caça à Raposa',
 'FreeFireZone': 'Zona de Fogo Livre',
 'FrozenPlains': 'Planícies Geladas',
 'GhostTown': 'Cidade Fantasma',
 'GreenPastures': 'Pastagens Verdes',
 'Gwall': 'Império Caído',
 'HighRoad': 'Estrada Alta',
 'HighlandHunt': 'Caçada nas Terras Altas',
 'HostileDawn': 'Amanhecer Hostil',
 'InfiniteJustice': 'Comandos do Cairo',
 'InterstateInferno': 'Inferno Interestadual',
 'IslandInTheZone': 'Ilha na Zona',
 'JungleRebellion': 'Rebelião na Selva',
 'KandaharHighlands': 'Chama do Crepúsculo',
 'KashmarKlash': 'Senhores da Guerra do Ermo',
 'KillingFields': 'Campos de Morte',
 'LARiot': 'Motim em Los Angeles',
 'Liberation': 'Libertação',
 'LightsOut': 'Apagão',
 'LongNight': 'Noite Longa',
 'ManicAggression': 'Agressão Maníaca',
 'Marshland': 'Terras Pantanosas',
 'Mirage': 'Miragem',
 'MountainGuns': 'Canhões da Montanha',
 'MountainMayhem': 'Caos nas Montanhas',
 'NoMansLand': 'Terra de Ninguém',
 'NorthAmerica': 'América do Norte',
 'Oasis': 'Oásis Dourado',
 'OverlandOffensive': 'Ofensiva Terrestre',
 'PanicDemise': 'Pânico Fatal',
 'PolarStorm': 'Tempestade Polar',
 'PowerPlay': 'Jogo de Poder',
 'PrecipicePass': 'Fortaleza da Avalanche',
 'RedRock': 'Rocha Vermelha',
 'RiverSnakes': 'Rio Sinuoso',
 'RoadRage': 'Fúria na Estrada',
 'RockyRampage': 'Fúria Rochosa',
 'RogueAgent': 'Agente Renegado',
 'RubbleTown': 'Cidade em Ruínas',
 'SBlast': 'Serpente de Areia',
 'ScorchedEarth': 'Terra Arrasada',
 'SeasideMutiny': 'Motim à Beira-Mar',
 'ShellShocker': 'Impacto de Artilharia',
 'Shellshock': 'Choque de Artilharia',
 'ShockAndAwe': 'Choque e Pavor',
 'Shockwave': 'Onda de Choque',
 'SleepingGiant': 'Montanha Sombria',
 'SleepyHollow': 'Escudo do Interior',
 'SmalltownUSA': 'Aliança da Pátria',
 'SniperDuel': 'Duelo de Atiradores',
 'SnowblindStrike': 'Ataque na Neve Cegante',
 'SteelTrap': 'Armadilha de Aço',
 'SwissMP': 'Águia Solitária',
 'TTourney': 'Tundra do Torneio',
 'TampicoTrauma': 'Rio Silencioso',
 'TheFrontline': 'Linha de Frente',
 'ThinIce': 'Gelo Fino',
 'TournamentA': 'Torneio A',
 'TournamentB': 'Torneio B',
 'TournamentBeach': 'Praia do Torneio',
 'TournamentCanyon': 'Cânion do Torneio',
 'TournamentCity': 'Cidade do Torneio',
 'TournamentContinent': 'Continente do Torneio',
 'TournamentDesolate': 'Ermo do Torneio',
 'TournamentForest': 'Floresta do Torneio',
 'TournamentIceStorm': 'Tempestade de Gelo do Torneio',
 'TournamentLadder01': 'Classificatória do Torneio 01',
 'TournamentLadder02': 'Classificatória do Torneio 02',
 'TournamentLadder03': 'Classificatória do Torneio 03',
 'TournamentLadder04': 'Classificatória do Torneio 04',
 'TournamentPlains': 'Planícies do Torneio',
 'TournamentTactics': 'Táticas de Torneio',
 'TournamentTown': 'Vila do Torneio',
 'TournamentUrban': 'Torneio Urbano',
 'TournamentValley': 'Vale do Torneio',
 'TourneyArena': 'Deserto do Torneio',
 'TwoRoads': 'Duas Estradas',
 'UnholyWar': 'Guerra Profana',
 'Unstable': 'Instável',
 'UrbanUnderground': 'Subterrâneo Urbano',
 'UrbanUprising': 'Revolta Urbana',
 'VictoryOutlook': 'Horizonte da Vitória',
 'VictoryRoad': 'Estrada da Vitória',
 'VictoryValley': 'Vale da Vitória',
 'WastelandWarfare': 'Guerra no Ermo',
 'Whiteout': 'Nevasca Cegante',
 'WoodcrestCircle': 'Cruzado Final',
 'XinjiangBang': 'Lobo do Inverno'}
EXPECTED_MAP_EXTRAS = {'Demo58End1': 'Controle de Batalha Desativado...',
 'Demo58End2': 'COMMAND & CONQUER GENERALS \\n CHEGANDO NO NATAL DE 2002',
 'NVIDIATEXT': 'Command & Conquer Generals \\n Chegando em fevereiro de 2003'}

EXPECTED_REVIEWED_STRINGS = {
 "GUI:PleaseEnterWOLInfo": "Digite as informações",
 "GUI:SaveCamera": "Salvar câmera nos replays",
 "GUI:UseCamera": "Usar câmera salva nos replays",
 "GUI:SandboxMode": "Entrando no modo sandbox...",
 "GUI:GSKickedGameStarted": "Você foi removido porque a partida já começou.",
 "MSG:Testing": "Mary tinha um cordeirinho, com a lã branca como a neve.",
 "MSG:Test2": "E, onde quer que Mary fosse, o cordeirinho certamente a seguia.",
 "Version:BuildMachine": "Máquina de compilação: %ls",
 "Version:BuildUser": "Por %ls",
 "LOAD:China01_2": "- Proteja o desfile militar da China",
 "HELP:CombatCycle-03": "DICA: \\n Coloque um Terrorista em uma Moto de Combate para \\n criar uma Moto-Bomba",
 "HELP:GPSScramble-01": "DICA: \\n Use o Desorientador de GPS dos Poderes dos Generais para \\n camuflar unidades aliadas",
 "CREDITS:SpecialThanx4": "Obrigado a meu filho Colin Moore por me deixar jogar na máquina dele",
 "CONTROLBAR:ToolTipWayPoints": "Clique no solo para definir pontos de passagem."
}

BUILD_REQUIRED=[
    "GeneralsMD/Code/Libraries/Source/Compression/ZLib/adler32.c",
    "GeneralsMD/Code/Libraries/Source/Compression/ZLib/zlib.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Huff.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lz.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibSource/Lzhl.cpp",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_huff.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lz.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lzhl.h",
    "GeneralsMD/Code/Libraries/Source/Compression/LZHCompress/CompLibHeader/lzhl.h",
    "GeneralsMD/Code/Libraries/Source/GameSpy/CMakeLists.txt",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/stub/miles.c",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/stub/miles.def",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/stub/miles.h",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Bink/stub/bink.c",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Bink/stub/bink.def",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Bink/include/bink.h",
    "GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/include/MSS/MSS.h",
]

def need(text,needle,label):
    if needle not in text:
        raise RuntimeError(f"{label}: missing expected source contract")

def labels(path):
    lines=path.read_text(encoding="utf-8-sig").splitlines()
    out=[]; i=0
    while i<len(lines):
        s=lines[i].strip()
        if not s or s.startswith("//"):
            i+=1; continue
        if i+2>=len(lines) or not lines[i+1].strip().startswith('"') or lines[i+2].strip()!="END":
            raise RuntimeError(f"invalid STR block near {s}")
        out.append(s); i+=3
    return out

def validate_prereqs(repo):
    missing=[x for x in BUILD_REQUIRED if not (repo/x).is_file()]
    if missing:
        raise RuntimeError("missing build prerequisites: "+", ".join(missing))
    return {"checked":len(BUILD_REQUIRED),"status":"PASS"}

def parse_str_entries(path):
    lines=path.read_text(encoding="utf-8-sig").splitlines()
    out={}; i=0
    while i < len(lines):
        s=lines[i].strip()
        if not s or s.startswith("//"):
            i+=1; continue
        if i+2>=len(lines) or not lines[i+1].strip().startswith('"') or lines[i+2].strip()!="END":
            raise RuntimeError(f"invalid STR block near {s}")
        out[s]=lines[i+1].strip()[1:-1]
        i+=3
    return out

def validate_map_titles(package):
    entries=parse_str_entries(package/"Data/PortugueseBrazil/Generals.str")
    wrong=[]
    expected={**EXPECTED_MAP_TITLES, **EXPECTED_MAP_EXTRAS}
    for label,value in expected.items():
        key="MAP:"+label
        got=entries.get(key)
        if got!=value:
            wrong.append({"label":key,"expected":value,"got":got})
    if wrong:
        raise RuntimeError("map-title translation audit failed: "+json.dumps(wrong,ensure_ascii=False))
    return {"checked":len(expected),"status":"PASS"}

def validate_reviewed_strings(package):
    entries=parse_str_entries(package/"Data/PortugueseBrazil/Generals.str")
    wrong=[]
    for key,expected in EXPECTED_REVIEWED_STRINGS.items():
        got=entries.get(key)
        if got!=expected:
            wrong.append({"label":key,"expected":expected,"got":got})
    if wrong:
        raise RuntimeError("reviewed-string audit failed: "+json.dumps(wrong,ensure_ascii=False))
    return {"checked":len(EXPECTED_REVIEWED_STRINGS),"status":"PASS"}

def validate_integration(repo,package,allow_missing_media):
    code=repo/"GeneralsMD/Code"
    checks={}
    checks["map_titles"]=validate_map_titles(package)
    checks["reviewed_strings"]=validate_reviewed_strings(package)

    h=(code/"GameEngine/Include/Common/GlobalData.h").read_text(encoding="utf-8")
    need(h,"TEXT_LANGUAGE_PORTUGUESE_BRAZIL","language enum")
    need(h,"AsciiString GetTextLanguageDirectory( void );","language resolver declaration")
    checks["language_enum"]="PASS"

    gd=(code/"GameEngine/Source/Common/GlobalData.cpp").read_text(encoding="utf-8")
    need(gd,'case TEXT_LANGUAGE_PORTUGUESE_BRAZIL: return AsciiString("PortugueseBrazil");',"language resolver")
    need(gd,"return GetRegistryLanguage();","registry fallback")
    checks["language_resolver"]="PASS"

    gt=(code/"GameEngine/Source/GameClient/GameText.cpp").read_text(encoding="utf-8")
    need(gt,'"Data\\\\PortugueseBrazil\\\\Generals.str"',"text overlay")
    checks["text_overlay"]="PASS"

    opt=(code/"GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp").read_text(encoding="utf-8")
    need(opt,"ReforgedPTBRLanguageInitialized","one-time migration")
    need(opt,'(*this)["TextLanguage"] = "2";',"default PT-BR")
    need(opt,"write();","preferences persistence")
    checks["options_migration"]="PASS"

    patch=(code/"Data/Patch.str").read_text(encoding="utf-8")
    need(patch,'GUI:Language2\n"Português (Brasil)"\nEND',"language option")
    need(patch,"localized videos","language tooltip")
    checks["language_ui"]="PASS"

    gl=(code/"GameEngine/Source/GameClient/GlobalLanguage.cpp").read_text(encoding="utf-8")
    need(gl,'#include "Common/GlobalData.h"',"GlobalLanguage include")
    need(gl,"AsciiString languageDir = GetTextLanguageDirectory();","Language.ini selection")
    need(gl,"languageDir = GetRegistryLanguage();","Language.ini fallback")
    checks["language_ini"]="PASS"

    w3d=(code/"GameEngineDevice/Source/W3DDevice/GameClient/W3DFileSystem.cpp").read_text(encoding="utf-8")
    need(w3d,"AsciiString localizedLanguage = GetTextLanguageDirectory();","localized art selection")
    need(w3d,"localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0","localized art fallback")
    checks["localized_art"]="PASS"

    bink=(code/"GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp").read_text(encoding="utf-8")
    need(bink,"AsciiString localizedLanguage = GetTextLanguageDirectory();","localized video selection")
    need(bink,"localizedLanguage.compareNoCase( GetRegistryLanguage().str() ) != 0","localized video fallback")
    checks["localized_video"]="PASS"

    cm=(code/"CMakeLists.txt").read_text(encoding="utf-8")
    need(cm,"Data/PortugueseBrazil","CMake locale copy")
    checks["cmake_locale_copy"]="PASS"

    qe=(code/"GameEngine/Source/GameLogic/Object/Update/ProductionExitUpdate/QueueProductionExitUpdate.cpp").read_text(encoding="utf-8")
    need(qe,"creationObject->isKindOf( KINDOF_FS_BARRACKS )","queue barracks-only recovery scope")
    need(qe,"adjustToPossibleDestination( newObj, ai->getLocomotorSet(), &adjustedCreatePoint )","queue blocked spawn recovery")
    need(qe,"adjustDestination( newObj, ai->getLocomotorSet(), &adjustedExitPoint )","queue blocked exit recovery")
    checks["barracks_exit_recovery_queue"]="PASS"

    de=(code/"GameEngine/Source/GameLogic/Object/Update/ProductionExitUpdate/DefaultProductionExitUpdate.cpp").read_text(encoding="utf-8")
    need(de,"creationObject->isKindOf( KINDOF_FS_BARRACKS )","default barracks-only recovery scope")
    need(de,"adjustDestination( newObj, ai->getLocomotorSet(), &adjustedExitPoint )","default blocked exit recovery")
    checks["barracks_exit_recovery_default"]="PASS"

    loc=code/"Data/PortugueseBrazil"
    missing_core=[x for x in CORE_LOCALE_FILES if not (loc/x).is_file()]
    if missing_core:
        raise RuntimeError("missing PT-BR core files: "+", ".join(missing_core))
    checks["core_locale"]="PASS"

    present=[x for x in MEDIA_LOCALE_FILES if (loc/x).is_file()]
    if present and len(present)!=len(MEDIA_LOCALE_FILES):
        missing=[x for x in MEDIA_LOCALE_FILES if x not in present]
        raise RuntimeError("partial PT-BR media set: "+", ".join(missing))
    if len(present)==len(MEDIA_LOCALE_FILES):
        checks["media"]="PASS"
    elif allow_missing_media:
        checks["media"]="OPTIONAL_NOT_INCLUDED"
    else:
        raise RuntimeError("PT-BR media is absent")

    patch_labels=set(labels(code/"Data/Patch.str"))
    ptbr_labels=set(labels(package/"Data/PortugueseBrazil/Generals.str"))
    missing=sorted(patch_labels-ptbr_labels)
    if missing:
        raise RuntimeError("PT-BR is missing Patch.str labels: "+", ".join(missing))
    checks["patch_label_coverage"]=f"{len(patch_labels)}/{len(patch_labels)}"

    return checks

def main():
    ap=argparse.ArgumentParser(description="Validate PT-BR integration")
    ap.add_argument("--repo",default=".")
    ap.add_argument("--allow-missing-media",action="store_true")
    ap.add_argument("--prereqs-only",action="store_true")
    args=ap.parse_args()
    repo=Path(args.repo).resolve()
    package=Path(__file__).resolve().parents[1]
    out={"status":"PASS"}
    if args.prereqs_only:
        out["build_prerequisites"]=validate_prereqs(repo)
    else:
        out["integration"]=validate_integration(repo,package,args.allow_missing_media)
    print(json.dumps(out,ensure_ascii=False,indent=2))

if __name__=="__main__":
    main()
