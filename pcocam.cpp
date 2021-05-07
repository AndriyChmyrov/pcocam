// pcocam.cpp : Defines the exported functions for the DLL application.
//

#include "pch.h"
#include "pcocam.h"

//#include <boost/chrono.hpp>
//#include <boost/thread.hpp>

/*	Selected camera.
*/
SHORT camera = -1;

/*	Number of available cameras or -1 if driver not initialized.
*/
SHORT cameras = -1;

/*	Camera states. Unused handles are set to 0.
*/
cameraState* cameraStates;


/*	Check for and read a scalar value.
*/
double getScalar(const mxArray* array)
{	if (!mxIsNumeric(array) || mxGetNumberOfElements(array) != 1) mexErrMsgTxt("Not a scalar.");
	return mxGetScalar(array);
}


void stopCamera(HANDLE hCamera)
{
	MEXENTER;
	if ( cameraStates[camera-1].strCamType.wCamType == CAMERATYPE_PCO_EDGE_42 )
	{
		MEXMESSAGE(PCO_SetRecordingState(hCamera, 0));
		MEXMESSAGE(PCO_CancelImages(hCamera));
	}
	else
	{
		MEXMESSAGE(PCO_CancelImages(hCamera));
		MEXMESSAGE(PCO_SetRecordingState(hCamera, 0));
	}
	MEXLEAVE;
}

/*
static double _s_get_fac(WORD dTb)
{
	switch(dTb)
	{
	case 0x0000:
		return 1e-9;
	case 0x0001:
		return 1e-6;
	case 0x0002:
		return 1e-3;
	default:
		return 0;
	}
}
*/

unsigned __stdcall BufferWatcher(LPVOID lpParam)
{
	UNREFERENCED_PARAMETER(lpParam);
	HANDLE	hCamera = cameraStates[camera - 1].handle;

	while (cameraStates[camera - 1].NumberOfFramesReceived < cameraStates[camera - 1].NumberOfFrames)
	{
		SHORT	nBuffersUsed = (std::min)(cameraStates[camera - 1].NumberOfFrames, MaxNumberOfBuffers);
		DWORD	nRet = WaitForMultipleObjects(nBuffersUsed, cameraStates[camera - 1].BufferEvent, FALSE, 5000);
		if (nRet == WAIT_TIMEOUT)
		{
			stopCamera(hCamera);
			mxFree(cameraStates[camera - 1].ImageBuffer);
			cameraStates[camera - 1].ImageBuffer = NULL;
			// mex function crashes if the following line is enabled - cannot print to Matlab from a thread
			// mexErrMsgTxt("WaitForMultipleObjects() timeout while receiving images from buffers!");
			_endthreadex(0);
		}
		else
		{
			if (nRet == WAIT_FAILED)
			{
				stopCamera(hCamera);
				mxFree(cameraStates[camera - 1].ImageBuffer);
				cameraStates[camera - 1].ImageBuffer = NULL;
				// mex function crashes if the following line is enabled - cannot print to Matlab from a thread
				// mexErrMsgTxt("WaitForMultipleObjects() failed while receiving images from buffers!");
				_endthreadex(0);
			}

			DWORD curr_buffer = nRet - WAIT_OBJECT_0;
			// Reset the event so it is not reused
			ResetEvent(cameraStates[camera - 1].BufferEvent[curr_buffer]);

			DWORD dwStatusDll, dwStatusDrv;
			MEXMESSAGE(PCO_GetBufferStatus(hCamera, static_cast<SHORT>(curr_buffer), &dwStatusDll, &dwStatusDrv));
			if (dwStatusDrv != PCO_NOERROR)
			{
				char* errorTxt = new char[StringLength];
				PCO_GetErrorTextSDK(dwStatusDrv, errorTxt, StringLength);
				stopCamera(hCamera); // not sure if this is needed
				_endthreadex(0);
			}

			// Fill ImageBuffer with data from the buffer
			ptrdiff_t offset;
			offset = cameraStates[camera - 1].ImageSizeBytes * cameraStates[camera - 1].NumberOfFramesReceived;
			WORD* AcqBuffer = cameraStates[camera - 1].AcqBuffers[curr_buffer];
			memcpy(cameraStates[camera - 1].ImageBuffer + offset, AcqBuffer, cameraStates[camera - 1].ImageSizeBytes);

			cameraStates[camera - 1].NumberOfFramesReceived++;

			if (cameraStates[camera - 1].NumberOfFramesReceived < cameraStates[camera - 1].NumberOfFrames)
			{
				// not all of the images are acquired
				// pass this buffer back to PCO SDK
				WORD wXRes = cameraStates[camera - 1].xsize;
				WORD wYRes = cameraStates[camera - 1].xsize;
				//WORD wBitPerPixel = 16;
				MEXMESSAGE(PCO_AddBufferEx(hCamera, 0, 0, static_cast<SHORT>(curr_buffer), wXRes, wYRes, cameraStates[camera - 1].wBitPerPixel));
			}
			else
			{
				// all of the images are acquired, stop the camera
				stopCamera(hCamera);
				_endthreadex(0);
			}
		}
	}

	return 0;
}


void CleanBufferWatcher()
{
	CloseHandle(cameraStates[camera - 1].BufferWatcherThread);
	if (cameraStates[camera - 1].ImageBuffer)
	{
		mxFree(cameraStates[camera - 1].ImageBuffer);
		cameraStates[camera - 1].ImageBuffer = NULL;
	}
	cameraStates[camera - 1].NumberOfFramesReceived = 0;
	cameraStates[camera - 1].bAcquisitionStarted = false;
}


