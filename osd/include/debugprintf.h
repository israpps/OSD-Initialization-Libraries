#ifndef DEBUG_INCLUDED
#define DEBUG_INCLUDED

/* change the printf type to whatever you want.
 * if you use EE_SIO, dont forget to define DEBUG_EESIO so sior.irx is loaded and sio_init() gets called
 */
#ifdef DEBUG_PRINTF
	#define DPRINTF(x...) sio_printf(x)
#else
	#define DPRINTF(x...) do {} while(0)
#endif
/*******************************************/
#endif
