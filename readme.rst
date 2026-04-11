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
        The property name to assign the output to.

The *log* parameter is optional, because the ``_SceneChangePrev`` property
will be attached to every frame. Thus some users may not need xvid's log file.

For correct scene change detection, one must request all the frames, starting
at 0, strictly in ascending order. It's probably best if Scxvid is the last
filter in the chain.
