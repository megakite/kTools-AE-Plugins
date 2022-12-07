/*
	kT Palpost.h
*/

#pragma once

#ifndef kT_Palpost_H
#define kT_Palpost_H

typedef unsigned char		u_char;
typedef unsigned short		u_short;
typedef unsigned short		u_int16;
typedef unsigned long		u_long;
typedef short int			int16;
#define PF_TABLE_BITS	12
#define PF_TABLE_SZ_16	4096

#define PF_DEEP_COLOR_AWARE 1	// make sure we get 16bpc pixels; 
								// AE_Effect.h checks for this.

#include "AEConfig.h"

#ifdef AE_OS_WIN
	typedef unsigned short PixelType;
	#include <Windows.h>
#endif

#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

#include "kT_Palpost_Strings.h"

/* Versioning information */

#define	MAJOR_VERSION	1
#define	MINOR_VERSION	0
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1


/* Parameter defaults */

#define	kT_Palpost_BITDEPTH_MIN		1
#define	kT_Palpost_BITDEPTH_MAX		8
#define	kT_Palpost_BITDEPTH_DFLT	3

enum {
	PALPOST_INPUT = 0,
	PALPOST_BITDEPTH,
	PALPOST_NUM_PARAMS
};

enum {
	BITDEPTH_DISK_ID = 1
};

// Fixed-point arithmetic
#define FIXED_MUL(x, y)	((PF_Fixed)((((int64_t)(x)) * (y)) >> 16))
#define FIXED_DIV(x, y)	((PF_Fixed)((((int64_t)(x)) << 16) / (y)))

// Constants used in color space conversion
#define FACTOR_I	(0.5957 * 2)
#define FACTOR_Q	(0.5226 * 2)


typedef struct {
	PF_FpShort Y, I, Q;
} kT_YIQ_Pixel;

typedef struct {
	kT_YIQ_Pixel	*pltYiqP;
	kT_YIQ_Pixel	*bufYiqP;
	A_u_char		bitDepth;
	A_long			frameWidth;
} PosterizeInfo, *PosterizeInfoP, **PosterizeInfoH;


extern "C" {

	DllExport
	PF_Err
	EffectMain(
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra);

}

#endif // kT Palpost_H