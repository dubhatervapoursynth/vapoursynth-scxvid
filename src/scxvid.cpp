#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>
#include <xvid.h>
#include <VapourSynth4.h>
#include <VSHelper4.h>

#define SCXVID_BUFFER_SIZE (1024*1024*4)


static std::once_flag xvid_inited_flag;
static bool xvid_inited = false;


typedef struct {
   VSNode *node;
   const VSVideoInfo *vi;
   std::string prop_name;
   int use_slices;
   void *xvid_handle;
   xvid_enc_frame_t xvid_enc_frame;
   void *output_buffer;
   int last_frame;
   xvid_enc_create_t xvid_enc_create;
   bool processingRequest;
   std::vector<bool> keyframe_map;
} ScxvidData;


static const VSFrame *VS_CC scxvidGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
   ScxvidData *d = (ScxvidData *) instanceData;

   static constexpr const int frameGroupSize = 20;

   if (activationReason == arInitial) {
       d->processingRequest = (n > d->last_frame);
       if (d->processingRequest) {
           int end_frame = d->last_frame + std::min(n - d->last_frame, frameGroupSize);
           for (int frame = d->last_frame + 1; frame <= end_frame; frame++)
               vsapi->requestFrameFilter(frame, d->node, frameCtx);
       } else {
           vsapi->requestFrameFilter(n, d->node, frameCtx);
       }
   } else if (activationReason == arAllFramesReady) {
       if (d->processingRequest) {
           int end_frame = d->last_frame + std::min(n - d->last_frame, frameGroupSize);
           for (int frame = d->last_frame + 1; frame <= end_frame; frame++) {
               const VSFrame *src = vsapi->getFrameFilter(frame, d->node, frameCtx);
               xvid_enc_stats_t stats;
               stats.version = XVID_VERSION;

               for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
                   d->xvid_enc_frame.input.plane[plane] = (void *)vsapi->getReadPtr(src, plane);
                   d->xvid_enc_frame.input.stride[plane] = static_cast<int>(vsapi->getStride(src, plane));
               }

               d->xvid_enc_frame.length = SCXVID_BUFFER_SIZE;
               d->xvid_enc_frame.bitstream = d->output_buffer;

               int error = xvid_encore(d->xvid_handle, XVID_ENC_ENCODE, &d->xvid_enc_frame, &stats);
               if (error < 0) {
                   vsapi->setFilterError("Scxvid: xvid_encore returned an error code", frameCtx);
                   vsapi->freeFrame(src);
                   return nullptr;
               }

               vsapi->freeFrame(src);
               if (frame != n)
                   vsapi->releaseFrameEarly(d->node, frame, frameCtx);
               d->last_frame = frame;

               d->keyframe_map[frame] = (stats.type == XVID_TYPE_IVOP);
           }

           d->processingRequest = (n > d->last_frame);
           if (d->processingRequest) {
               int end_frame = d->last_frame + std::min(n - d->last_frame, frameGroupSize);
               for (int frame = d->last_frame + 1; frame <= end_frame; frame++)
                   vsapi->requestFrameFilter(frame, d->node, frameCtx);
               return nullptr;
           }
       }

      const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
      VSFrame *dst = vsapi->copyFrame(src, core);
      vsapi->freeFrame(src);

      VSMap *props = vsapi->getFramePropertiesRW(dst);
      vsapi->mapSetInt(props, d->prop_name.c_str(), d->keyframe_map.at(n), maAppend);

      return dst;
   }

   return nullptr;
}


static void VS_CC scxvidFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
   ScxvidData *d = (ScxvidData *)instanceData;
   vsapi->freeNode(d->node);

   free(d->output_buffer);
   xvid_encore(d->xvid_handle, XVID_ENC_DESTROY, nullptr, nullptr);

   delete d;
}


static void initializeXvid() {
    xvid_gbl_init_t xvid_init;
    memset(&xvid_init, 0, sizeof(xvid_init));
    xvid_init.version = XVID_VERSION;
    xvid_init.debug = ~0;
    xvid_inited = !xvid_global(nullptr, XVID_GBL_INIT, &xvid_init, nullptr);
}


