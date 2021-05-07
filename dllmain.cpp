// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "pcocam.h"

#define VALUE	plhs[0]

/*	Driver mutually exclusive access lock (SEMAPHORE).

    The semaphore is granted by the mexEnter() function. The owner of the
    semaphore must release it with mexLeave().
*/
static HANDLE driver;


/*	Handle of the DLL module itself.
*/
static HANDLE self;


// HINSTANCE SC2Lib = NULL;


/*	Library initialization and termination.

    This function creates a semaphore when a process is attaching to the
    library.
*/
MWMEX_EXPORT_SYM
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
	UNREFERENCED_PARAMETER(lpReserved);
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        self = hModule;
        driver = CreateSemaphore(NULL, 1, 1, NULL);
        return driver != NULL;
    case DLL_THREAD_ATTACH:
        self = hModule;
        break;
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

/*	Request exclusive access.

	This function uses a semaphore for granting mutually exclusive access to a
	code section between mexEnter()/mexLeave(). If the semaphore is locked, the
	function waits up to 10ms before giving up.

	The semaphore is created once upon initialization of the library and persists
	until the library is unloaded. Code that may raise an exception or lead to a
	MEX abort on error must not execute within a mexEnter()/mexLeave section,
	otherwise the access may lock permanently.
*/
#ifndef NDEBUG
void mexEnter(const char* file, const int line)
#else
void mexEnter(void)
#endif
{
	switch (WaitForSingleObject(driver, 10))
	{
	case WAIT_ABANDONED:
	case WAIT_OBJECT_0:					// access granted
		return;
	default:
#ifndef NDEBUG
		mexPrintf("%s:%d - ", file, line);
#endif
		mexErrMsgTxt("Locked.");
	}
}


/*	Release exclusive access.
*/
void mexLeave(void)
{
	ReleaseSemaphore(driver, 1, NULL);
}


/*	Check for an error and print the message.

	This function is save within a mexEnter()/mexLeave section.
*/
#ifndef NDEBUG
void mexMessage(const char* file, const int line, unsigned error)
#else
void mexMessage(unsigned error)
#endif
{
	if (error != 0)
	{
		MEXLEAVE;
#ifndef NDEBUG
		mexPrintf("%s:%d ", file, line);
#endif
		char errbuffer[400];
		PCO_GetErrorTextSDK(error, errbuffer, 1000);
		mexErrMsgTxt(errbuffer);
	}
}


/*  Allocate a number of memory buffers to store frames
*/
void createBuffers(void)
{
	HANDLE hCamera = cameraStates[camera - 1].handle;

	if (_strnicmp(cameraStates[camera - 1].szCameraName, "pco.edge", 8) == 0)
	{

		WORD wIdentifier = 0;
		if (cameraStates[camera - 1].xsize > 1920)
		{
			if ((cameraStates[camera - 1].strCamType.wCamType == CAMERATYPE_PCO_EDGE) && (cameraStates[camera - 1].dwPixelRate == cameraStates[camera - 1].caminfo.dwPixelRateDESC[1]))
			{
				cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x12L | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
				wIdentifier = 0x1612;
			}
			else
			{
				cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x16 | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
			}
		}
		else
		{
			cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x16 | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
		}
		MEXMESSAGE(PCO_SetTransferParameter(hCamera, &cameraStates[camera - 1].TransferParam, sizeof(cameraStates[camera - 1].TransferParam)));

		if (cameraStates[camera - 1].strCamType.wCamType != CAMERATYPE_PCO_EDGE_HS)
		{
			// Edge 4.2 CLHS does not support this
			WORD wParameter = 0;
			MEXMESSAGE(PCO_SetActiveLookupTable(hCamera, &wIdentifier, &wParameter));
		}

		MEXMESSAGE(PCO_ArmCamera(hCamera));

		MEXMESSAGE(PCO_CamLinkSetImageParameters(hCamera, cameraStates[camera - 1].xsize, cameraStates[camera - 1].ysize));
	}

	for (SHORT i = 0; i < MaxNumberOfBuffers; i++)
	{
		DWORD BufferSizeBytes = cameraStates[camera - 1].ImageSizeBytes;

		//		SHORT sBufNr = i;
		//	    WORD* acqBuffer = static_cast<WORD*>(mxMalloc(BufferSizeBytes));
		//		mexMakeMemoryPersistent(acqBuffer);

		SHORT sBufNr = -1;	// -1 produces a new buffer
		WORD* acqBuffer = NULL;
		HANDLE bufferEvent = NULL;

		MEXMESSAGE(PCO_AllocateBuffer(hCamera, &sBufNr, BufferSizeBytes, &acqBuffer, &bufferEvent));
		cameraStates[camera - 1].AcqBuffers[i] = acqBuffer;
		cameraStates[camera - 1].BufferEvent[i] = bufferEvent;
	}
}


