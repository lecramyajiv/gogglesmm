/*******************************************************************************
*                         Goggles Audio Player Library                         *
********************************************************************************
*           Copyright (C) 2010-2012 by Sander Jansen. All Rights Reserved      *
*                               ---                                            *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU General Public License as published by         *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
* This program is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                *
* GNU General Public License for more details.                                 *
*                                                                              *
* You should have received a copy of the GNU General Public License            *
* along with this program.  If not, see http://www.gnu.org/licenses.           *
********************************************************************************/
#include "ap_defs.h"
#include "ap_config.h"
#include "ap_event.h"
#include "ap_pipe.h"
#include "ap_event_queue.h"
#include "ap_thread_queue.h"
#include "ap_format.h"
#include "ap_device.h"
#include "ap_buffer.h"
#include "ap_packet.h"
#include "ap_engine.h"
#include "ap_thread.h"
#include "ap_input_plugin.h"
#include "ap_output_plugin.h"
#include "ap_decoder_plugin.h"
#include "ap_decoder_thread.h"

#include "ap_pulse_plugin.h"

using namespace ap;

extern "C" GMAPI OutputPlugin * ap_load_plugin() {
  return new PulseOutput();
  }

extern "C" GMAPI void ap_free_plugin(OutputPlugin* plugin) {
  delete plugin;
  }



namespace ap {



PulseOutput::PulseOutput() : OutputPlugin(), mainloop(NULL),context(NULL),stream(NULL) {
  }

PulseOutput::~PulseOutput() {
  close();
  }


static FXbool to_pulse_format(const AudioFormat & af,pa_sample_format & pulse_format){
  switch(af.format) {
    case AP_FORMAT_S16_LE   : pulse_format=PA_SAMPLE_S16LE; break;
    case AP_FORMAT_S16_BE   : pulse_format=PA_SAMPLE_S16BE; break;
    case AP_FORMAT_S24_LE   : pulse_format=PA_SAMPLE_S24_32LE;  break;
    case AP_FORMAT_S24_BE   : pulse_format=PA_SAMPLE_S24_32BE;  break;
    case AP_FORMAT_S24_3LE  : pulse_format=PA_SAMPLE_S24LE;  break;
    case AP_FORMAT_S24_3BE  : pulse_format=PA_SAMPLE_S24BE;  break;
    case AP_FORMAT_FLOAT_LE : pulse_format=PA_SAMPLE_FLOAT32LE;  break;
    case AP_FORMAT_FLOAT_BE : pulse_format=PA_SAMPLE_FLOAT32BE;  break;
    default                 : return false; break;
    }
  return true;
  }


static FXbool to_gap_format(pa_sample_format pulse_format,AudioFormat & af){
  switch(pulse_format) {
    case PA_SAMPLE_U8       : af.format = AP_FORMAT_U8;       break;
    case PA_SAMPLE_S16LE    : af.format = AP_FORMAT_S16_LE;   break;
    case PA_SAMPLE_S16BE    : af.format = AP_FORMAT_S16_BE;   break;
    case PA_SAMPLE_S24LE    : af.format = AP_FORMAT_S24_3LE;   break;
    case PA_SAMPLE_S24BE    : af.format = AP_FORMAT_S24_3BE;   break;
    case PA_SAMPLE_S24_32LE : af.format = AP_FORMAT_S24_LE;  break;
    case PA_SAMPLE_S24_32BE : af.format = AP_FORMAT_S24_BE;  break;
    case PA_SAMPLE_FLOAT32LE: af.format = AP_FORMAT_FLOAT_LE; break;
    case PA_SAMPLE_FLOAT32BE: af.format = AP_FORMAT_FLOAT_BE; break;
    default                 : return false;
    }
  return true;
  }

static void context_state_callback(pa_context *c,void*ptr){
  pa_threaded_mainloop * mainloop = (pa_threaded_mainloop*)ptr;
  switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal(mainloop,0);
      break;
    default: break;
    }
  }

static void stream_state_callback(pa_stream *s,void*ptr){
  pa_threaded_mainloop * mainloop = (pa_threaded_mainloop*)ptr;
  switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY:
    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
      pa_threaded_mainloop_signal(mainloop, 0);
      break;
    default: break;
    }
  }

static void stream_write_callback(pa_stream*,size_t,void *ptr){
  pa_threaded_mainloop * mainloop = (pa_threaded_mainloop*)ptr;
  pa_threaded_mainloop_signal(mainloop,0);
  }



FXbool PulseOutput::open() {

  /// Start the mainloop
  if (mainloop==NULL) {
    mainloop = pa_threaded_mainloop_new();
    pa_threaded_mainloop_start(mainloop);
    }

  pa_threaded_mainloop_lock(mainloop);

  /// Get a context
  if (context==NULL) {
    context = pa_context_new(pa_threaded_mainloop_get_api(mainloop),"Goggles Music Manager");
    pa_context_set_state_callback(context,context_state_callback,mainloop);
    }

  /// Try connecting
  if (pa_context_get_state(context)==PA_CONTEXT_UNCONNECTED) {
    if (pa_context_connect(context,NULL,PA_CONTEXT_NOFLAGS,NULL)<0) {
      GM_DEBUG_PRINT("pa_context_connect failed\n");
      pa_threaded_mainloop_unlock(mainloop);
      return false;
      }
    }

  /// Wait until we're connected to the pulse daemon
  pa_context_state_t state;
  while((state=pa_context_get_state(context))!=PA_CONTEXT_READY) {
    if (state==PA_CONTEXT_FAILED || state==PA_CONTEXT_TERMINATED){
      GM_DEBUG_PRINT("Unable to connect to pulsedaemon\n");
      pa_threaded_mainloop_unlock(mainloop);
      return false;
      }
    pa_threaded_mainloop_wait(mainloop);
    }

  pa_threaded_mainloop_unlock(mainloop);
  return true;
  }

