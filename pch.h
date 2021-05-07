// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#define WIN32_LEAN_AND_MEAN						// Exclude rarely-used stuff from Windows headers
#define MATLAB_MEX_FILE

// Windows Header Files:
#include <windows.h>
#include <process.h>
#include <algorithm>

// TODO: reference additional headers your program requires here
#include <mex.h>
#include <mat.h>
#include <math.h>
//#include <PCO_errtext.h>
#include <PCO_err.h>
#include <sc2_SDKStructures.h>
#include <sc2_common.h>
#include <sc2_defs.h>
#include <SC2_CamExport.h>
#include <SC2_SDKAddendum.h>

// MATLAB libraries
#pragma comment(lib, "libmx.lib")
#pragma comment(lib, "libmex.lib")
#pragma comment(lib, "libmat.lib")

// PCO libraries
#pragma comment(lib, "SC2_Cam.lib")
