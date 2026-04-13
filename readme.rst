Description
===========

Scene change detection plugin for VapourSynth, using xvidcore.

Converted from the Avisynth plugin written by Fredrik Mellbin.


Usage
=====
::

    scxvid.Scxvid(vnode clip[, string log="", bint use_slices=True, prop="_SceneChangePrev"])

Parameters:
    *clip*
        Input clip. Must be YUV420P8 with constant format and dimensions.

    *log*
        Name of the xvid first pass log file.

    *use_slices*
        This should make Scxvid faster, at the cost of slight differences in
        the scene change detection.
        
    *prop*
        The property name to store scene change information in.

The *log* parameter is optional because the ``_SceneChangePrev`` property
will be attached to every frame. Thus some users may not need xvid's log file.

Note that seeking can be slow since the filter needs to process every frame 
from the start up to the requested frame number.