void PulseOutput::close() {
  if (mainloop)
    pa_threaded_mainloop_lock(mainloop);

  if (stream) {
    GM_DEBUG_PRINT("disconnecting stream\n");
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream=NULL;
    }

  if (context) {
    GM_DEBUG_PRINT("disconnecting context\n");
    pa_context_disconnect(context);
    pa_context_unref(context);
    context=NULL;
    }

  if (mainloop) {
    GM_DEBUG_PRINT("disconnecting mainloop\n");
    pa_threaded_mainloop_unlock(mainloop);
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
    mainloop=NULL;
    }
  af.reset();
  }




void PulseOutput::volume(FXfloat v) {
  if (mainloop && context && stream) {
    pa_threaded_mainloop_lock(mainloop);
    pa_cvolume cvol;
    pa_cvolume_set(&cvol,af.channels,pa_sw_volume_from_linear((FXdouble)v));
    pa_operation* operation = pa_context_set_sink_input_volume(context,pa_stream_get_index(stream),&cvol,NULL,NULL);
    pa_operation_unref(operation);
    pa_threaded_mainloop_unlock(mainloop);
    }
  }

FXint PulseOutput::delay() {
  FXint value=0;
  if (stream) {
    pa_usec_t latency;
    int negative;
    pa_threaded_mainloop_lock(mainloop);
    if (pa_stream_get_latency(stream,&latency,&negative)>=0){
      value = (latency*af.rate) / 1000000;
      }
    pa_threaded_mainloop_unlock(mainloop);
    }
  return value;
  }

void PulseOutput::drop() {
  if (stream) {
    pa_threaded_mainloop_lock(mainloop);
    pa_operation* operation = pa_stream_flush(stream,NULL,0);
    pa_operation_unref(operation);
    pa_threaded_mainloop_unlock(mainloop);
    }
  }


static void drain_callback(pa_stream*,int,void *ptr) {
  pa_threaded_mainloop * mainloop = (pa_threaded_mainloop*)ptr;
  pa_threaded_mainloop_signal(mainloop,0);
  }


void PulseOutput::drain() {
  if (stream) {
    pa_threaded_mainloop_lock(mainloop);
    pa_operation * operation = pa_stream_drain(stream,drain_callback,mainloop);
    while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
      pa_threaded_mainloop_wait(mainloop);
    pa_operation_unref(operation);
    pa_threaded_mainloop_unlock(mainloop);
    }
  }

void PulseOutput::pause(FXbool) {
  }

FXbool PulseOutput::configure(const AudioFormat & fmt){
  const pa_sample_spec * config=NULL;

  if (!open())
    return false;

  if (stream && fmt==af)
    return true;

  pa_threaded_mainloop_lock(mainloop);

  if (stream) {
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
    stream=NULL;
    }

  pa_sample_spec spec;

  if (!to_pulse_format(fmt,spec.format))
    goto failed;

  spec.rate     = fmt.rate;
  spec.channels = fmt.channels;

  stream = pa_stream_new(context,"Goggles Music Manager",&spec,NULL);
  pa_stream_set_state_callback(stream,stream_state_callback,mainloop);
  pa_stream_set_write_callback(stream,stream_write_callback,mainloop);

  if (pa_stream_connect_playback(stream,NULL,NULL,PA_STREAM_NOFLAGS,NULL,NULL)<0)
    goto failed;

  /// Wait until stream is ready
  pa_stream_state_t state;
  while((state=pa_stream_get_state(stream))!=PA_STREAM_READY) {
    if (state==PA_STREAM_FAILED || state==PA_STREAM_TERMINATED){
      goto failed;
      }
    pa_threaded_mainloop_wait(mainloop);
    }

  /// Get Actual Format
  config = pa_stream_get_sample_spec(stream);
  if (!to_gap_format(config->format,af))
    goto failed;
  af.channels=config->channels;
  af.rate=config->rate;

  pa_threaded_mainloop_unlock(mainloop);
  return true;
failed:
  GM_DEBUG_PRINT("Unsupported pulse configuration:\n");
  af.debug();
  pa_threaded_mainloop_unlock(mainloop);
  return false;
  }


FXbool PulseOutput::write(const void * b,FXuint nframes){
  FXASSERT(stream);
  const FXchar * buffer = reinterpret_cast<const FXchar*>(b);

  pa_threaded_mainloop_lock(mainloop);
  FXuint total = nframes*af.framesize();
  while(total) {
    size_t nbytes = pa_stream_writable_size(stream);
    size_t n = FXMIN(total,nbytes);
    if (n<=0) {
      pa_threaded_mainloop_wait(mainloop);
      continue;
      }
    pa_stream_write(stream,buffer,n,NULL,0,PA_SEEK_RELATIVE);
    total-=n;
    buffer+=n;
    }
  pa_threaded_mainloop_unlock(mainloop);
  return true;
  }

}