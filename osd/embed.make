icon_sys_A.c : icons/icon_A.sys
	bin2c $< $@ icon_sys_A

icon_sys_J.c : icons/icon_J.sys
	bin2c $< $@ icon_sys_J

icon_sys_C.c : icons/icon_C.sys
	bin2c $< $@ icon_sys_C

icobysys_icn.c : icons/icobysys.icn
	bin2c $< $@ icobysys_icn

sio2man_irx.c : $(PS2SDK)/iop/irx/sio2man.irx
	bin2c $< $@ sio2man_irx

mcman_irx.c : $(PS2SDK)/iop/irx/mcman.irx
	bin2c $< $@ mcman_irx

mcserv_irx.c : $(PS2SDK)/iop/irx/mcserv.irx
	bin2c $< $@ mcserv_irx

padman_irx.c: $(PS2SDK)/iop/irx/freepad.irx
	bin2c $(PS2SDK)/iop/irx/freepad.irx padman_irx.c padman_irx
	
ioprp.c : psx/ioprp.img
	bin2c $< $@ psx_ioprp