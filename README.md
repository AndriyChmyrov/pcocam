# pcocam
Matlab mex driver for PCO cameras (tested on Edge4.2)

Syntaxis:
pcocam('setParameterCommand',value);
result = pcocam('getParameterCommand');

Example:
pcocam('ExposureTime',0.1);
exptime = pcocam('ExposureTime');
pcocam('FrameCount',5);
frames = pcocam('AcquireFrames')