/*  Delete previously allocated buffers
*/
void deleteBuffers(void)
{
	WORD wRecState = 0;
	MEXMESSAGE(PCO_GetRecordingState(cameraStates[camera - 1].handle, &wRecState));
	if (wRecState == 1)
	{
		MEXMESSAGE(PCO_SetRecordingState(cameraStates[camera - 1].handle, 0));
	}
	MEXMESSAGE(PCO_CancelImages(cameraStates[camera - 1].handle));
	for (SHORT i = 0; i < MaxNumberOfBuffers; i++)
	{
		MEXMESSAGE(PCO_FreeBuffer(cameraStates[camera - 1].handle, i));
		cameraStates[camera - 1].AcqBuffers[i] = NULL;
	}
}


/*	Free the cameras and the driver.
*/
void mexCleanup(void)
{
	if (cameras >= 0)
	{
		MEXENTER;
		while (cameras)
		{
			camera = cameras;
			// Free the allocated buffer
			deleteBuffers();
			MEXMESSAGE(PCO_CloseCamera(cameraStates[camera - 1].handle));
			mxFree(cameraStates[camera - 1].ImageBuffer);
			mxFree(cameraStates[camera - 1].AcqBuffers);
			mxFree(cameraStates[camera - 1].BufferEvent);
			mxFree(cameraStates[camera - 1].szCameraName);
			cameras--;
		}
		MEXLEAVE;
#ifndef NDEBUG
		mexPrintf("PCO system shutted down!\n");
#endif
		if (cameraStates)
		{
			mxFree(cameraStates);
			cameraStates = NULL;
		}
	}
}


