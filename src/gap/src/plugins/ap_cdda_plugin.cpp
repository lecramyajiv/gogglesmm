#include "ap_config.h"
#include "ap_defs.h"
#include "ap_pipe.h"
#include "ap_format.h"
#include "ap_device.h"
#include "ap_event.h"
#include "ap_event_private.h"
#include "ap_event_queue.h"
#include "ap_thread_queue.h"
#include "ap_memory_buffer.h"
#include "ap_packet.h"
#include "ap_engine.h"
#include "ap_input_plugin.h"
#include "ap_decoder_plugin.h"
#include "ap_thread.h"
#include "ap_input_thread.h"
#include "ap_memory_buffer.h"
#include "ap_decoder_thread.h"
#include "ap_output_thread.h"
#include "ap_cdda_plugin.h"

namespace ap {


  AudioCD::AudioCD() :drive(NULL) {
    }

  FXbool AudioCD::open(const FXString & cdrom) {

    if (!cdrom.empty()) {
      drive=cdio_cddap_identify(cdrom.text(),0,NULL);
      if (drive==NULL) {
        return false;
        }
      }
    else {
      drive = cdio_cddap_find_a_cdrom(0,NULL);
      if (drive==NULL) {
        return false;
        }
      }

    if (cdio_cddap_open(drive)){
      cdio_cddap_close(drive);
      return false;
      }

    ntracks = cdio_cddap_tracks(drive);
    if (ntracks==0){
      cdio_cddap_close(drive);
      return false;
      }

    fxmessage("%d tracks found\n",ntracks);

    track=1;
    sector=cdio_cddap_track_firstsector(drive,track);
    return true;
    }

  FXbool AudioCD::isSerial() const { return false; }

  /// Get current file position
  FXlong AudioCD::position() const {
    return sector;
    }

  FXlong AudioCD::position(FXlong offset,FXuint from) {
   // lsn_t           start=cdio_cddap_disc_firstsector(drive);
    //lsn_t             end=cdio_cddap_disc_firstsector(drive);
    sector=cdio_cddap_track_firstsector(drive,track)+offset;
    return 0;
    }

  FXival AudioCD::readBlock(void* data,FXival count) {

    if (sector>cdio_cddap_track_lastsector(drive,track))
      return 0;

    fxmessage("read sector %d %d %d\n",cdio_cddap_track_lastsector(drive,track),sector,count);

//    if ((sector+count)>(cdio_cddap_track_lastsector(drive,track)-1)) {
//      count=cdio_cddap_track_lastsector(drive,track)-sector+1;
//      }
    FXival n = cdio_cddap_read(drive,data,sector,count);
    if (n<=0) { fxmessage("oops\n"); return 0; }
//    if (n!=1) { fxmessage("NNN==%d\n",n); }
    sector+=n;
    return count;
    }

  FXlong AudioCD::size() {
    return cdio_cddap_track_lastsector(drive,track)-cdio_cddap_track_firstsector(drive,track);
    }

  FXbool AudioCD::eof() {
    if (sector>=cdio_cddap_track_lastsector(drive,track))
      return true;
    else
      return false;
    }

  AudioCD::~AudioCD(){
    if (drive)
      cdio_cddap_close(drive);
    }

  void AudioCD::setTrack(FXint n) {
    track=n;
    sector=cdio_cddap_track_firstsector(drive,track);
    }


class CDDAInput : public InputPlugin {
protected:
  FXuint datasize;    // size of the data section
  FXlong input_start;
  FXlong stream_position;
public:
  CDDAInput(AudioEngine*);
  FXbool init();
  InputStatus process(Packet*);

  FXbool can_seek() const;
  FXbool seek(FXdouble);
  virtual ~CDDAInput();
  };



CDDAInput::CDDAInput(AudioEngine*e) : InputPlugin(e) {
  }

CDDAInput::~CDDAInput(){
  }

FXbool CDDAInput::init() {
  datasize=0;
  input_start=0;
  flags=0;
  stream_position=0;
  return true;
  }

FXbool CDDAInput::can_seek() const {
  return true;
  }

FXbool CDDAInput::seek(FXdouble pos){
  stream_position=(pos*engine->input->size()*CDIO_CD_FRAMESIZE_RAW)/af.framesize();
  engine->input->position(pos*engine->input->size(),FXIO::Begin);
  return true;
  }

InputStatus CDDAInput::process(Packet * packet) {

  if (!(flags&FLAG_PARSED)) {
    fxmessage("sending configure\n");
    flags|=FLAG_PARSED;
    stream_position=0;
    af.set(AP_FORMAT_S16,44100,2);
    engine->decoder->post(new ConfigureEvent(af,Codec::PCM));
    }

  packet->clear();

  FXint n = packet->space() / CDIO_CD_FRAMESIZE_RAW;

  FXint nread = engine->input->read(packet->data(),n);
  if (nread) {
//    fxmessage("got %d\n",nread);
    packet->wrote(CDIO_CD_FRAMESIZE_RAW*nread);
    packet->af=af;
    packet->stream_position=stream_position;
    packet->stream_length=(engine->input->size()*CDIO_CD_FRAMESIZE_RAW)/af.framesize();
//    fxmessage("%ld/%ld\n",packet->stream_position,packet->stream_length);
    stream_position+=(CDIO_CD_FRAMESIZE_RAW/af.framesize());

    if (engine->input->eof())
      packet->flags=FLAG_EOS;
    else
      packet->flags=0;


/*
    for (FXint i=0;i<CDIO_CD_FRAMESIZE_RAW;i++)
      fxmessage("%x",packet->data()[i]);
    fxmessage("\n\n");
*/
    engine->decoder->post(packet);
    }
  else {
    return InputDone;
    }

  return InputOk;
  }

InputPlugin * ap_cdda_input(AudioEngine * engine) {
  return new CDDAInput(engine);
  }
}
