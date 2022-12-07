#include "kT_Palpost.h"

#include <algorithm>

static PF_Err 
About (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	
	suites.ANSICallbacksSuite1()->sprintf(	out_data->return_msg,
											"%s v%d.%d\r%s",
											STR(StrID_Name), 
											MAJOR_VERSION, 
											MINOR_VERSION, 
											STR(StrID_Description));
	return PF_Err_NONE;
}

static PF_Err 
GlobalSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	out_data->my_version = PF_VERSION(	MAJOR_VERSION, 
										MINOR_VERSION,
										BUG_VERSION, 
										STAGE_VERSION, 
										BUILD_VERSION);

	out_data->out_flags =  PF_OutFlag_DEEP_COLOR_AWARE;	// just 16bpc, not 32bpc
	
	return PF_Err_NONE;
}

static PF_Err 
ParamsSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err		err		= PF_Err_NONE;
	PF_ParamDef	def;	

	AEFX_CLR_STRUCT(def);

	PF_ADD_SLIDER(	STR(StrID_BitDepth_Param_Name), 
					kT_Palpost_BITDEPTH_MIN, 
					kT_Palpost_BITDEPTH_MAX, 
					kT_Palpost_BITDEPTH_MIN, 
					kT_Palpost_BITDEPTH_MAX, 
					kT_Palpost_BITDEPTH_DFLT,
					BITDEPTH_DISK_ID);
	
	out_data->num_params = PALPOST_NUM_PARAMS;

	return err;
}

static void RGB2YIQ8(const PF_Pixel8 *inP, kT_YIQ_Pixel* outP)
{
	outP->Y =		(	0.299F	*	inP->red	+	0.587F	*	inP->green	+	0.114F	*	inP->blue	)	/	PF_MAX_CHAN8;
	outP->I =		(	0.5959F	*	inP->red	-	0.2746F	*	inP->green	-	0.3213F	*	inP->blue	)	/	PF_MAX_CHAN8;
	outP->Q	=		(	0.2115F	*	inP->red	-	0.5227F	*	inP->green	+	0.3112F	*	inP->blue	)	/	PF_MAX_CHAN8;	
}

static void YIQ2RGB8(const kT_YIQ_Pixel *inP, PF_Pixel8* outP)
{
	outP->red	=	(A_u_char)((	inP->Y		+	0.956F	*	inP->I		+	0.619F	*	inP->Q		)	*	PF_MAX_CHAN8);
	outP->green =	(A_u_char)((	inP->Y		-	0.272F	*	inP->I		-	0.647F	*	inP->Q		)	*	PF_MAX_CHAN8);
	outP->blue	=	(A_u_char)((	inP->Y		-	1.106F	*	inP->I		+	1.703F	*	inP->Q		)	*	PF_MAX_CHAN8);
}

static void MedianCut8(kT_YIQ_Pixel *pixbuf, A_long size, A_u_char cut)
{
	if (cut == 0)
		return;

	// Find greatest range
	PF_FpLong	maxY	= 0;
	PF_FpLong	maxI	= -0.5;
	PF_FpLong	maxQ	= -0.5;
	PF_FpLong	minY	= 1;
	PF_FpLong	minI	= 0.5;
	PF_FpLong	minQ	= 0.5;
	for (A_long i = 0; i < size; ++i)
	{
		maxY = MAX(maxY, pixbuf[i].Y);
		maxI = MAX(maxI, pixbuf[i].I);
		maxQ = MAX(maxQ, pixbuf[i].Q);
		minY = MIN(minY, pixbuf[i].Y);
		minI = MIN(minI, pixbuf[i].I);
		minQ = MIN(minQ, pixbuf[i].Q);
	}
	PF_FpLong	rngY	=	maxY - minY;
	PF_FpLong	rngI	= (	maxI - minI	) / FACTOR_I;
	PF_FpLong	rngQ	= (	maxQ - minQ	) / FACTOR_Q;

	// Sort by greatest range found
	PF_FpLong	maxRng	= MAX(rngY, MAX(rngI, rngQ));
	if		(maxRng == rngY)
	{
		std::sort(pixbuf, pixbuf + size,
			[](const kT_YIQ_Pixel& lhs, const kT_YIQ_Pixel& rhs) {
				return lhs.Y < rhs.Y;
			});
	}
	else if (maxRng == rngI)
	{
		std::sort(pixbuf, pixbuf + size,
			[](const kT_YIQ_Pixel& lhs, const kT_YIQ_Pixel& rhs) {
				return lhs.I < rhs.I;
			});
	}
	else if (maxRng == rngQ)
	{
		std::sort(pixbuf, pixbuf + size,
			[](const kT_YIQ_Pixel& lhs, const kT_YIQ_Pixel& rhs) {
				return lhs.Q < rhs.Q;
			});
	}

	// Recursion
	MedianCut8(pixbuf			, size / 2, cut - 1);
	MedianCut8(pixbuf + size / 2, size / 2, cut - 1);
}

static PF_Err
Buffer8(
	void		*refcon,
	A_long		xL,
	A_long		yL,
	PF_Pixel8	*inP,
	PF_Pixel8	*outP)
{
	PF_Err		err	= PF_Err_NONE;

	PosterizeInfo	*piP	=	reinterpret_cast<PosterizeInfo*>(refcon);
	A_long			bufIdx	=	yL * piP->frameWidth + xL;

	if (piP)
		RGB2YIQ8(inP, &piP->bufYiqP[bufIdx]);

	return err;
}