/*	Initialize PCO driver and get cameras.
*/
int mexStartup(void)
{
	mexAtExit(mexCleanup);
	if (cameras < 0)
	{
		cameras = 0;
		DEBUG("PCO camera driver initialized");
	}
	if (cameras == 0)
	{
		MEXENTER;

		HANDLE hCamera = NULL;

		//PCO_OpenStruct strOpen;
		//strOpen.wSize = sizeof(PCO_OpenStruct);
		//strOpen.wInterfaceType = PCO_INTERFACE_CL_ME4;
		//strOpen.wCameraNumber = 0;
		//strOpen.wOpenFlags[0] = PCO_OPENFLAG_GENERIC_IS_CAMLINK;
		//MEXMESSAGE(PCO_OpenCameraEx(&hCamera, &strOpen));

		MEXMESSAGE(PCO_OpenCamera(&hCamera, cameras));

		cameras += 1;

		//		MEXMESSAGE(AT_GetInt(AT_HANDLE_SYSTEM, L"Device Count", &cameras));
#ifndef NDEBUG
		mexPrintf("%d camera%s available.\n", static_cast<int>(cameras), (cameras == 1) ? "" : "s");
#endif
		if (cameras > 0)
		{
			SHORT n = 0;
			if (cameraStates)
			{
				mxFree(cameraStates);
				cameraStates = NULL;
			}
			mexMakeMemoryPersistent(cameraStates = static_cast<cameraState*>(mxCalloc(static_cast<size_t>(cameras), sizeof(cameraState))));
			while (n < cameras)
			{
				camera = n + 1;
				cameraStates[camera - 1].handle = hCamera; // camera was already opened at the previous call

				// First, reset all of the camera setting to the default values
				MEXMESSAGE(PCO_ResetSettingsToDefault(hCamera));
				WORD wRecState;
				MEXMESSAGE(PCO_GetRecordingState(hCamera, &wRecState));
				if (wRecState == 1)
				{
					MEXMESSAGE(PCO_SetRecordingState(hCamera, 0));
				}

				MEXMESSAGE(PCO_SetBinning(hCamera, 1, 1));
				MEXMESSAGE(PCO_SetTriggerMode(hCamera, 0)); // auto trigger

				MEXMESSAGE(PCO_GetSizes(hCamera, &cameraStates[camera - 1].xsize, &cameraStates[camera - 1].ysize, &cameraStates[n].max_xsize, &cameraStates[n].max_ysize));

				// set ROI to full frame 
				WORD wRoiX0 = 1;
				WORD wRoiY0 = 1;
				WORD wRoiX1 = cameraStates[n].max_xsize;
				WORD wRoiY1 = cameraStates[n].max_ysize;
				MEXMESSAGE(PCO_SetROI(hCamera, wRoiX0, wRoiY0, wRoiX1, wRoiY1));
				MEXMESSAGE(PCO_ArmCamera(hCamera));
				MEXMESSAGE(PCO_GetSizes(hCamera, &cameraStates[camera - 1].xsize, &cameraStates[camera - 1].ysize, &cameraStates[n].max_xsize, &cameraStates[n].max_ysize));
#ifndef NDEBUG
				mexPrintf("Current ROI = %d x %d, Chip size = %d x %d\n", cameraStates[camera - 1].xsize, cameraStates[camera - 1].ysize, cameraStates[n].max_xsize, cameraStates[n].max_ysize);
#endif

				cameraStates[camera - 1].wBitPerPixel = 16;
				cameraStates[camera - 1].ImageSizeBytes = cameraStates[camera - 1].xsize * cameraStates[camera - 1].ysize * cameraStates[camera - 1].wBitPerPixel / 8;

				cameraStates[camera - 1].AcqBuffers = static_cast<WORD**>(mxCalloc(MaxNumberOfBuffers, sizeof(WORD*)));
				mexMakeMemoryPersistent(cameraStates[camera - 1].AcqBuffers);

				cameraStates[camera - 1].BufferEvent = static_cast<HANDLE*>(mxCalloc(MaxNumberOfBuffers, sizeof(HANDLE)));
				mexMakeMemoryPersistent(cameraStates[camera - 1].BufferEvent);

				cameraStates[camera - 1].NumberOfBuffers = MaxNumberOfBuffers;
				cameraStates[camera - 1].BufferWatcherThread = NULL;
				cameraStates[camera - 1].AcquisitionTimeoutSec = 10;

				cameraStates[camera - 1].NumberOfFrames = 1;
				cameraStates[camera - 1].accumulations = 1;
				cameraStates[camera - 1].ImageBuffer = NULL;
				cameraStates[camera - 1].NumberOfFramesReceived = 0;
				cameraStates[camera - 1].bAcquisitionStarted = false;

				cameraStates[camera - 1].caminfo.wSize = sizeof(PCO_Description);
				MEXMESSAGE(PCO_GetCameraDescription(hCamera, &cameraStates[camera - 1].caminfo));

				cameraStates[camera - 1].szCameraName = static_cast<char*>(mxCalloc(StringLength, sizeof(char)));
				mexMakeMemoryPersistent(cameraStates[camera - 1].szCameraName);
				PCO_GetCameraName(hCamera, cameraStates[camera - 1].szCameraName, StringLength);
#ifndef NDEBUG
				mexPrintf("Camera name = %s\n", cameraStates[camera - 1].szCameraName);
#endif

				cameraStates[camera - 1].strCamType.wSize = sizeof(PCO_CameraType);
				MEXMESSAGE(PCO_GetCameraType(hCamera, &cameraStates[camera - 1].strCamType));
#ifndef NDEBUG
				mexPrintf("Camera type = 0x%x\n", cameraStates[camera - 1].strCamType.wCamType);
#endif

				if (_strnicmp(cameraStates[camera - 1].szCameraName, "pco.edge", 8) == 0)
				{
#ifndef NDEBUG
					mexPrintf("pco.edge detected. Performing pco.edge specific initialization.\n");
#endif
					MEXMESSAGE(PCO_GetPixelRate(hCamera, &cameraStates[camera - 1].dwPixelRate));
					if (cameraStates[camera - 1].dwPixelRate != cameraStates[camera - 1].caminfo.dwPixelRateDESC[1])
					{
						// Set the pixel Readout Rate to fastest - 280 MHz
						MEXMESSAGE(PCO_SetPixelRate(hCamera, cameraStates[camera - 1].caminfo.dwPixelRateDESC[1]));
						MEXMESSAGE(PCO_ArmCamera(hCamera));
						MEXMESSAGE(PCO_GetPixelRate(hCamera, &cameraStates[camera - 1].dwPixelRate));
					}

					WORD wDestInterface;
					wDestInterface = static_cast<WORD>(INTERFACE_CL_SCCMOS);
					WORD wFormat;
					wFormat = static_cast<WORD>(SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM); // mode B - readout starts from the center
					WORD wReserved1 = 0, wReserved2 = 0;
					MEXMESSAGE(PCO_SetInterfaceOutputFormat(hCamera, wDestInterface, wFormat, wReserved1, wReserved2));

					WORD wIdentifier = 0x0;
					if (cameraStates[camera - 1].strCamType.wCamType == CAMERATYPE_PCO_EDGE_HS)
					{
						// CameraLink HS special init
						MEXMESSAGE(PCO_GetTransferParameter(hCamera, &cameraStates[camera - 1].TransferParamHS, sizeof(cameraStates[camera - 1].TransferParamHS)));
						cameraStates[camera - 1].TransferParamHS.Transmit = 1;         // single or continuous transmitting images, 0-single, 1-continuous
						MEXMESSAGE(PCO_SetTransferParameter(hCamera, &cameraStates[camera - 1].TransferParamHS, sizeof(cameraStates[camera - 1].TransferParamHS)));
					}
					else
					{
						MEXMESSAGE(PCO_GetTransferParameter(hCamera, &cameraStates[camera - 1].TransferParam, sizeof(cameraStates[camera - 1].TransferParam)));

						cameraStates[camera - 1].TransferParam.baudrate = 115200;
						if (cameraStates[camera - 1].xsize > 1920)
						{
							if ((cameraStates[camera - 1].strCamType.wCamType == CAMERATYPE_PCO_EDGE) && (cameraStates[camera - 1].dwPixelRate == cameraStates[camera - 1].caminfo.dwPixelRateDESC[1]))
							{
								cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x12L | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
								wIdentifier = 0x1612;
							}
							else
							{
								cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x16 | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
							}
						}
						else
						{
							cameraStates[camera - 1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x16 | SCCMOS_FORMAT_CENTER_TOP_CENTER_BOTTOM;
						}

						cameraStates[camera - 1].TransferParam.Transmit = 1;         // single or continuous transmitting images, 0-single, 1-continuous

						MEXMESSAGE(PCO_SetTransferParameter(hCamera, &cameraStates[camera - 1].TransferParam, sizeof(cameraStates[camera - 1].TransferParam)));
						//int err;
						//err = PCO_SetTransferParameter(hCamera, &cameraStates[camera-1].TransferParam, sizeof(cameraStates[camera-1].TransferParam));
						//if(err != PCO_NOERROR)
						//{
						//	if((err & PCO_ERROR_DRIVER_LUT_MISMATCH) == PCO_ERROR_DRIVER_LUT_MISMATCH)
						//	{
						//		mexPrintf("Camera and sc2_cl_xxx.dll lut do not match.\r\nSwitching back to 12bit mode.\r\nPlease update your sdk dlls and/or Camware!", MB_ICONERROR | MB_OK, 0);
						//		cameraStates[camera-1].TransferParam.DataFormat = PCO_CL_DATAFORMAT_5x12 | SCCMOS_FORMAT_TOP_CENTER_BOTTOM_CENTER;
						//		err = PCO_SetTransferParameter(hCamera, &cameraStates[camera-1].TransferParam, sizeof(cameraStates[camera-1].TransferParam));
						//		if(err != PCO_NOERROR)
						//		{
						//			return err;
						//		}
						//	}
						//}

						WORD wParameter = 0;
						MEXMESSAGE(PCO_SetActiveLookupTable(hCamera, &wIdentifier, &wParameter));
					}

					MEXMESSAGE(PCO_ArmCamera(hCamera));

					MEXMESSAGE(PCO_CamLinkSetImageParameters(hCamera, cameraStates[camera - 1].xsize, cameraStates[camera - 1].ysize));

					if (cameraStates[camera - 1].strCamType.wCamType == CAMERATYPE_PCO_EDGE_42)
					{
#ifndef NDEBUG
						mexPrintf("pco.edge 4.2 detected. Switching off NoiseFilter and HotPixelCorrection.\n");
#endif
						// switch off corrections
						MEXMESSAGE(PCO_SetNoiseFilterMode(hCamera, NOISE_FILTER_MODE_OFF));
						MEXMESSAGE(PCO_SetHotPixelCorrectionMode(hCamera, HOT_PIXEL_CORRECTION_OFF));
					}

					//PCO_Recording strRecording;
					//strRecording.wSize = sizeof(PCO_Recording);
					//MEXMESSAGE(PCO_GetRecordingStruct(hCamera, &strRecording));

					if (cameraStates[camera - 1].strCamType.wCamType == CAMERATYPE_PCO_EDGE)
					{
						int ts[3] = { 5000, 5000, 250 }; // command, image, channel timeout
						PCO_SetTimeouts(hCamera, &ts[0], 2); // only 2 valid entries for CameraLink interface
						DEBUG("New timeouts are set!\n");
					}

				}

				MEXMESSAGE(PCO_SetAcquireMode(hCamera, 0)); // auto mode = all images taken are stored - default

				MEXMESSAGE(PCO_ArmCamera(hCamera));
				MEXMESSAGE(PCO_SetRecordingState(hCamera, 0)); // camera is stopped

				createBuffers();

				n++;
			}
			// sets current camera number
			camera = 1;
		}
		MEXLEAVE;
#ifndef NDEBUG
		mexPrintf("%d camera%s available.\n", static_cast<int>(cameras), (cameras == 1) ? "" : "s");
#endif
	}
	return static_cast<int>(cameras);
}


MEXFUNCTION_LINKAGE
void mexFunction(int nlhs, mxArray * plhs[], int nrhs, const mxArray * prhs[])
{
	if (nrhs == 0 && nlhs == 0)
	{
		mexPrintf("\nPCO.edge sCMOS camera interface.\n\n\tAndriy Chmyrov © 13.01.2014\n\n");
		return;
	}
	if (driver == NULL) mexErrMsgTxt("Semaphore not initialized.");
	if (mexStartup() == 0) return;
	int n = 0;
	while (n < nrhs)
	{
		SHORT index;
		int field;
		switch (mxGetClassID(prhs[n]))
		{
		default:
			mexErrMsgTxt("Parameter name expected.");
		case mxCHAR_CLASS:
		{
			char read[StringLength];
			if (mxGetString(prhs[n], read, StringLength)) mexErrMsgTxt("Unknown parameter.");
			if (++n < nrhs)
			{
				setParameter(read, prhs[n]);
				break;
			}
			if (nlhs > 1) mexErrMsgTxt("Too many output arguments.");
			VALUE = getParameter(read);

			return;
		}
		case mxDOUBLE_CLASS:
			index = static_cast<SHORT>(getScalar(prhs[n]));
			if (index < 1 || index > cameras) mexErrMsgTxt("Invalid camera handle.");
			camera = index;
			DEBUG("Camera selected");
			break;
		case mxSTRUCT_CLASS:
			for (index = 0; index < static_cast<int>(mxGetNumberOfElements(prhs[n])); index++)
				for (field = 0; field < mxGetNumberOfFields(prhs[n]); field++)
					setParameter(mxGetFieldNameByNumber(prhs[n], field), mxGetFieldByNumber(prhs[n], index, field));
		}
		n++;
	}
	switch (nlhs)
	{
	default:
		mexErrMsgTxt("Too many output arguments.");
	case 1:
		VALUE = mxCreateDoubleScalar(static_cast<double>(cameras));
	case 0:;
	}

}
