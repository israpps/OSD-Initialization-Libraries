#Enable to write ICOBYSYS (_SCE8 icon), if the console does not belong to US, Europe, Asia or Japan.
#The icon (icobysys.icn) must be supplied.
WRITE_ICOBYSYS ?= 0

#Enable to enable the check against DVD Player v1.00 and v1.01.
#This will prohibit them from being booted.
PROHBIT_DVD_0100 ?= 0

#Enable to enable the newer sceCdReadKey checks, which are only supported by a newer CDVDMAN module.
XCDVD_READKEY ?= 0

#Enable to build the PSX example.
PSX ?= 0

EE_BIN ?= example.elf
EE_BIN_PACKED = SC-$(EE_BIN)
EE_BIN_STRIPPED = S-$(EE_BIN)
IOP_OBJS = sio2man_irx.o mcman_irx.o mcserv_irx.o padman_irx.o
ICONS = icon_sys_A.o icon_sys_J.o icon_sys_C.o
ICONS_TEMP = icon_sys_A.c icon_sys_J.c icon_sys_C.c sio2man_irx.c mcman_irx.c mcserv_irx.c
IOPRP_TEMP = ioprp.c
EE_OBJS = main.o common/elf.o loader_elf.o\
	common/pad.o common/libcdvd_add.o common/OSDConfig.o common/OSDInit.o common/OSDHistory.o common/dvdplayer.o common/ps1.o common/ps2.o common/modelname.o \
	$(ICONS) $(IOP_OBJS)

EE_INCS := -I$(PS2SDK)/ports/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I. -Iinclude -Ipsx
EE_LDFLAGS := -L$(PS2SDK)/ee/lib -nostartfiles -Tlinkfile
EE_LIBS := -lmc -ldebug -lpatches -lc -lkernel -lpadx 
EE_CFLAGS += -mno-gpopt -G0 -Os -Wall

ifeq ($(WRITE_ICOBYSYS),1)
	ICONS += icobysys_icn.o
	ICONS_TEMP += icobysys_icn.c
	EE_CFLAGS += -DWRITE_ICOBYSYS=1
endif

ifeq ($(XCDVD_READKEY),1)
	EE_CFLAGS += -DXCDVD_READKEY=1
endif

ifeq ($(PROHBIT_DVD_0100),1)
	EE_CFLAGS += -DPROHBIT_DVD_0100=1
endif

ifeq ($(PSX),1)
	EE_CFLAGS += -DPSX=1
	EE_OBJS += psx/scmd_add.o ioprp.o
	EE_LIBS += -lxcdvd -liopreboot
else
	EE_LIBS += -lcdvd
endif
all: $(EE_BIN_PACKED)
	@echo done

%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

%.o : %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

loader/loader.elf: loader
	$(MAKE) -C $<

loader_elf.c: loader/loader.elf
	bin2c $< $@ loader_elf

loader_elf.o: loader_elf.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_BIN) : $(EE_OBJS)
	$(EE_CC) -T$(PS2SDK)/ee/startup/linkfile $(EE_LDFLAGS) \
		-o $(EE_BIN) $(EE_OBJS) $(PS2SDK)/ee/startup/crt0.o $(EE_LIBS)

$(EE_BIN_STRIPPED): $(EE_BIN)
	$(EE_STRIP) -o $@ $<

$(EE_BIN_PACKED): $(EE_BIN_STRIPPED)
	ps2-packer -v $< $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(ICONS_TEMP) $(IOPRP_TEMP)
	$(MAKE) -C loader clean

include embed.make
include $(PS2SDK)/Defs.make
