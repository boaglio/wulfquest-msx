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
ProjModules = [ ProjName ];
LibModules  = [ "system", "bios", "vdp", "print", "input", "memory" ];

//-- Headers the build generates (tools/gen_palette.py now; the converter from M1)
CompileOpt = `-I${ProjDir}../build/gen`;

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
