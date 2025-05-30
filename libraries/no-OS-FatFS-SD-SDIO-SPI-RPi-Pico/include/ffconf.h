/*---------------------------------------------------------------------------/
/  Configurations of FatFs Module
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286	/* Revision ID */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY	0 /* Was 1. Set to 0 for Read/Write, enables f_setbuf dependency */
/* This option switches read-only configuration. (0:Read/Write or 1:Read-only)
/  Read-only configuration removes writing API functions, f_write(), f_sync(),
/  f_unlink(), f_mkdir(), f_chmod(), f_rename(), f_truncate(), f_getfree()
/  and optional writing functions as well. */


#define FF_FS_MINIMIZE	0 /* Was 1. Set to 0 for full features, ensuring f_setbuf */
/* This option defines minimization level to remove some basic API functions.
/
/   0: Basic functions are fully enabled.
/----------------------------------------------------------------------------*/

#define FF_USE_STRFUNC    0   /* 0:Disable or 1-2:Enable */ 