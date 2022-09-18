#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <kernel.h>
#include <debug.h>
#include <fileio.h>
#include <iopcontrol.h>
#include <tamtypes.h>
#ifdef PSX
#include <iopcontrol_special.h>
#endif
#include <iopheap.h>
#include <libcdvd.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <sifcmd.h>
#include <osd_config.h>

#include <sbv_patches.h>

#include <usbhdfsd-common.h>

#ifdef DEBUG_EESIO
#include <sior.h>
#endif

#include "main.h"
#include "libcdvd_add.h"
#ifdef PSX
#include "plibcdvd_add.h"
#endif
#include "dvdplayer.h"
#include "OSDInit.h"
#include "OSDConfig.h"
#include "OSDHistory.h"
#include "ps1.h"
#include "ps2.h"
#include "modelname.h"
#include "pad.h"
#include "cnfman.h"
#include "debugprintf.h"

#define IMPORT_BIN2C(_n) extern unsigned char _n[]; extern unsigned int size_ ## _n;
IMPORT_BIN2C(sio2man_irx)
IMPORT_BIN2C(mcman_irx)
IMPORT_BIN2C(mcserv_irx)
IMPORT_BIN2C(padman_irx)
IMPORT_BIN2C(usbd_irx)
IMPORT_BIN2C(usb_mass_irx)
#ifdef PSX
IMPORT_BIN2C(psx_ioprp)
#endif
#ifdef DEBUG_EESIO
IMPORT_BIN2C(sior_irx)
#endif


#define MAX_LEN 64
#define CNF_LEN_MAX 20480 // 20kb should be enough for massive CNF's
#define DELAY 3000

typedef struct
{
	int SKIPLOGO;
    char *KEYPATHS[17][3];
}CONFIG;

CONFIG GLOBCFG;

const char* KEYS_ID[17] = {
    "AUTO",
    "SELECT",    // 0x0001
    "L3",        // 0x0002
    "R3",        // 0x0004
    "START",     // 0x0008
    "UP",        // 0x0010
    "RIGHT",     // 0x0020
    "DOWN",      // 0x0040
    "LEFT",      // 0x0080
    "L2",        // 0x0100
    "R2",        // 0x0200
    "L1",        // 0x0400
    "R1",        // 0x0800
    "TRIANGLE",  // 0x1000
    "CIRCLE",    // 0x2000
    "CROSS",     // 0x4000
    "SQUARE"     // 0x8000
};

void RunLoaderElf(char *filename);
void TimerInit(void);
u64 Timer(void);
void TimerEnd(void);
void delay(int delay);

void CleanUp(void)
{ // This is called from DVDPlayerBoot(). Deinitialize all RPCs here.
    sceCdInit(SCECdEXIT);
}

void SetDefaultSettings(void)
{
    int i, j;
	for (i = 0; i < 17; i++)
		for (j = 0; j < 3; j++)
			GLOBCFG.KEYPATHS[i][j] = NULL;
    GLOBCFG.SKIPLOGO = 1;
}

static void AlarmCallback(s32 alarm_id, u16 time, void *common)
{
    iWakeupThread((int)common);
}

#ifdef PSX
/**
 * Performs PSX-specific initialization.
 */
static void InitPSX()
{
    int result, stat;

    SifInitRpc(0);
    sceCdInit(SCECdINoD);

    // No need to perform boot certification because rom0:OSDSYS does it.
    while (sceCdChgSys(2) != 2) {}; // Switch the drive into PS2 mode.

    // Signal the start of a game, so that the user can use the "quit" game button.
    do
    {
        result = sceCdNoticeGameStart(1, &stat);
    } while ((result == 0) || (stat & 0x80));

    // Reset the IOP again to get the standard PS2 default modules.
    while (!SifIopReset("", 0)) {};

    /*    Set the EE kernel into 32MB mode. Let's do this, while the IOP is being reboot.
        The memory will be limited with the TLB. The remap can be triggered by calling the _InitTLB syscall
        or with ExecPS2().
        WARNING! If the stack pointer resides above the 32MB offset at the point of remap, a TLB exception will occur.
        This example has the stack pointer configured to be within the 32MB limit. */
    SetMemoryMode(1);
    _InitTLB();

    while (!SifIopSync()) {};
}
#endif

int dischandler();

static int file_exists(char *filepath)
{
	int fdn;

	fdn = open(filepath, O_RDONLY);
	if (fdn < 0)
		return 0;

	close(fdn);

	return 1;
}

/*check if path has a special command or token (like 'mc?:/) to handle
 */