static PF_Err
Posterize8 (
	void		*refcon, 
	A_long		xL, 
	A_long		yL, 
	PF_Pixel8	*inP, 
	PF_Pixel8	*outP)
{
	PF_Err		err = PF_Err_NONE;
	
	PosterizeInfo	*piP	= reinterpret_cast<PosterizeInfo*>(refcon);
					
	if (piP)
	{
		PF_FpLong	prevDist	= INFINITY;
		PF_FpLong	currDist	= 0;
		A_long		nearestIdx	= 0;

		kT_YIQ_Pixel	yiq;
		RGB2YIQ8(inP, &yiq);

		PF_FpLong	dY	= 0;
		PF_FpLong	dI	= 0;
		PF_FpLong	dQ	= 0;
		for (A_long i = 0; i < (1 << piP->bitDepth); ++i)
		{
			dY	=	piP->pltYiqP[i].Y - yiq.Y;
			dI	= (	piP->pltYiqP[i].I - yiq.I ) / FACTOR_I;
			dQ	= (	piP->pltYiqP[i].Q - yiq.Q ) / FACTOR_Q;
			
			currDist = sqrt(dY * dY + dI * dI + dQ * dQ);

			if (currDist < prevDist)
			{
				prevDist = currDist;
				nearestIdx = i;
			}
		}

		outP->alpha	= inP->alpha;
		YIQ2RGB8(&piP->pltYiqP[nearestIdx], outP);
	}

	return err;
}

static PF_Err 
Render (
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err				err		= PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);

	A_long	linesL	= output->extent_hint.bottom - output->extent_hint.top;
	A_long	colsL	= output->extent_hint.right - output->extent_hint.left;

	PosterizeInfo	piP;
	AEFX_CLR_STRUCT(piP);

	piP.bitDepth	= params[PALPOST_BITDEPTH]->u.sd.value;
	piP.frameWidth	= colsL;

	A_long		sizeL		= linesL * colsL;
	A_u_short	colors		= 1 << piP.bitDepth;
	A_long		blockSize	= sizeL / colors;

	piP.bufYiqP		= new kT_YIQ_Pixel[sizeL];
	piP.pltYiqP		= new kT_YIQ_Pixel[colors];

	if (PF_WORLD_IS_DEEP(output))
	{
		// todo
	}
	else
	{
		// Fill the pixel buffer
		ERR(suites.Iterate8Suite2()->iterate(	in_data,
												output->extent_hint.top,
												output->extent_hint.bottom,
												&params[PALPOST_INPUT]->u.ld,
												NULL,
												(void*)&piP,
												Buffer8,
												output));

		// Proceed median cut
		MedianCut8(piP.bufYiqP, sizeL, piP.bitDepth);

		// Save average colors to temporary palette
		for (A_u_short i = 0; i < colors; ++i)
		{
			PF_FpShort		sumY = 0;
			PF_FpShort		sumI = 0;
			PF_FpShort		sumQ = 0;
			for (A_long j = 0; j < blockSize; ++j)
			{
				sumY	+= piP.bufYiqP[i * blockSize + j].Y;
				sumI	+= piP.bufYiqP[i * blockSize + j].I;
				sumQ	+= piP.bufYiqP[i * blockSize + j].Q;
			}
			piP.pltYiqP[i].Y	= sumY / blockSize;
			piP.pltYiqP[i].I	= sumI / blockSize;
			piP.pltYiqP[i].Q	= sumQ / blockSize;
		}

		// Palletize
		ERR(suites.Iterate8Suite2()->iterate(	in_data,
												output->extent_hint.top,		// progress base
												output->extent_hint.bottom,		// progress final
												&params[PALPOST_INPUT]->u.ld,	// src 
												NULL,							// area - null for all pixels
												(void*)&piP,					// refcon - your custom data pointer
												Posterize8,					// pixel function pointer
												output));
	}

	delete[] piP.pltYiqP;
	delete[] piP.bufYiqP;

	return err;
}


extern "C" DllExport
PF_Err PluginDataEntryFunction(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT(
		inPtr,
		inPluginDataCallBackPtr,
		"kT Palpost", // Name
		"ADBE kT Palpost", // Match Name
		"kTools AE Plug-ins", // Category
		AE_RESERVED_INFO); // Reserved Info

	return result;
}


PF_Err
EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err	err = PF_Err_NONE;
	
	try
	{
		switch (cmd)
		{
		case PF_Cmd_ABOUT:

			err = About(in_data,
						out_data,
						params,
						output);
			break;
			
		case PF_Cmd_GLOBAL_SETUP:

			err = GlobalSetup(	in_data,
								out_data,
								params,
								output);
			break;
			
		case PF_Cmd_PARAMS_SETUP:

			err = ParamsSetup(	in_data,
								out_data,
								params,
								output);
			break;
			
		case PF_Cmd_RENDER:

			err = Render(	in_data,
							out_data,
							params,
							output);
			break;
		}
	}
	catch (PF_Err &thrown_err)
	{
		err = thrown_err;
	}

	return err;
}

