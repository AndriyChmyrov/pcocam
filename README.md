# pcocam
Matlab mex driver for PCO Edge cameras (tested on Edge 4.2 and Edge 4.2 CLHS)

Syntaxis:
~~~Matlab
pcocam('setParameterCommand',value); 	% set Parameter to value or execute command
result = pcocam('getParameterCommand');	% get Parameter value or get command output
~~~

| Parameter/Commands:		| Call type |
| :---						| :----:	|
| AOIHeight					| get only	|
| AOIWidth					| get only	|
| Acquire					| set only	|
| AcquireFrames				| get only	|
| AcquisitionStart			| get only	|
| AcquisitionStop			| get only	|
| AcquisitionTimeoutSec		| set&get	|
| ActiveLookupTable			| get only	|
| ArmCamera					| get only	|
| CameraHealthStatus		| get only	|
| CameraHealthStatus		| get only	|
| CameraType				| get only	|
| EnableSoftROI				| set&get	|
| ExposureTime				| set&get	|
| FrameCount				| set&get	|
| FrameRate					| set&get	|
| Frames					| get only	|
| HotPixelCorrectionMode	| set&get	|
| NoiseFilterMode			| set&get	|
| PixelReadoutRate			| set&get	|
| ROI						| set&get	|
| RebootCamera				| get only	|
| SensorHeight				| get only	|
| SensorSize				| get only	|
| SensorTemperature			| get only	|
| SensorWidth				| get only	|
| ShutterMode				| set&get	|
| TriggerMode				| set&get	|


**Example:**
~~~Matlab
pcocam('ExposureTime',0.1);
exptime = pcocam('ExposureTime');
pcocam('FrameCount',5);
frames = pcocam('AcquireFrames')
~~~