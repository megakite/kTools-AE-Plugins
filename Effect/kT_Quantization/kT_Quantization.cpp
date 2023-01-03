#include "kT_Quantization.h"

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
					kT_Quantization_BITDEPTH_MIN, 
					kT_Quantization_BITDEPTH_MAX, 
					kT_Quantization_BITDEPTH_MIN, 
					kT_Quantization_BITDEPTH_MAX, 
					kT_Quantization_BITDEPTH_DFLT,
					BITDEPTH_DISK_ID);
	
	out_data->num_params = Quantization_NUM_PARAMS;

	return err;
}

static void MedianCut8(PF_YIQ_Pixel *pixbuf, A_long size, A_u_char cut)
{
	if (cut == 0)
		return;

	// Find greatest ranges
	PF_Fixed	maxY = PF_Fixed_MINVAL,
				maxI = PF_Fixed_MINVAL,
				maxQ = PF_Fixed_MINVAL;
	PF_Fixed	minY = PF_Fixed_MAXVAL,
				minI = PF_Fixed_MAXVAL,
				minQ = PF_Fixed_MAXVAL;
	for (A_long i = 0; i < size; ++i)
	{
		maxY = MAX(maxY, pixbuf[i][0]);
		maxI = MAX(maxI, pixbuf[i][1]);
		maxQ = MAX(maxQ, pixbuf[i][2]);

		minY = MIN(minY, pixbuf[i][0]);
		minI = MIN(minI, pixbuf[i][1]);
		minQ = MIN(minQ, pixbuf[i][2]);
	}
	PF_Fixed	rngY	= maxY - minY,
				rngI	= maxI - minI,
				rngQ	= maxQ - minQ;
	
	// Sort by greatest range found
	PF_Fixed	rngMax	= MAX(rngY, MAX(rngI, rngQ));
	if		(rngMax == rngY)	// Human vision is most sensitive towards luminance difference
	{
		std::qsort(pixbuf, size, sizeof(*pixbuf),
			[](void const* lhs, void const* rhs) -> int {
				return (int)((*((PF_YIQ_Pixel const*)lhs))[0] - (*((PF_YIQ_Pixel const*)rhs))[0]);
			});
	}
	else if (rngMax == rngI)	// then orange-blue,
	{
		std::qsort(pixbuf, size, sizeof(*pixbuf),
			[](void const* lhs, void const* rhs) -> int {
				return (int)((*((PF_YIQ_Pixel const*)lhs))[1] - (*((PF_YIQ_Pixel const*)rhs))[1]);
			});
	}
	else if (rngMax == rngQ)	// finally purple-green.
	{
		std::qsort(pixbuf, size, sizeof(*pixbuf),
			[](void const* lhs, void const* rhs) -> int {
				return (int)((*((PF_YIQ_Pixel const*)lhs))[2] - (*((PF_YIQ_Pixel const*)rhs))[2]);
			});
	}

	// Recursion
	MedianCut8(pixbuf,			  size / 2, cut - 1);
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

	if (piP)
	{
		piP->bufP[yL * piP->frameWidth + xL].alpha	=	inP->alpha;
		piP->bufP[yL * piP->frameWidth + xL].red	=	inP->red;
		piP->bufP[yL * piP->frameWidth + xL].green	=	inP->green;
		piP->bufP[yL * piP->frameWidth + xL].blue	=	inP->blue;
	}

	return err;
}

