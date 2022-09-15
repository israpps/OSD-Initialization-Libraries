#ifndef DEBUG_INCLUDED
#define DEBUG_INCLUDED

#if defined(DEBUG_EESIO)
	#define DPRINTF(x...) sio_printf(x)
#elif defined(DEBUG_PRINTF)
	#define DPRINTF(x...) printf(x)
#elif defined (DEBUG_SCR)
	#define DPRINTF(x...) scr_printf(x)
#else
	#define DPRINTF(x...) do {} while(0)
#endif

#endif