static void VS_CC scxvidCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
   ScxvidData d;
   ScxvidData *data;
   int err;

   d.output_buffer = nullptr;
   d.last_frame = -1;
   d.node = vsapi->mapGetNode(in, "clip", 0, nullptr);
   d.vi = vsapi->getVideoInfo(d.node);

   if (!vsh::isSameVideoPresetFormat(pfYUV420P8, &d.vi->format, core, vsapi) || !vsh::isConstantVideoFormat(d.vi)) {
      vsapi->mapSetError(out, "Scxvid: only constant format YUV420P8 input supported");
      vsapi->freeNode(d.node);
      return;
   }

   const char *log = vsapi->mapGetData(in, "log", 0, &err);

   const char *prop_name = vsapi->mapGetData(in, "prop", 0, &err);
   if (err || !prop_name[0]) {
       d.prop_name = "_SceneChangePrev";
   } else {
       d.prop_name = prop_name;
   }

   d.use_slices = vsapi->mapGetIntSaturated(in, "use_slices", 0, &err);
   if (err) {
      // Enabled by default.
      d.use_slices = 1;
   }


   std::call_once(xvid_inited_flag, initializeXvid);

   if (!xvid_inited) {
       vsapi->freeNode(d.node);
       vsapi->mapSetError(out, "Scxvid: Failed to initialize Xvid");
       return;
   }

   xvid_gbl_info_t xvid_info;
   memset(&xvid_info, 0, sizeof(xvid_info));
   xvid_info.version = XVID_VERSION;
   int error = xvid_global(nullptr, XVID_GBL_INFO, &xvid_info, nullptr);
   if (error) {
       vsapi->freeNode(d.node);
       vsapi->mapSetError(out, "Scxvid: Failed to initialize Xvid");
       return;
   }

   memset(&d.xvid_enc_create, 0, sizeof(d.xvid_enc_create));
   d.xvid_enc_create.version = XVID_VERSION;
   d.xvid_enc_create.profile = 0;
   d.xvid_enc_create.width = d.vi->width;
   d.xvid_enc_create.height = d.vi->height;
   d.xvid_enc_create.num_threads = xvid_info.num_threads;
   if (d.use_slices)
       d.xvid_enc_create.num_slices = xvid_info.num_threads;
   d.xvid_enc_create.fincr = 1;
   d.xvid_enc_create.fbase = 1;
   d.xvid_enc_create.max_key_interval = 10000000; //huge number
   xvid_enc_plugin_t plugins[1];
   xvid_plugin_2pass1_t xvid_rc_plugin;
   memset(&xvid_rc_plugin, 0, sizeof(xvid_rc_plugin));
   xvid_rc_plugin.version = XVID_VERSION;
   xvid_rc_plugin.filename = (char *)log;
   plugins[0].func = xvid_plugin_2pass1;
   plugins[0].param = &xvid_rc_plugin;
   d.xvid_enc_create.plugins = plugins;
   d.xvid_enc_create.num_plugins = 1;

   error = xvid_encore(nullptr, XVID_ENC_CREATE, &d.xvid_enc_create, nullptr);
   if (error) {
       vsapi->freeNode(d.node);
       vsapi->mapSetError(out, "Scxvid: Failed to initialize Xvid encoder");
       return;
   }
   d.xvid_handle = d.xvid_enc_create.handle;

   //default identical(?) to xvid 1.1.2 vfw general preset
   memset(&d.xvid_enc_frame, 0, sizeof(d.xvid_enc_frame));
   d.xvid_enc_frame.version = XVID_VERSION;
   d.xvid_enc_frame.vol_flags = 0;
   d.xvid_enc_frame.vop_flags = XVID_VOP_MODEDECISION_RD
       | XVID_VOP_HALFPEL
       | XVID_VOP_HQACPRED
       | XVID_VOP_TRELLISQUANT
       | XVID_VOP_INTER4V;

   d.xvid_enc_frame.motion = XVID_ME_CHROMA_PVOP
       | XVID_ME_CHROMA_BVOP
       | XVID_ME_HALFPELREFINE16
       | XVID_ME_EXTSEARCH16
       | XVID_ME_HALFPELREFINE8
       | 0
       | XVID_ME_USESQUARES16;

   d.xvid_enc_frame.type = XVID_TYPE_AUTO;
   d.xvid_enc_frame.quant = 0;

   /*
    * NOT XVID_CSP_YV12, even though we are feeding it that,
    * because with XVID_CSP_YV12 it assumes the U plane
    * is located just after the Y plane, and the V plane
    * just after the U plane. This used to be the way
    * VapourSynth allocated the planes, before r316.
    *
    * With XVID_CSP_PLANAR it doesn't assume anything and
    * just uses whatever pointers we pass.
    */
   d.xvid_enc_frame.input.csp = XVID_CSP_PLANAR;

   if (!(d.output_buffer = malloc(SCXVID_BUFFER_SIZE))) {
       vsapi->freeNode(d.node);
       xvid_encore(d.xvid_handle, XVID_ENC_DESTROY, nullptr, nullptr);
       vsapi->mapSetError(out, "Scxvid: Failed to allocate buffer");
       return;
   }

   d.keyframe_map.resize(d.vi->numFrames);

   data = new ScxvidData();
   *data = d;

   VSFilterDependency deps[] = { {data->node, rpGeneral} };
   VSNode *outNode = vsapi->createVideoFilter2("Scxvid", data->vi, scxvidGetFrame, scxvidFree, fmFrameState, deps, 1, data, core);
   vsapi->setLinearFilter(outNode);
   vsapi->mapConsumeNode(out, "clip", outNode, maReplace);
   return;
}


VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.nodame.scxvid", "scxvid", "VapourSynth Scxvid Plugin", VS_MAKE_VERSION(3, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Scxvid", "clip:vnode;log:data:opt;use_slices:int:opt;prop:data:opt;", "clip:vnode;", scxvidCreate, 0, plugin);
}