static PF_Err
Debuffer8 (
	void		*refcon, 
	A_long		xL, 
	A_long		yL, 
	PF_Pixel8	*inP, 
	PF_Pixel8	*outP)
{
	PF_Err		err = PF_Err_NONE;
	
	PosterizeInfo	*piP	=	reinterpret_cast<PosterizeInfo*>(refcon);
					
	if (piP)
	{
		outP->alpha =	piP->bufP[yL * piP->frameWidth + xL].alpha;
		outP->red	=	piP->bufP[yL * piP->frameWidth + xL].red;
		outP->green	=	piP->bufP[yL * piP->frameWidth + xL].green;
		outP->blue	=	piP->bufP[yL * piP->frameWidth + xL].blue;
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

	const A_long	linesL	= output->extent_hint.bottom - output->extent_hint.top;
	const A_long	colsL	= output->extent_hint.right - output->extent_hint.left;

	const A_long	bitDepth	= params[Quantization_BITDEPTH]->u.sd.value;

	const A_long	sizeL		= linesL * colsL;
	const A_long	colors		= 1 << bitDepth;
	const A_long	blockSize	= sizeL / colors;

	PosterizeInfo	piP;
	AEFX_CLR_STRUCT(piP);
	piP.bufP		= new PF_Pixel8[sizeL];
	piP.frameWidth	= colsL;

	PF_YIQ_Pixel	*yiqP = new PF_YIQ_Pixel[sizeL];
	PF_YIQ_Pixel	*pltP = new PF_YIQ_Pixel[colors];

	if (PF_WORLD_IS_DEEP(output))
	{
		// TODO
	}
	else
	{
		// Fill the pixel buffer
		ERR(suites.Iterate8Suite2()->iterate(	in_data,
												output->extent_hint.top,
												output->extent_hint.bottom,
												&params[Quantization_INPUT]->u.ld,
												NULL,
												(void*)&piP,
												Buffer8,
												output));

		// Color conversion
		for (A_long i = 0; i < sizeL; ++i)
			PF_RGB_TO_YIQ(&piP.bufP[i], yiqP[i]);

		// Proceed median cut
		MedianCut8(yiqP, sizeL, bitDepth);
		
		// Save average colors to palette
		for (A_long i = 0; i < colors; ++i)
		{
			int64_t	sumY	= 0,
					sumI	= 0,
					sumQ	= 0;
			for (A_long j = 0; j < blockSize; ++j)
			{
				sumY	+= yiqP[i * blockSize + j][0];
				sumI	+= yiqP[i * blockSize + j][1];
				sumQ	+= yiqP[i * blockSize + j][2];
			}
			pltP[i][0]	= (PF_Fixed)(sumY / blockSize);
			pltP[i][1]	= (PF_Fixed)(sumI / blockSize);
			pltP[i][2]	= (PF_Fixed)(sumQ / blockSize);
		}

		// Palletize
		PF_YIQ_Pixel	temp;
		for (A_long i = 0; i < sizeL; ++i)
		{
			PF_FpLong	prevDist	= INFINITY;
			PF_FpLong	currDist	= 0;
			A_long		nearestIdx	= 0;

			PF_RGB_TO_YIQ(&piP.bufP[i], temp);
			PF_Fixed	dY	= 0,
						dI	= 0,
						dQ	= 0;
			for (A_long j = 0; j < colors; ++j)
			{
				dY	= temp[0] - pltP[j][0];
				dI	= temp[1] - pltP[j][1];
				dQ	= temp[2] - pltP[j][2];

				currDist = sqrt(dY * dY + dI * dI + dQ * dQ);

				if (currDist < prevDist)
				{
					prevDist = currDist;
					nearestIdx = j;
				}
			}

			PF_YIQ_TO_RGB(pltP[nearestIdx], &piP.bufP[i]);
		}

		// Output
		ERR(suites.Iterate8Suite2()->iterate(	in_data,
												output->extent_hint.top,		// progress base
												output->extent_hint.bottom,		// progress final
												&params[Quantization_INPUT]->u.ld,	// src 
												NULL,							// area - null for all pixels
												(void*)&piP,					// refcon - your custom data pointer
												Debuffer8,						// pixel function pointer
												output));
	}

	delete[] pltP;
	delete[] yiqP;
	delete[] piP.bufP;

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
		"kT Quantization", // Name
		"ADBE kT Quantization", // Match Name
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

