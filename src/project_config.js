// Wulf Quest for MSX2: the MSXgl build settings (AGENTS.md §1, §3).
// build.sh runs MSXgl's build tool from this folder, so this folder is the
// project and msxgl/ stays untouched. Everything not set here keeps MSXgl's
// default (msxgl/engine/script/js/default_config.js).

//-- Where the build writes: the repository's build/, never committed
OutDir = `${ProjDir}../build/msxgl/`;

//-- The C compiler and tools pinned with the msxgl submodule (SDCC 4.6.0)
// (MSXgl's defaults, written out so a reader knows which SDCC builds the game)
SDCCPath  = `${ToolsDir}sdcc/`;
Compiler  = `${SDCCPath}bin/sdcc`;
Assembler = `${SDCCPath}bin/sdasz80`;
Linker    = `${SDCCPath}bin/sdcc`;
MakeLib   = `${SDCCPath}bin/sdar`;

ProjName    = "wulfquest";
// data first: it must sit in ROM segment 0 (see data.c; build.sh checks it)
ProjModules = [ "data", ProjName, "room" ];
LibModules  = [ "system", "bios", "vdp", "print", "input", "memory" ];

//-- Headers the converter generates (build/gen/, AGENTS.md §4)
CompileOpt = `-I${ProjDir}../build/gen`;

//-- The converter's blobs, each in the ROM segment it chose (build/gen/rawfiles.json).
// MSXgl also writes a wulfquest_rawdef.h here; the game uses build/gen/rom.h instead.
const GenDir = `${ProjDir}../build/gen/`;
RawFiles = require(`${GenDir}rawfiles.json`).map(r => ({ segment: r.segment, file: GenDir + r.file }));

//-- §1: MSX2 only, on a MegaROM cartridge
Machine  = "2";
Target   = "ROM_ASCII16";
ROMSize  = 256;
CheckVersion    = true;   // an MSX1 gets MSXgl's "A MSX2 is required" instead of a crash
AddROMSignature = true;

AppSignature = true;
AppCompany   = "WQ";
AppID        = "M2";

Verbose   = false;
LogStdout = true;
DoRun     = false;
DoDeploy  = false;   // the ROM stays in build/; no emul/ folder in src/