/*	Get a parameter or status value.
*/
mxArray* getParameter(const char* name)
{	
	//int n = 0;
	HANDLE hCamera = cameraStates[camera-1].handle;

	if (_stricmp("ArmCamera",name) == 0)
	{	
		WORD wRecState;
		MEXMESSAGE(PCO_GetRecordingState(hCamera, &wRecState));
		if (wRecState == 1)
		{
			mexErrMsgTxt("Camera recording state is RUN. The command is rejected.");
		}

		MEXMESSAGE(PCO_ArmCamera(hCamera));

		WORD wXResAct, wYResAct, wXResMax, wYResMax;
		MEXMESSAGE(PCO_GetSizes(hCamera, &wXResAct, &wYResAct, &wXResMax, &wYResMax));
		if ( cameraStates[camera-1].xsize != wXResAct || cameraStates[camera-1].ysize != wYResAct)
		{
			cameraStates[camera-1].xsize = wXResAct;
			cameraStates[camera-1].ysize = wYResAct;
			cameraStates[camera-1].ImageSizeBytes = wXResAct * wYResAct * cameraStates[camera-1].wBitPerPixel / 8;
			deleteBuffers();
			createBuffers();
		}

		return mxCreateLogicalScalar(true);
	}

	if (_stricmp("AcquireFrames",name) == 0)
	{
		WORD wRecStateOld;
		MEXMESSAGE(PCO_GetRecordingState(hCamera, &wRecStateOld));

		if ( cameraStates[camera-1].strCamType.wCamType == CAMERATYPE_PCO_EDGE )
		{
			MEXMESSAGE(PCO_SetRecordingState(hCamera, 0));
			wRecStateOld = 0;
		}

		if ( wRecStateOld != 1)
		{
			mxArray* output;
			mwSize dims[3];
			dims[0] = static_cast<mwSize>(cameraStates[camera-1].xsize);
			dims[1] = static_cast<mwSize>(cameraStates[camera-1].ysize);
			dims[2] = static_cast<mwSize>(cameraStates[camera-1].NumberOfFrames);
			output = mxCreateNumericArray(3, dims, mxUINT16_CLASS, mxREAL);

			UINT8* pointer;
			pointer = static_cast<UINT8*>(mxGetData(output));

			MEXENTER;
			MEXMESSAGE(PCO_SetRecordingState(hCamera, 1));
			MEXLEAVE;

			WORD	wXRes = cameraStates[camera-1].xsize;
			WORD	wYRes = cameraStates[camera-1].ysize;
			SHORT	nBuffersUsed = (std::min)(cameraStates[camera-1].NumberOfFrames, MaxNumberOfBuffers);
			for(SHORT i = 0; i < nBuffersUsed; i++)
			{
				MEXMESSAGE(PCO_AddBufferEx(hCamera, 0, 0, i, wXRes, wYRes, cameraStates[camera-1].wBitPerPixel));
			}

			cameraStates[camera-1].NumberOfFramesReceived = 0;
			// read out images in a loop
			while ( cameraStates[camera-1].NumberOfFramesReceived < cameraStates[camera-1].NumberOfFrames )
			{
				DWORD nRet = WaitForMultipleObjects(nBuffersUsed, cameraStates[camera-1].BufferEvent, FALSE, 1000);
				if ( nRet == WAIT_TIMEOUT )
				{
					stopCamera(hCamera);
					mexErrMsgTxt("WaitForMultipleObjects() timeout while receiving images from buffers!");
				}
				else
				{
					if ( nRet == WAIT_FAILED )
					{
						stopCamera(hCamera);
						mexErrMsgTxt("WaitForMultipleObjects() failed while receiving images from buffers!");
					}
					DWORD curr_buffer = nRet - WAIT_OBJECT_0;

					// Reset the event so it is not reused
					ResetEvent(cameraStates[camera-1].BufferEvent[curr_buffer]);

					DWORD dwStatusDll, dwStatusDrv;
					MEXMESSAGE(PCO_GetBufferStatus(hCamera, (SHORT) curr_buffer, &dwStatusDll, &dwStatusDrv));
					if ( dwStatusDrv != PCO_NOERROR )
					{
						char* errorTxt = static_cast<char*>(mxCalloc(StringLength, sizeof(char)));
						PCO_GetErrorTextSDK(dwStatusDrv, errorTxt, StringLength);
						mexPrintf("Error during the image transfer from the PCO driver DLL to the PCO buffer.\n");
						stopCamera(hCamera);
						mexErrMsgTxt(errorTxt);
					}

					// Fill ImageBuffer with data from the buffer
					ptrdiff_t offset = cameraStates[camera - 1].ImageSizeBytes * cameraStates[camera - 1].NumberOfFramesReceived;
					WORD* AcqBuffer = cameraStates[camera - 1].AcqBuffers[curr_buffer];
					memcpy(pointer + offset, AcqBuffer, cameraStates[camera-1].ImageSizeBytes);

					cameraStates[camera-1].NumberOfFramesReceived++;

					if (cameraStates[camera-1].NumberOfFramesReceived < cameraStates[camera-1].NumberOfFrames)
					{
						// not all of the images are acquired, pass this buffer back to PCO SDK
						MEXMESSAGE(PCO_AddBufferEx(hCamera, 0, 0, static_cast<SHORT>(curr_buffer), wXRes, wYRes, cameraStates[camera-1].wBitPerPixel));
					}
					else
					{
						// all of the images are acquired
						cameraStates[camera - 1].NumberOfFramesReceived = 0;
						stopCamera(hCamera);
						return output;
					}
				}
			}
		}
		mexErrMsgTxt("Previous camera recording state was not 0!");
	}

	if (_stricmp("AcquisitionStart", name) == 0)
	{
		WORD wRecStateOld;
		MEXMESSAGE(PCO_GetRecordingState(hCamera, &wRecStateOld));
		if (wRecStateOld == 0)
		{
			SHORT	m_nBuffersUsed = (std::min)(cameraStates[camera - 1].NumberOfFrames, PCO_NUM_BUFFERS);
			for (SHORT i = 0; i < m_nBuffersUsed; i++)
			{
				WORD wXRes = cameraStates[camera - 1].xsize;
				WORD wYRes = cameraStates[camera - 1].ysize;
				MEXMESSAGE(PCO_AddBufferEx(hCamera, 0, 0, i, wXRes, wYRes, cameraStates[camera - 1].wBitPerPixel));
			}

			// allocate memory buffer where acquired images will be stored until transfer to MATLAB
			UINT8* output;
			output = static_cast<UINT8*>(mxMalloc(cameraStates[camera - 1].ImageSizeBytes * cameraStates[camera - 1].NumberOfFrames));
			mexMakeMemoryPersistent(output);
			cameraStates[camera - 1].ImageBuffer = output;

			cameraStates[camera - 1].BufferWatcherThread = (HANDLE)_beginthreadex(
				NULL,                   // default security attributes
				0,                      // use default stack size  
				&BufferWatcher,			// thread function name
				0,						// argument to thread function - no argument needed
				0,                      // use default creation flags 
				NULL);					// could be the thread identifier, but we don't use it


			// Check the return value for success.
			// If CreateThread fails, terminate execution. 
			// This will automatically clean up threads and memory. 
			if (cameraStates[camera - 1].BufferWatcherThread == NULL)
			{
				mexErrMsgTxt("Error while creating BufferWatcherThread! Acquisition not started!");
			}

			MEXENTER;
			MEXMESSAGE(PCO_SetRecordingState(hCamera, 1));
			MEXLEAVE;
			cameraStates[camera - 1].bAcquisitionStarted = true;
		}
		else
		{
			mexErrMsgTxt("Previous camera recording state was not 0.");
		}
		return mxCreateLogicalScalar(1);
	}

	if (_stricmp("AcquisitionStop", name) == 0)
	{
		if (!cameraStates[camera - 1].bAcquisitionStarted)
		{
			mexErrMsgTxt("Acquisition was not started!");
		}

		DWORD dwSleepIntervalMs = 200;
		DWORD nloopsmax = cameraStates[camera - 1].AcquisitionTimeoutSec * 1000 / dwSleepIntervalMs;
		DWORD loop = 0;
		while (cameraStates[camera - 1].NumberOfFramesReceived < cameraStates[camera - 1].NumberOfFrames)
		{
			if (loop > nloopsmax)
			{
				CleanBufferWatcher();
				mexErrMsgTxt("Timeout occurred during image acquisition!\n");
			}
			Sleep(dwSleepIntervalMs);
			loop++;
		}

		mxArray* output;
		if (cameraStates[camera - 1].NumberOfFrames > 1)
		{
			mwSize dims[3];
			dims[0] = static_cast<mwSize>(cameraStates[camera - 1].xsize);
			dims[1] = static_cast<mwSize>(cameraStates[camera - 1].ysize);
			dims[2] = static_cast<mwSize>(cameraStates[camera - 1].NumberOfFrames);
			output = mxCreateNumericArray(3, dims, mxUINT16_CLASS, mxREAL);
		}
		else
		{
			output = mxCreateNumericMatrix(cameraStates[camera - 1].xsize, cameraStates[camera - 1].ysize, mxUINT16_CLASS, mxREAL);
		}

		UINT8* pointer;
		pointer = static_cast<UINT8*>(mxGetData(output));
		size_t bytesToCopy = cameraStates[camera - 1].ImageSizeBytes * cameraStates[camera - 1].NumberOfFramesReceived;
		memcpy(pointer, cameraStates[camera - 1].ImageBuffer, bytesToCopy);

		CleanBufferWatcher();

		return output;
	}

	if (_stricmp("AcquisitionTimeoutSec", name) == 0)
	{
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera - 1].AcquisitionTimeoutSec));
	}

	if (_stricmp("FrameRate", name) == 0)
	{	
		WORD wFrameRateStatus;
		DWORD dwFrameRate, dwFrameRateExposure;
		MEXMESSAGE(PCO_GetFrameRate(hCamera, &wFrameRateStatus, &dwFrameRate, &dwFrameRateExposure));
		//if (wFrameRateStatus != 0x8000)
		//	return mxCreateDoubleScalar(static_cast<double>(dwFrameRate));
		//else
		//	return mxCreateDoubleScalar(0);
		return mxCreateDoubleScalar(static_cast<double>(dwFrameRate) / 1000);
	}

	if (_stricmp("FrameCount",name) == 0)
	{
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].NumberOfFrames));
	}

	if (_stricmp("Frames",name) == 0)
	{
		mxArray* output;

		if (cameraStates[camera - 1].NumberOfFramesReceived < cameraStates[camera - 1].NumberOfFrames)
		{
			// camera is still acquiring images, return -1 as a sign of error
			mexPrintf("Camera is still acquiring images!\n");
			return mxCreateDoubleScalar(-1);
		}

		if ( cameraStates[camera-1].NumberOfFrames > 1 )
		{
			mwSize dims[3];
			dims[0] = static_cast<mwSize>(cameraStates[camera-1].xsize);
			dims[1] = static_cast<mwSize>(cameraStates[camera-1].ysize);
			dims[2] = static_cast<mwSize>(cameraStates[camera-1].NumberOfFrames);
			output = mxCreateNumericArray(3, dims, mxUINT16_CLASS, mxREAL);
		}
		else
		{
			output = mxCreateNumericMatrix(cameraStates[camera-1].xsize,cameraStates[camera-1].ysize,mxUINT16_CLASS,mxREAL);
		}

		UINT8* pointer;
		pointer = static_cast<UINT8*>(mxGetData(output));
		size_t bytesToCopy = cameraStates[camera-1].ImageSizeBytes * cameraStates[camera-1].NumberOfFramesReceived;
		memcpy(pointer, cameraStates[camera-1].ImageBuffer, bytesToCopy);

		if (cameraStates[camera-1].NumberOfFramesReceived == cameraStates[camera-1].NumberOfFrames )
		{
			CloseHandle(cameraStates[camera - 1].BufferWatcherThread);
			mxFree(cameraStates[camera - 1].ImageBuffer);
			cameraStates[camera - 1].ImageBuffer = NULL;
			cameraStates[camera - 1].NumberOfFramesReceived = 0;
		}
		//MEXENTER;
		//MEXMESSAGE(PCO_CancelImages(hCamera));
		//MEXMESSAGE(PCO_SetRecordingState(hCamera, 0));
		//MEXLEAVE;

		return output;
	}

	if (_stricmp("RebootCamera",name) == 0)
	{	
		MEXMESSAGE(PCO_RebootCamera(hCamera));
		return mxCreateLogicalScalar(true);
	}

	if (_stricmp("CameraHealthStatus",name) == 0)
	{	
		DWORD dwWarn, dwErr, dwStatus;
		MEXMESSAGE(PCO_GetCameraHealthStatus(hCamera, &dwWarn, &dwErr, &dwStatus));
		mexPrintf("Warning code = 0x%X, Error code = 0x%X, Status code = 0x%X\n", dwWarn, dwErr, dwStatus);

		mxArray* output;
		output = mxCreateDoubleMatrix(1, 3, mxREAL);
		double* pointer;
		pointer = mxGetDoubles(output);
		pointer[0] = static_cast<double>(dwWarn);
		pointer[1] = static_cast<double>(dwErr);
		pointer[2] = static_cast<double>(dwStatus);
		return output;
	}

	if (_stricmp("TriggerMode",name) == 0)
	{	
		WORD wTriggerMode;
		MEXMESSAGE(PCO_GetTriggerMode(hCamera, &wTriggerMode));
		return mxCreateDoubleScalar(static_cast<double>(wTriggerMode));
	}

	if (_stricmp("SensorSize",name) == 0)
	{	
		mxArray* output;
		output = mxCreateDoubleMatrix(1, 2, mxREAL);
		double* pointer;
		pointer = static_cast<double*>(mxGetData(output));
		pointer[0] = static_cast<double>(cameraStates[camera-1].max_xsize);
		pointer[1] = static_cast<double>(cameraStates[camera-1].max_ysize);
		return output;
	}

	if (_stricmp("SensorWidth",name) == 0)
	{	
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].max_xsize));
	}

	if (_stricmp("SensorHeight",name) == 0)
	{	
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].max_ysize));
	}

	if (_stricmp("ROI",name) == 0)
	{
		WORD wRoiX0, wRoiY0, wRoiX1, wRoiY1;
		MEXMESSAGE(PCO_GetROI(hCamera, &wRoiX0, &wRoiY0, &wRoiX1, &wRoiY1));
		mxArray* output;
		output = mxCreateDoubleMatrix(1, 4, mxREAL);
		double* pointer;
		pointer = static_cast<double*>(mxGetData(output));
		pointer[0] = static_cast<double>(wRoiX0);
		pointer[1] = static_cast<double>(wRoiY0);
		pointer[2] = static_cast<double>(wRoiX1);
		pointer[3] = static_cast<double>(wRoiY1);
		return output;
	}

	if (_stricmp("ExposureTime",name) == 0)
	{	
		/*	It is recommended by PCO to use only one function - PCO_GetFrameRate or PCO_GetDelayExposureTime
			I decided to use PCO_GetFrameRate
		*/
		//DWORD dwDelay, dwExposure;
		//WORD wTimeBaseDelay, wTimeBaseExposure;
		//MEXMESSAGE(PCO_GetDelayExposureTime(hCamera, &dwDelay, &dwExposure, &wTimeBaseDelay, &wTimeBaseExposure));
		//return mxCreateDoubleScalar(static_cast<double>(dwExposure) * _s_get_fac(wTimeBaseExposure));

		WORD wFrameRateStatus;
		DWORD dwFrameRate, dwFrameRateExposure;
		MEXMESSAGE(PCO_GetFrameRate(hCamera, &wFrameRateStatus, &dwFrameRate, &dwFrameRateExposure));
		#ifndef NDEBUG
		switch (wFrameRateStatus)
		{
			case 0x0000: 
				mexPrintf("Settings consistent, all conditions met\n");
				break;
			case 0x0001: 
				mexPrintf("Frame rate trimmed, frame rate was limited by readout time\n");
				break;
			case 0x0002: 
				mexPrintf("Frame rate trimmed, frame rate was limited by exposure time\n");
				break;
			case 0x0004: 
				mexPrintf("Exposure time trimmed, exposure time cut to frame time\n");
				break;
			case 0x8000: 
				mexPrintf("The return values dwFrameRate and dwFrameRateExposure are not yet validated.\n");
		}
		#endif
		return mxCreateDoubleScalar(static_cast<double>(dwFrameRateExposure / 1e9));
	}

	if (_stricmp("SensorTemperature",name) == 0)
	{	
		SHORT sCCDTemp, sCamTemp, sPowTemp;
		MEXMESSAGE(PCO_GetTemperature(hCamera, &sCCDTemp, &sCamTemp, &sPowTemp));
		if ( sCCDTemp != 0x8000 )
		{
			return mxCreateDoubleScalar(static_cast<double>(sCCDTemp / 10));
		}
		else
		{
			DEBUG("Sensor temperature is not available!");
			return mxCreateDoubleScalar(0);
		}

	}

	if (_stricmp("HotPixelCorrectionMode",name) == 0)
	{
		if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_HOT_PIXEL_CORRECTION)
		{
			WORD wHotPixelCorrectionMode;
			MEXMESSAGE(PCO_GetHotPixelCorrectionMode(hCamera, &wHotPixelCorrectionMode));
			return mxCreateDoubleScalar(static_cast<double>(wHotPixelCorrectionMode));
		}
		else
		{
			mexPrintf("HotPixelCorrection is not supported by the camera.\n");
			return mxCreateDoubleScalar(0);
		}
	}

	if (_stricmp("NoiseFilterMode",name) == 0)
	{
		if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_NOISE_FILTER)
		{
			WORD wNoiseFilterMode;
			MEXMESSAGE(PCO_GetNoiseFilterMode(hCamera, &wNoiseFilterMode));
			return mxCreateDoubleScalar(static_cast<double>(wNoiseFilterMode));
		}
		else
		{
			mexPrintf("NoiseFilter is not supported by the camera.\n");
			return mxCreateDoubleScalar(0);
		}
	}

	if (_stricmp("PixelReadoutRate",name) == 0)
	{
		DWORD dwPixelRate;
		MEXMESSAGE(PCO_GetPixelRate(hCamera, &dwPixelRate));
		cameraStates[camera-1].dwPixelRate = dwPixelRate;
		return mxCreateDoubleScalar(static_cast<double>(dwPixelRate));
	}

	if (_stricmp("AOIHeight",name) == 0)
	{
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].ysize));
	}

	if (_stricmp("AOIWidth",name) == 0)
	{
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].xsize));
	}

	if (_stricmp("ActiveLookupTable",name) == 0)
	{
		if (_strnicmp(cameraStates[camera-1].szCameraName, "pco.edge", 8) == 0)
		{
			WORD wIdentifier;
			WORD wParameter;
			MEXMESSAGE(PCO_GetActiveLookupTable(hCamera, &wIdentifier, &wParameter));
			return mxCreateDoubleScalar(static_cast<double>(wIdentifier));
		}
		else
		{
			mexPrintf("ActiveLookupTable is not supported by the camera %s\n",cameraStates[camera-1].szCameraName);
			return mxCreateDoubleScalar(0);
		}
	}

	if (_stricmp("CameraType",name) == 0)
	{
		PCO_CameraType strCamType;
		strCamType.wSize = sizeof(PCO_CameraType);
		MEXMESSAGE(PCO_GetCameraType(hCamera, &strCamType));
		mexPrintf("Camera type = %d\n",strCamType.wCamType);
		mexPrintf("Camera sub type = %d\n",strCamType.wCamSubType);
		mexPrintf("Camera serial number = %d\n",strCamType.dwSerialNumber);
		mexPrintf("Hardware version number = %d\n",strCamType.dwHWVersion);
		mexPrintf("Firmware version number = %d\n",strCamType.dwFWVersion);
		mexPrintf("Interface type = %d ",strCamType.wInterfaceType);
		switch (strCamType.wInterfaceType)
		{
		case INTERFACE_FIREWIRE:
			mexPrintf("(FIREWIRE)");
			break;
		case INTERFACE_CAMERALINK:
			mexPrintf("(CAMERALINK)");
			break;
		case INTERFACE_USB:
			mexPrintf("(USB)");
			break;
		case INTERFACE_ETHERNET:
			mexPrintf("(ETHERNET)");
			break;
		case INTERFACE_SERIAL:
			mexPrintf("(SERIAL)");
			break;
		case INTERFACE_USB3:
			mexPrintf("(USB3)");
			break;
		case INTERFACE_CAMERALINKHS:
			mexPrintf("(CAMERALINK HS)");
			break;
		case INTERFACE_COAXPRESS:
			mexPrintf("(COAXPRESS)");
		}
		mexPrintf("\nNumber of hardware boards present = %d\n",strCamType.strHardwareVersion.BoardNum);
		for (WORD i = 0; i < strCamType.strHardwareVersion.BoardNum; i++)
		{
			mexPrintf(" Board #%d: Name = %s, Production batch # = %d, Revision = %d, Variant = %d\n", i,
				strCamType.strHardwareVersion.Board[i].szName,
				strCamType.strHardwareVersion.Board[i].wBatchNo,
				strCamType.strHardwareVersion.Board[i].wRevision,
				strCamType.strHardwareVersion.Board[i].wVariant);
		}
		mexPrintf("Number of devices present = %d\n",strCamType.strFirmwareVersion.DeviceNum);
		for (WORD i = 0; i < strCamType.strFirmwareVersion.DeviceNum; i++)
		{
			mexPrintf(" Device #%d: Name = %s, Revision = %d.%d, Variant = %d\n", i,
				strCamType.strFirmwareVersion.Device[i].szName,
				strCamType.strFirmwareVersion.Device[i].bMajorRev,
				strCamType.strFirmwareVersion.Device[i].bMinorRev,
				strCamType.strFirmwareVersion.Device[i].wVariant);
		}
		return mxCreateDoubleScalar(static_cast<double>(strCamType.wCamType));
	}

	if (_stricmp("CameraHealthStatus",name) == 0)
	{
		DWORD dwWarn, dwErr, dwStatus;
		MEXMESSAGE(PCO_GetCameraHealthStatus(hCamera, &dwWarn, &dwErr, &dwStatus));
		mexPrintf("Warning code = 0x%X\n", dwWarn);
//		switch (dwWarn)
//		{
//			// write warning codes here
//		}
		mexPrintf("Error code = 0x%X\n", dwErr);
		mexPrintf("Status code = 0x%X\n", dwStatus);
		return mxCreateDoubleScalar(static_cast<double>(dwStatus));
	}

	if (_stricmp("ShutterMode",name) == 0)
	{
		if (_strnicmp(cameraStates[camera-1].szCameraName, "pco.edge", 8) != 0)
		{
			DEBUG(cameraStates[camera-1].szCameraName);
			mexErrMsgTxt("ShutterMode command is supported only by pco.edge camera!");
		}
		WORD wType, wLen = 10;
		DWORD dwSetup[10];
		MEXMESSAGE(PCO_GetCameraSetup(hCamera, &wType, &dwSetup[0], &wLen));
		return mxCreateDoubleScalar(static_cast<double>(dwSetup[0]));
	}

	if (_stricmp("EnableSoftROI",name) == 0)
	{
		if (_strnicmp(cameraStates[camera-1].szCameraName, "pco.edge", 8) != 0)
		{
			mexErrMsgTxt("EnableSoftROI command is supported only by pco.edge camera!");
		}
		return mxCreateDoubleScalar(static_cast<double>(cameraStates[camera-1].wSoftROI));
	}

	#ifndef NDEBUG
	mexPrintf("%s:%d - ",__FILE__,__LINE__);
	#endif
	mexPrintf("\"%s\" unknown.\n",name);
	return NULL;
}


