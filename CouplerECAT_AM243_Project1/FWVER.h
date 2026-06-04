#ifndef FW_VER_H
#define FW_VER_H

#ifdef MASTER_TYPE
#undef MASTER_TYPE
#endif
#ifdef MASTER_FW_VERSION
#undef MASTER_FW_VERSION
#endif
	
#define MASTER_TYPE	        "GS-Coupler-EtherCAT"
#define MASTER_FW_VERSION	"Alpha-1.0.0-090526"

#endif