char* CheckPath(char* path)
{
    if (!strncmp("mc?", path, 3))
    {
        path[2] = '0';
        if (file_exists(path))
            return path;
        else
        {
            path[2] = '1';
            if (file_exists(path))
                return path;
        }
    }
    if (!strcmp("LOAD_DVD", path))
        dischandler();
    if (!strcmp("LOAD_DVD_NO_PS2LOGO", path))
    {
        GLOBCFG.SKIPLOGO = 1;
        dischandler();
    }
    return path;
}

enum {SOURCE_INVALID = 0, SOURCE_MC0, SOURCE_MC1, SOURCE_MASS} CONFIG_SOURCES_ID;

enum { WLEAPPmc0=0, WLEAPPmc1, DEV1mc0, DEV1mc1, INFMANmc0, INFMANmc1, OPLAPPmc0, OPLAPPmc1, DEFPATH_CNT} DEFPATH_ENUM;
char* DEFPATH[] = {
    "mc?:/BOOT/ULE.ELF",  // AUTO [0]
    "mc?:/BOOT/BOOT2.ELF",
    "mc?:/APPS/ULE.ELF",
    "mass:/BOOT/BOOT4.ELF",  // L2 [3]
    "mc?:/BOOT/BOOT4.ELF",
    "mc?:/B?DATA-SYSTEM/BOOT4.ELF",
    "mass:/BOOT/BOOT2.ELF",  // R2 [6]
    "mc?:/BOOT/BOOT2.ELF",
    "mc?:/B?DATA-SYSTEM/BOOT2.ELF",
    "mass:/BOOT/OPNPS2LD.ELF",  // L1 [9]
    "mc?:/BOOT/OPNPS2LD.ELF",
    "mc?:/APPS/OPNPS2LD.ELF",
    "mass:/RESCUE.ELF",  // R1 [12]
    "mc?:/BOOT/BOOT1.ELF",
    "mc?:/B?DATA-SYSTEM/BOOT1.ELF",
};
enum {DISCLOAD = 0} COMMANDS_ENUM;
char* COMMANDS[] = {
    "$DISCLOAD",
};
char* EXECPATHS[3];

int main(int argc, char *argv[])
{
    int result, is_PCMCIA, ret, button, x=0, j=0, config_source = SOURCE_INVALID, cnf_size = 0, padval = 0;
	unsigned long int bios_version;
    u32 stat;
    int fd;
    char romver[16], RomName[4], ROMVersionNumStr[5];
    unsigned char *RAM_p, *CNFBUFF, *name, *value;
    static int num_buttons = 4,
                pad_button = 0x0100;  // first pad button is L2
    // Initialize SIFCMD & SIFRPC
    SifInitRpc(0);

    // Reboot IOP
#ifndef PSX
    while (!SifIopReset("", 0))
    {
    };
#else
    /* We need some of the PSX's CDVDMAN facilities, but we do not want to use its (too-)new FILEIO module.
       This special IOPRP image contains a IOPBTCONF list that lists PCDVDMAN instead of CDVDMAN.
       PCDVDMAN is the board-specific CDVDMAN module on all PSX, which can be used to switch the CD/DVD drive operating mode.
       Usually, I would discourage people from using board-specific modules, but I do not have a proper replacement for this. */
    while (!SifIopRebootBuffer(psx_ioprp, size_psx_ioprp)) {};
#endif
    while (!SifIopSync())
    {
    };

#ifdef PSX
    InitPSX();
#endif

    // Initialize SIFCMD & SIFRPC
    SifInitRpc(0);

    // Initialize SIF services for loading modules and files.
    SifInitIopHeap();
    SifLoadFileInit();
    fioInit();

    // The old IOP kernel has no support for LoadModuleBuffer. Apply the patch to enable it.
    sbv_patch_enable_lmb();

    /*  The MODLOAD module has a black/white (depends on version) list that determines what devices can have unprotected EE/IOP executables loaded from.
        Typically, only protected executables can be loaded from user-writable media like the memory card or HDD unit.
        This patch will disable the black/white list, allowing executables to be freely loaded from any device.
    */
    sbv_patch_disable_prefix_check();

    /*  Load SDK modules to avoid different behavior across different models*/
    SifExecModuleBuffer(sio2man_irx, size_sio2man_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, NULL);
    SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, NULL);
    SifExecModuleBuffer(padman_irx, size_padman_irx, 0, NULL, NULL);

