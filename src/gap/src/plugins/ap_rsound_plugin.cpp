#include "ap_defs.h"
#include "ap_config.h"
#include "ap_pipe.h"
#include "ap_event.h"
#include "ap_format.h"
#include "ap_device.h"
#include "ap_buffer.h"
#include "ap_packet.h"
#include "ap_event_queue.h"
#include "ap_thread_queue.h"
#include "ap_format.h"
#include "ap_engine.h"
#include "ap_thread.h"
#include "ap_input_plugin.h"
#include "ap_output_plugin.h"
#include "ap_decoder_plugin.h"
#include "ap_decoder_thread.h"
#include "ap_rsound_plugin.h"

using namespace ap;


extern "C" GMAPI OutputPlugin * ap_load_plugin() {
  return new RSoundOutput();
  }

extern "C" GMAPI void ap_free_plugin(OutputPlugin* plugin) {
  delete plugin;
  }

namespace ap {



static FXbool to_gap_format(const FXint rsd,AudioFormat & af) {
  switch(rsd){
    case RSD_U8          : af.format=AP_FORMAT_U8;      break;
    case RSD_S8          : af.format=AP_FORMAT_S8;      break;
    case RSD_S16_NE      : af.format=AP_FORMAT_S16;     break;
    case RSD_S16_LE      : af.format=AP_FORMAT_S16_LE;  break;
    case RSD_S16_BE      : af.format=AP_FORMAT_S16_BE;  break;
    default              : return false; break;
    }
  return true;
  }

static FXbool to_rsd_format(const AudioFormat & af,FXint & rsd){
  switch(af.format) {
    case AP_FORMAT_S8       : rsd=RSD_S8;     break;
    case AP_FORMAT_S16_LE   : rsd=RSD_S16_LE; break;
    case AP_FORMAT_S16_BE   : rsd=RSD_U16_BE; break;
    default                 : return false; break;
    }
  return true;
  }

RSoundOutput::RSoundOutput() : OutputPlugin(), rsd(NULL) {
  }

RSoundOutput::~RSoundOutput() {
  close();
  }

FXbool RSoundOutput::open() {
  if (rsd_init(&rsd)==0)
    return true;
  return false;
  }

void RSoundOutput::close() {
  if (rsd) {
    rsd_free(rsd);
    rsd=NULL;
    }
  af.reset();
  }


void RSoundOutput::volume(FXfloat v) {
  }

FXint RSoundOutput::delay() {
  FXASSERT(rsd);
  return rsd_delay(rsd) / af.framesize();
  }

void RSoundOutput::drop() {
  rsd_stop(rsd);
  }

void RSoundOutput::drain() {
  rsd_stop(rsd);
  }

void RSoundOutput::pause(FXbool) {
  }

FXbool RSoundOutput::configure(const AudioFormat & fmt){
  int rsd_format;
  int rsd_rate      = fmt.rate;
  int rsd_channels  = fmt.channels;

  if (__unlikely(rsd==NULL) && !open()) {
    goto failed;
    }


  if (!to_rsd_format(fmt,rsd_format))
    goto failed;

  rsd_set_param(rsd,RSD_FORMAT,&rsd_format);
  rsd_set_param(rsd,RSD_SAMPLERATE,&rsd_rate);
  rsd_set_param(rsd,RSD_CHANNELS,&rsd_channels);

  rsd_start(rsd);

  af=fmt;
  return true;

failed:
  af.reset();
  return false;
  }



FXbool RSoundOutput::write(const void * b,FXuint nframes){
  if (rsd_write(rsd,b,nframes*af.framesize())==0)
    return false;
  return true;
  }
}

