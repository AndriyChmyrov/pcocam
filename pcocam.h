#ifndef NDEBUG
 #define DEBUG(text) mexPrintf("%s:%d - %s\n",__FILE__,__LINE__,text);	// driver actions

 #define MEXMESSAGE(error) mexMessage(__FILE__,__LINE__,error)
 #define MEXENTER mexEnter(__FILE__,__LINE__)
#else
 #ifdef DEBUG
  #undef DEBUG
 #endif
 #define DEBUG(text)

 #define MEXMESSAGE(error) mexMessage(error)
 #define MEXENTER mexEnter()
#endif
#define MEXLEAVE mexLeave()

#define PCO_ERRT_H_CREATE_OBJECT
#define DO_THE_EDGE
#define PCO_NUM_BUFFERS ((WORD)15) // max number for "pco.edge 4.2" = 15

const WORD StringLength = 64;
const WORD MaxNumberOfBuffers = PCO_NUM_BUFFERS;

typedef struct
{	HANDLE	handle;			// camera handle
	WORD	max_xsize;		// max horizontal pixel size
	WORD	max_ysize;		// max vertical pixel size
	WORD	xsize;			// set horizontal pixel size
	WORD	ysize;			// set vertical pixel size 
	WORD	wSoftROI;		// SoftROI status - 0 or 1
	WORD	wBitPerPixel;	// Bits per pixel - for determining correct buffer size
	DWORD	ImageSizeBytes;	// size of an image in bytes
	//WORD	ExposureNs;		// exposure time in ns
	WORD	NumberOfFrames;	// number of frames in kinetic serie
	WORD	accumulations;		// number of accumulations in one frame
	WORD	NumberOfBuffers;	// number of buffers when reading out
	WORD**	AcqBuffers;		// Memory buffers to store incoming frames
	HANDLE* BufferEvent;	// Array of handles to BufferEvent
	UINT8*	ImageBuffer;	// Pointer to image buffer filled by BufferWatcher function
	HANDLE	BufferWatcherThread;
	WORD NumberOfFramesReceived;	// number of frames which are alewady read out from Buffers
	UINT8	AcquisitionTimeoutSec;	// Acquisition timeout in seconds, used at 'AcquisitionStop'
	bool	bAcquisitionStarted;	// flag to use in AcquisitionStart/AcquisitionStop commands (fool check)
	PCO_Description caminfo;
	PCO_CameraType	strCamType;
	char* szCameraName;		// camera name as string
	DWORD dwPixelRate;		// Pixel Readout Rate [Hz]
	_PCO_SC2_CL_TRANSFER_PARAMS TransferParam;
	_PCO_CLHS_TRANSFER_PARAMS	TransferParamHS;
} cameraState;


extern SHORT camera, cameras;		// Cameras
extern cameraState* cameraStates;

#ifndef NDEBUG
 void mexEnter(const char* file, const int line);
 void mexMessage(const char* file, const int line, unsigned error);
#else
 void mexEnter(void);
 void mexMessage(unsigned error);
#endif
void mexLeave(void);
void mexCleanup(void);

void createBuffers(void);
void deleteBuffers(void);

double	getScalar(const mxArray* array);
mxArray*	getParameter(const char* name);
void	setParameter(const char* name, const mxArray* field);