#ifdef DEBUG_EESIO
	int id;
	// I call this just after SIO2MAN have been loaded
	sio_init(38400, 0, 0, 0, 0);
	DPRINTF("Hello from EE SIO!\n");

	SIOR_Init(0x20);

	id = SifExecModuleBuffer(sior_irx, size_sior_irx, 0, NULL, &ret);
	DPRINTF("\tsior id=%d _start ret=%d\n", id, ret);
#endif

    DPRINTF("mcInit\n");
    mcInit(MC_TYPE_XMC);
    DPRINTF("PadInitPads\n");
    PadInitPads();

    DPRINTF("Loading rom0:ADDDRV\n");// Load ADDDRV. The OSD has it listed in rom0:OSDCNF/IOPBTCONF, but it is otherwise not loaded automatically.
    SifLoadModule("rom0:ADDDRV", 0, NULL);

    // Initialize libcdvd & supplement functions (which are not part of the ancient libcdvd library we use).
    DPRINTF("initializing libcdvd & supplements\n");
	sceCdInit(SCECdINoD);
    cdInitAdd();
    DPRINTF("Loading USBD.IRX\n");
    SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, NULL);
    DPRINTF("Loading USBHDFSD.IRX\n");
    SifExecModuleBuffer(usb_mass_irx, size_usb_mass_irx, 0, NULL, NULL);
    delay(3);

    // Initialize system paths.
    OSDInitSystemPaths();

    padval = ReadCombinedPadStatus();
    if (padval & (PAD_START|PAD_R1))
    while (1) {if (file_exists("mass:/RESCUE.ELF")) RunLoaderElf("mass:/RESCUE.ELF");}

    if ((fd = open("rom0:ROMVER", O_RDONLY)) >= 0)
    {
        read(fd, romver, sizeof(romver));
        close(fd);
    }

#ifndef PSX
    /*  Perform boot certification to enable the CD/DVD drive.
        This is not required for the PSX, as its OSDSYS will do it before booting the update. */
        // e.g. 0160HC = 1,60,'H','C'
        RomName[0] = (romver[0] - '0') * 10 + (romver[1] - '0');
        RomName[1] = (romver[2] - '0') * 10 + (romver[3] - '0');
        RomName[2] = romver[4];
        RomName[3] = romver[5];

        ret = sceCdBootCertify(RomName);// Do not check for success/failure. Early consoles do not support (and do not require) boot-certification.
        DPRINTF("CDVD Drive boot certification performed (%d)\n", ret);

    // This disables DVD Video Disc playback. This functionality is restored by loading a DVD Player KELF.
    /*    Hmm. What should the check for stat be? In v1.xx, it seems to be a check against 0x08. In v2.20, it checks against 0x80.
          The HDD Browser does not call this function, but I guess it would check against 0x08. */
    /*  do
     {
         sceCdForbidDVDP(&stat);
     } while (stat & 0x08); */
#endif

	strncpy(ROMVersionNumStr, romver, 4);
	ROMVersionNumStr[4] = '\0';
	bios_version = strtoul(ROMVersionNumStr, NULL, 16);
    is_PCMCIA = ((bios_version <= 0x120) && (romver[4] == 'J'));
#if 0
    is_PROTOKERNEL = ((bios_version <= 0x101) && (romver[4] == 'J'));
    is_DEX = (romver[5] == 'D');