int my_round(double x)
{
    return static_cast<int>(floor(x + 0.5f));
}


/*	Check for and read a matrix
*/
double* getArray(const mxArray* array, size_t size)
{	if (!mxIsNumeric(array) || mxGetNumberOfElements(array) != size) mexErrMsgTxt("Supplied wrong number of elements in array.");
	return mxGetPr(array);
}


/*	Set a measurement parameter.
*/
void setParameter(const char* name, const mxArray* field)
{  
	HANDLE hCamera = cameraStates[camera-1].handle;

	if (mxGetNumberOfElements(field) < 1) return;
	if (_stricmp("camera",name) == 0) return;
	if (_stricmp("cameras",name) == 0) return;
	if (camera < 1 || camera > cameras) mexErrMsgTxt("Invalid camera handle.");

	if (_stricmp("FrameRate",name) == 0)
	{	
		double framerate = getScalar(field);
		WORD wFrameRateStatus;
		//WORD wFramerateMode = 0x0001; // Framerate has priority, (exposure time will be trimmed)
		WORD wFramerateMode = 0x0002; // Exposure time has priority, (frame rate will be trimmed)
		DWORD dwFrameRate;
		DWORD dwFrameRateExposure;

		MEXMESSAGE(PCO_GetFrameRate(hCamera, &wFrameRateStatus, &dwFrameRate, &dwFrameRateExposure));
		if (dwFrameRate == static_cast<DWORD>(my_round(framerate*1000)))
		{
			return;
		}
		dwFrameRate = static_cast<DWORD>(framerate * 1000); // function accepts value in milli-Hz
		MEXENTER;
		MEXMESSAGE(PCO_SetFrameRate(hCamera, &wFrameRateStatus, wFramerateMode, &dwFrameRate, &dwFrameRateExposure));
		MEXLEAVE;

		#ifndef NDEBUG
		switch (wFrameRateStatus)
		{
			case 0x0000: 
				//mexPrintf("Settings consistent, all conditions met\n");
				break;
			case 0x0001: 
				mexPrintf("Frame rate trimmed, frame rate was limited by readout time\n");
				break;
			case 0x0002: 
				mexPrintf("Frame rate trimmed, frame rate was limited by exposure time\n");
				break;
			case 0x0004: 
				mexPrintf("Exposure time trimmed, exposure time cut to frame time\n");
				break;
			case 0x8000: 
				//mexPrintf("The return values dwFrameRate and dwFrameRateExposure are not yet validated.\n");
				;
		}
		//mexPrintf("Current framerate = %d [Hz], exposure time = %f [s]\n", dwFrameRate/1000, dwFrameRateExposure/1e9);
		#endif

		return;
	}

	if (_stricmp("FrameCount",name) == 0)
	{
		cameraStates[camera-1].NumberOfFrames = static_cast<WORD>(getScalar(field));
		return;
	}

	if (_stricmp("Acquire",name) == 0)
	{
		WORD wRecStateOld;
		WORD wRecStateNew = static_cast<WORD>(getScalar(field));
		MEXMESSAGE(PCO_GetRecordingState(hCamera, &wRecStateOld));
		if ( wRecStateOld != wRecStateNew)
			if (wRecStateNew == 1)
			{
				if (wRecStateOld == 0)
				{
					WORD wXRes = cameraStates[camera-1].xsize;
					WORD wYRes = cameraStates[camera-1].ysize;
					/*
					WORD wXResAct, wYResAct, wXResMax, wYResMax;
					MEXMESSAGE(PCO_GetSizes(hCamera, &wXResAct, &wYResAct, &wXResMax, &wYResMax));
					*/

					SHORT	m_nBuffersUsed = (std::min)(cameraStates[camera-1].NumberOfFrames, PCO_NUM_BUFFERS);
					SHORT	i;
					for(i = 0; i < m_nBuffersUsed; i++)
					{
						MEXMESSAGE(PCO_AddBufferEx(hCamera, 0, 0, i, wXRes, wYRes, cameraStates[camera - 1].wBitPerPixel));
					}

					// allocate memory buffer where acquired images will be stored until transfer to MATLAB
					UINT8* output;
					output = static_cast<UINT8*>(mxMalloc(cameraStates[camera - 1].ImageSizeBytes * cameraStates[camera - 1].NumberOfFrames));
					mexMakeMemoryPersistent(output);
					cameraStates[camera-1].ImageBuffer = output;

					cameraStates[camera - 1].BufferWatcherThread = (HANDLE)_beginthreadex(
						NULL,                   // default security attributes
						0,                      // use default stack size  
						&BufferWatcher,			// thread function name
						0,						// argument to thread function - no argument needed
						0,                      // use default creation flags 
						NULL);					// could be the thread identifier, but we don't use it


					// Check the return value for success.
					// If CreateThread fails, terminate execution. 
					// This will automatically clean up threads and memory. 
					if (cameraStates[camera - 1].BufferWatcherThread == NULL)
					{
						mexErrMsgTxt("Error while creating BufferWatcherThread! Acquisition not started!");
					}
	
					MEXENTER;
					MEXMESSAGE(PCO_SetRecordingState(hCamera, wRecStateNew));
					MEXLEAVE;
				}
				else
					mexErrMsgTxt("Previous camera recording state was not 0.");
			}
			else
			{
				//MEXENTER;
				stopCamera(hCamera);
				CloseHandle(cameraStates[camera - 1].BufferWatcherThread);
				mxFree(cameraStates[camera - 1].ImageBuffer);
				//MEXLEAVE;
			}
		return;
	}

	if (_stricmp("AcquisitionTimeoutSec", name) == 0)
	{
		UINT8 AcquisitionTimeoutSec = static_cast<UINT8>(getScalar(field));
		cameraStates[camera - 1].AcquisitionTimeoutSec = AcquisitionTimeoutSec;
		return;
	}

	if (_stricmp("TriggerMode", name) == 0)
	{	
		WORD triggermode = static_cast<WORD>(getScalar(field));
		WORD wRecState = 0;
		PCO_GetRecordingState(hCamera, &wRecState);
		if ( wRecState == 1)
		{
			mexErrMsgTxt("Camera recording state is RUN. The command is rejected.");
		}

		MEXENTER;
		MEXMESSAGE(PCO_SetTriggerMode(hCamera, triggermode));
		MEXLEAVE;
		return;
	}

	if (_stricmp("ROI",name) == 0)
	{
		double *arguments = getArray(field,4);
		if ( arguments[0] != floor(arguments[0]) ) mexErrMsgTxt("First argument is not an integer value.");
		if ( arguments[1] != floor(arguments[1]) ) mexErrMsgTxt("Second argument is not an integer value.");
		if ( arguments[2] != floor(arguments[2]) ) mexErrMsgTxt("Third argument is not an integer value.");
		if ( arguments[3] != floor(arguments[3]) ) mexErrMsgTxt("Forth argument is not an integer value.");
		if ( arguments[0] < 0 || arguments[1] < 0  || arguments[2] < 0  || arguments[3] < 0 ) mexErrMsgTxt("Positive arguments expected.");

		WORD wRoiX0 = static_cast<WORD>(arguments[0]);
		WORD wRoiY0 = static_cast<WORD>(arguments[1]);
		WORD wRoiX1 = static_cast<WORD>(arguments[2]);
		WORD wRoiY1 = static_cast<WORD>(arguments[3]);
		
		if ( wRoiX1 > cameraStates[camera-1].max_xsize ) mexErrMsgTxt("Third argument is larger then camera sensor width.");
		if ( wRoiY1 > cameraStates[camera-1].max_ysize ) mexErrMsgTxt("Forth argument is larger then camera sensor height.");

		MEXENTER;
		MEXMESSAGE(PCO_SetROI(hCamera, wRoiX0, wRoiY0, wRoiX1, wRoiY1));
		MEXLEAVE;

		WORD wRoiX0_, wRoiY0_, wRoiX1_, wRoiY1_;
		MEXMESSAGE(PCO_GetROI(hCamera, &wRoiX0_, &wRoiY0_, &wRoiX1_, &wRoiY1_));

		if ( wRoiX0_ != wRoiX0 || wRoiY0_ != wRoiY0 ||  wRoiX1_ != wRoiX1 ||  wRoiY1_ != wRoiY1 )
		{
			DEBUG("New ROI was not accepted! Check ROI constrains imposed by the camera!");
		}

		WORD currXsize = wRoiX1_ - wRoiX0_ + 1;
		WORD currYsize = wRoiY1_ - wRoiY0_ + 1;
		if ( (cameraStates[camera-1].xsize != currXsize ) || (cameraStates[camera-1].ysize != currYsize ) )
		{
			cameraStates[camera-1].xsize = currXsize;
			cameraStates[camera-1].ysize = currYsize;
			cameraStates[camera-1].ImageSizeBytes = cameraStates[camera-1].xsize * cameraStates[camera-1].ysize * cameraStates[camera-1].wBitPerPixel / 8;
			deleteBuffers();
			createBuffers();
		}

		return;
	}

	if (_stricmp("ExposureTime",name) == 0)
	{	
		/*	It's better to use only one function - PCO_Get/SetFrameRate or PCO_Get/SetDelayExposureTime
			I decided to use PCO_GetFrameRate
		*/
		//// set timebase in microseconds
		//WORD wTimeBaseDelay = 0x0001;
		//WORD wTimeBaseExposure = 0x0001;

		//DWORD dwDelay = 0;
		//DWORD dwExposure = static_cast<DWORD>(getScalar(field) * 1e6);
		//MEXMESSAGE(PCO_SetDelayExposureTime(hCamera, dwDelay, dwExposure, wTimeBaseDelay, wTimeBaseExposure));

		WORD wFrameRateStatus;
		//WORD wFramerateMode = 0x0001; // Framerate has priority, (exposure time will be trimmed)
		WORD wFramerateMode = 0x0002; // Exposure time has priority, (frame rate will be trimmed)
		DWORD dwFrameRate;
		DWORD dwFrameRateExposure;

		MEXMESSAGE(PCO_ArmCamera(hCamera));
		MEXMESSAGE(PCO_GetFrameRate(hCamera, &wFrameRateStatus, &dwFrameRate, &dwFrameRateExposure));

		dwFrameRateExposure = static_cast<DWORD>(getScalar(field) * 1e9);
		if (_stricmp(cameraStates[camera-1].szCameraName, "pco.edge rolling 4.2") == 0)
		{
			dwFrameRateExposure = (std::max)(dwFrameRateExposure, static_cast<DWORD>(500000)); // min 500 us exposure for "pco.edge rolling 4.2"
		}

		MEXMESSAGE(PCO_SetFrameRate(hCamera, &wFrameRateStatus, wFramerateMode, &dwFrameRate, &dwFrameRateExposure));

		#ifndef NDEBUG
		switch (wFrameRateStatus)
		{
			case 0x0000: 
				mexPrintf("Settings consistent, all conditions met\n");
				break;
			case 0x0001: 
				mexPrintf("Frame rate trimmed, frame rate was limited by readout time\n");
				break;
			case 0x0002: 
				mexPrintf("Frame rate trimmed, frame rate was limited by exposure time\n");
				break;
			case 0x0004: 
				mexPrintf("Exposure time trimmed, exposure time cut to frame time\n");
				break;
			case 0x8000: 
				mexPrintf("The return values dwFrameRate and dwFrameRateExposure are not yet validated.\n");
		}
		mexPrintf("Current framerate = %d [Hz], exposure time = %f [s]\n", dwFrameRate/1000, dwFrameRateExposure/1e9);
		#endif
		return;
	}

	if (_stricmp("HotPixelCorrectionMode",name) == 0)
	{
		WORD wRecState = 0;
		PCO_GetRecordingState(hCamera, &wRecState);
		if ( wRecState == 1)
		{
			mexErrMsgTxt("Camera recording state is RUN. The command is rejected.");
		}
		if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_HOT_PIXEL_CORRECTION)
		{
			WORD wHotPixelCorrectionMode;
			wHotPixelCorrectionMode = static_cast<WORD>(getScalar(field));
			if ( (wHotPixelCorrectionMode == HOT_PIXEL_CORRECTION_ON) & (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_HOTPIX_ONLY_WITH_NOISE_FILTER ) )
			{
				WORD wNoiseFilterMode;
				MEXMESSAGE(PCO_GetNoiseFilterMode(hCamera, &wNoiseFilterMode));
				if ( wNoiseFilterMode == NOISE_FILTER_MODE_OFF )
				{
					mexErrMsgTxt("HotPixelCorrectionMode is only possible when NoiseFilter is on!.");
				}
			}
			MEXMESSAGE(PCO_SetHotPixelCorrectionMode(hCamera, wHotPixelCorrectionMode));
		}
		else
		{
			mexPrintf("HotPixelCorrection is not supported\n");
		}
		return;
	}

	if (_stricmp("NoiseFilterMode",name) == 0)
	{
		WORD wRecState = 0;
		PCO_GetRecordingState(hCamera, &wRecState);
		if ( wRecState == 1)
		{
			mexErrMsgTxt("Camera recording state is RUN. The command is rejected.");
		}
		if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_NOISE_FILTER)
		{
			WORD wNoiseFilterMode;
			wNoiseFilterMode = static_cast<WORD>(getScalar(field));
			MEXMESSAGE(PCO_SetNoiseFilterMode(hCamera, wNoiseFilterMode));
		}
		else
		{
			mexPrintf("NoiseFilter is not supported by the camera.\n");
		}
		return;
	}

	if (_stricmp("PixelReadoutRate",name) == 0)
	{	
		WORD wRecState = 0;
		PCO_GetRecordingState(hCamera, &wRecState);
		if ( wRecState == 1)
		{
			mexErrMsgTxt("Camera recording state is RUN. The command is rejected.");
		}

		DWORD dwPixelRate;
		dwPixelRate = static_cast<DWORD>(getScalar(field));

		// check if is a valid pixel rate
		bool valid = false;
		int nelem = sizeof( cameraStates[camera-1].caminfo.dwPixelRateDESC) / sizeof(unsigned long);
		for (int i = 0; i < nelem; i++)
		{
			if ( dwPixelRate == cameraStates[camera-1].caminfo.dwPixelRateDESC[i] )
			{
				valid = true;
				break;
			}
		}

		if (valid)
		{
			MEXENTER;
			MEXMESSAGE(PCO_SetPixelRate(hCamera, dwPixelRate));
			MEXMESSAGE(PCO_ArmCamera(hCamera));
			MEXMESSAGE(PCO_GetPixelRate(hCamera, &cameraStates[camera-1].dwPixelRate));
			MEXLEAVE;
		}
		else
		{
			mexPrintf("Requested Pixel Readout Rate is not supported!\n");
			mexPrintf("Valid settings are:\n");
			for (int i = 0; i < nelem; i++)
			{
				mexPrintf("%d\n",cameraStates[camera-1].caminfo.dwPixelRateDESC[i]);
			}
		}
		return;
	}

	if (_stricmp("ShutterMode",name) == 0)
	{
		if (_strnicmp(cameraStates[camera-1].szCameraName, "pco.edge", 8) != 0)
		{
			mexErrMsgTxt("ShutterMode command is supported only by pco.edge camera!");
		}
		WORD wType, wLen = 10;
		DWORD dwSetup[10];
		int ts[3] = { 2000, 3000, 250}; // command, image, channel timeout
		MEXMESSAGE(PCO_GetCameraSetup(hCamera, &wType, &dwSetup[0], &wLen));
		DWORD mode;
		mode = static_cast<DWORD>(getScalar(field));
		if (mode == 1)
		{
			mode = PCO_EDGE_SETUP_ROLLING_SHUTTER;
		}
		else if (mode == 2)
		{
			if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_NO_GLOBAL_SHUTTER)
			{
				mexPrintf("Current camera (%s) does not support Global Shutter Mode!\n", cameraStates[camera-1].szCameraName);
				return;
			}
			mode = PCO_EDGE_SETUP_GLOBAL_SHUTTER;
		}
		else if (mode == 4)
		{
			if (cameraStates[camera-1].caminfo.dwGeneralCapsDESC1 & GENERALCAPS1_GLOBAL_RESET_MODE)
			{
				mexPrintf("Current camera (%s) does not support Global Reset Shutter Mode!\n", cameraStates[camera-1].szCameraName);
				return;
			}
			mode = PCO_EDGE_SETUP_GLOBAL_RESET;
		}
		else
		{
			mexPrintf("Requested Shutter Mode is not supported!\n");
			mexPrintf("Possible parameter values:\n1:rolling shutter, 2:global shutter, 4:global reset\n");
			return;
		}
		dwSetup[0] = mode;
		MEXENTER;
		MEXMESSAGE(PCO_SetTimeouts(hCamera, &ts[0], sizeof(ts)));
		MEXMESSAGE(PCO_SetCameraSetup(hCamera, wType, &dwSetup[0], wLen));
		MEXMESSAGE(PCO_RebootCamera(hCamera));
		MEXLEAVE;
		mexPrintf("You MUST unload and re-initialize camera driver after changing Shutter Mode!\n");
		return;
	}

	if (_stricmp("EnableSoftROI",name) == 0)
	{
		if (_strnicmp(cameraStates[camera-1].szCameraName, "pco.edge", 8) != 0)
		{
			mexErrMsgTxt("EnableSoftROI command is supported only by pco.edge camera!");
		}
		WORD wSoftROIFlag;
		wSoftROIFlag = static_cast<WORD>(getScalar(field));
		if ( (wSoftROIFlag != 0) && (wSoftROIFlag != 1) )
		{
			mexErrMsgTxt("EnableSoftROI parameter should be only 0 or 1!");
		}
		MEXMESSAGE(PCO_EnableSoftROI(hCamera, wSoftROIFlag, NULL, 0));
		cameraStates[camera-1].wSoftROI = wSoftROIFlag;
		return;
	}

	#ifndef NDEBUG
	mexPrintf("%s:%d - ",__FILE__,__LINE__);
	#endif
	mexPrintf("\"%s\" unknown.\n",name);
	return;
}
