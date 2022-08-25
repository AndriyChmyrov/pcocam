# pcocam
Matlab MEX wrapper written in C/C++ (MS Visual Studio) for PCO Edge cameras (tested on Edge 4.2 and Edge 4.2 CLHS)

Requires PCO SDK (SC2_Cam.lib and sc2*.h files)

Syntaxis:
~~~Matlab
pcocam('setParameterCommand',value); 	% set Parameter to provided value or execute command
result = pcocam('getParameterCommand');	% get Parameter value or get command output
~~~

| Parameters/Commands:		| Call type |
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
pcocam('ExposureTime',0.1);        % set camera exposure time
exptime = pcocam('ExposureTime');  % get camera exposure time
pcocam('FrameCount',5);	           % prepare camera to acquire 5 frames next time
frames = pcocam('AcquireFrames')   % starts image acquisition, waits until finished and returns all frames as 3D array
figure
imagesc(rot90(frames(:,:,1),-1))   % display the first of the acquired frames
axis image, colormap hot
%
pcocam('TriggerMode',2);           % set camera to "external exposure start & software" trigger mode
pcocam('AcquisitionStart');        % starts the acquisition, but the camera is waiting for the external trigger
% start triggering
frames = pcocam('AcquisitionStop') % stops the acquisition and returns the acquired frames as 3D array [x,y,frame_number]
~~~