#endif
	DPRINTF("ROM Version=%x, region=%c, Machine type=%c", bios_version, romver[4], romver[5]);
    // Apply kernel updates for applicable kernels.
    InitOsd();
    // Initialize ROM version (must be done first).
    OSDInitROMVER();
    // Initialize model name
    ModelNameInit();
    // Initialize PlayStation Driver (PS1DRV)
    PS1DRVInit();
    // Initialize ROM DVD player.
    // It is normal for this to fail on consoles that have no DVD ROM chip (i.e. DEX or the SCPH-10000/SCPH-15000).
    DVDPlayerInit();

    // Load OSD configuration
    if (OSDConfigLoad() != 0)
    { // OSD configuration not initialized. Defaults loaded.
        DPRINTF("OSD Configuration not initialized. Defaults loaded.\n");
    }

    // Applies OSD configuration (saves settings into the EE kernel)
    OSDConfigApply();

    /*  Try to enable the remote control, if it is enabled.
        Indicate no hardware support for it, if it cannot be enabled. */
    do
    {
        result = sceCdRcBypassCtl(OSDConfigGetRcGameFunction() ^ 1, &stat);
        if (stat & 0x100)
        { // Not supported by the PlayStation 2.
            // Note: it does not seem like the browser updates the NVRAM here to change this status.
            OSDConfigSetRcEnabled(0);
            OSDConfigSetRcSupported(0);
            break;
        }
    } while ((stat & 0x80) || (result == 0));

    // Remember to set the video output option (RGB or Y Cb/Pb Cr/Pr) accordingly, before SetGsCrt() is called.
    SetGsVParam(OSDConfigGetVideoOutput() == VIDEO_OUTPUT_RGB ? 0 : 1);

    init_scr();
    DPRINTF(	"SIDIF Mode:\t%u\n"
				"Screen type:\t%u\n"
				"Video mode:\t%u\n"
				"Language:\t%u\n"
				"PS1DRV config:\t0x%02x\n"
				"Timezone offset:\t%u\n"
				"Daylight savings:\t%u\n"
				"Time format:\t%u\n"
				"Date format:\t%u\n",
				OSDConfigGetSPDIF(),
				OSDConfigGetScreenType(),
				OSDConfigGetVideoOutput(),
				OSDConfigGetLanguage(),
				OSDConfigGetPSConfig(),
				OSDConfigGetTimezoneOffset(),
				OSDConfigGetDaylightSaving(),
				OSDConfigGetTimeFormat(),
				OSDConfigGetDateFormat());

    /*    If required, make any changes with the getter/setter functions in OSDConfig.h, before calling OSDConfigSave(1).
    Example: */
/*     OSDConfigSetScreenType(TV_SCREEN_169);
    OSDConfigSave(0);
    OSDConfigApply(); */

    scr_printf("\nModel:\t\t%s\n"
               "PlayStation Driver:\t%s\n"
               //"DVD Player:\t%s\n"
               ,
               ModelNameGet(),
               PS1DRVGetVersion()
               //,DVDPlayerGetVersion()
               );

    SetDefaultSettings();
    FILE* fp;
    fp = fopen("mc0:/PS2RB/LAUNCHER.CNF", "r");
    if (fp == NULL) {
		DPRINTF("Cant load config from mc0\n");
        fp = fopen("mc1:/PS2RB/LAUNCHER.CNF", "r");
        if (fp == NULL) {
			DPRINTF("Cant load config from mc1\n");
            config_source = SOURCE_INVALID;
        } else {config_source = SOURCE_MC1;}
    } else {config_source = SOURCE_MC0;}

    if (config_source != SOURCE_INVALID)
    {
        DPRINTF("Config not invalid, reading now\n");
        pad_button = 0x0001;
        num_buttons = 16;
        DPRINTF("Check CNF size\n");
        fseek(fp, 0, SEEK_END);
        cnf_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        DPRINTF("Allocating %d bytes for RAM_p\n", cnf_size);
        RAM_p = (char *)malloc(cnf_size + 1);
        CNFBUFF = RAM_p;
        DPRINTF("Read data INTO the buffer\n");
        fread(RAM_p, cnf_size, 1, fp);
        DPRINTF("Reading finished... Closing fp*\n");
        fclose(fp);
        DPRINTF("NULL Terminate buffer\n");
        CNFBUFF[cnf_size] = '\0';
        int var_cnt = 0;
        char TMP[64];
        for (var_cnt = 0; get_CNF_string(&CNFBUFF, &name, &value); var_cnt++) {
            DPRINTF("reading entry %d", var_cnt);
            if (!strcmp("SKIP_PS2LOGO", name))
                GLOBCFG.SKIPLOGO = atoi(value);

	    	for (x = 0; x < 17; x++) {
	    		for (j = 0; j < 3; j++) {
	    			sprintf(TMP, "LK_%s_E%d", KEYS_ID[x], j + 1);
	    			if (!strcmp(name, TMP)) {
	    				GLOBCFG.KEYPATHS[x][j] = value;
	    				break;
	    			}
	    		}
	    	}

        }
        free(RAM_p);
    } 
    else
    {
        DPRINTF("Invalid config, loading hardcoded shit\n");
		for (x = 0; x < 5; x++)
			for (j = 0; j < 3; j++)
				GLOBCFG.KEYPATHS[x][j] = DEFPATH[3 * x + j];
    }
	if (RAM_p != NULL)
		free(RAM_p);

    scr_printf("PadRead...\n");
    u64 tstart;
    TimerInit();
	tstart = Timer();

	//Stores last key during DELAY msec
	while (Timer() <= (tstart + DELAY))
	{
		//If key was detected
	    padval = ReadCombinedPadStatus();
		button = pad_button;
		for (x = 0; x < num_buttons; x++) {  // check all pad buttons
			if (padval & button) {
				// if button detected , copy path to corresponding index
				for (j = 0; j < 3; j++)
					EXECPATHS[j] = GLOBCFG.KEYPATHS[x + 1][j];
                for (j = 0; j < 3; j++)
                {
				    EXECPATHS[j] = CheckPath(EXECPATHS[j]);
                    if (file_exists(EXECPATHS[j]))
                    {
                        if (!is_PCMCIA)
                            PadDeinitPads();
                        RunLoaderElf(EXECPATHS[j]);
                    }
                }
                break;
			}
			button = button << 1;  // sll of 1 cleared bit to move to next pad button
		}
	}
	TimerEnd();

    if (!is_PCMCIA)
        PadDeinitPads();
    for (j = 0; j < 3; j++) //no keys pressed or something happened! copy AUTO keys and try
	EXECPATHS[j] = GLOBCFG.KEYPATHS[0][j];
    for (j = 0; j < 3; j++)
    {
	    EXECPATHS[j] = CheckPath(EXECPATHS[j]);
        if (file_exists(EXECPATHS[j]))
        {
            if (!is_PCMCIA)
                PadDeinitPads();
            RunLoaderElf(EXECPATHS[j]);
        }
    }

	DPRINTF("END OF CONTROL REACHED!\n");
    return 0;
}

