#include "kT_Palpost.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;



TableString		g_strs[StrID_NUMTYPES] = {
	StrID_NONE,						"",
	StrID_Name,						"kT Palpost",
	StrID_Description,				"kTools Palleted posterization",
	StrID_BitDepth_Param_Name,		"Bit Depth",
};


char	*GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}
	