int dischandler()
{
    int OldDiscType, DiscType, ValidDiscInserted, result;
    u32 stat;
    scr_printf("Enabling Diagnosis...\n");
    do
    { // 0 = enable, 1 = disable.
        result = sceCdAutoAdjustCtrl(0, &stat);
    } while ((stat & 0x08) || (result == 0));

    // For this demo, wait for a valid disc to be inserted.
    scr_printf("Waiting for disc to be inserted...\n");

    ValidDiscInserted = 0;
    OldDiscType       = -1;
    while (!ValidDiscInserted)
    {
        DiscType = sceCdGetDiskType();
        if (DiscType != OldDiscType)
        {
            scr_printf("New Disc:\t");
            OldDiscType = DiscType;

            switch (DiscType)
            {
                case SCECdNODISC:
                    scr_printf("No Disc\n");
                    break;

                case SCECdDETCT:
                case SCECdDETCTCD:
                case SCECdDETCTDVDS:
                case SCECdDETCTDVDD:
                    scr_printf("Reading Disc...\n");
                    break;

                case SCECdPSCD:
                case SCECdPSCDDA:
                    scr_printf("PlayStation\n");
                    ValidDiscInserted = 1;
                    break;

                case SCECdPS2CD:
                case SCECdPS2CDDA:
                case SCECdPS2DVD:
                    scr_printf("PlayStation 2\n");
                    ValidDiscInserted = 1;
                    break;

                case SCECdCDDA:
                    scr_printf("Audio Disc (not supported by this program)\n");
                    break;

                case SCECdDVDV:
                    scr_printf("DVD Video\n");
                    ValidDiscInserted = 1;
                    break;
                default:
                    scr_printf("Unknown\n");
            }
        }

        // Avoid spamming the IOP with sceCdGetDiskType(), or there may be a deadlock.
        // The NTSC/PAL H-sync is approximately 16kHz. Hence approximately 16 ticks will pass every millisecond.
        SetAlarm(1000 * 16, &AlarmCallback, (void *)GetThreadId());
        SleepThread();
    }

    // Now that a valid disc is inserted, do something.
    // CleanUp() will be called, to deinitialize RPCs. SIFRPC will be deinitialized by the respective disc-handlers.
    switch (DiscType)
    {
        case SCECdPSCD:
        case SCECdPSCDDA:
            // Boot PlayStation disc
            PS1DRVBoot();
            break;

        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
            // Boot PlayStation 2 disc
            PS2DiscBoot(GLOBCFG.SKIPLOGO);
            break;

        case SCECdDVDV:
            /*  If the user chose to disable the DVD Player progressive scan setting,
                it is disabled here because Sony probably wanted the setting to only bind if the user played a DVD.
                The original did the updating of the EEPROM in the background, but I want to keep this demo simple.
                The browser only allowed this setting to be disabled, by only showing the menu option for it if it was enabled by the DVD Player. */
            /* OSDConfigSetDVDPProgressive(0);
            OSDConfigApply(); */

            /*  Boot DVD Player. If one is stored on the memory card and is newer, it is booted instead of the one from ROM.
                Play history is automatically updated. */
            DVDPlayerBoot();
            break;
    }
    return 0;
}
