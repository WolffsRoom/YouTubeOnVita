/*
 * Honest Starboard 15 unsupported-feature contracts for the PS Vita port.
 * No DRM/CDM has been implemented, no microphone capture is wired to Vita
 * hardware and no TTS engine is bundled. Never signal success or fabricate
 * protected playback/recording capabilities.
 */
#include "starboard/accessibility.h"
#include "starboard/drm.h"
#include "starboard/microphone.h"
#include "starboard/speech_synthesis.h"
#include <cstring>

extern "C" {
bool SbAccessibilityGetTextToSpeechSettings(
    SbAccessibilityTextToSpeechSettings* out) {
  if(!out)return false;
  memset(out,0,sizeof(*out)); // has setting = false
  return true;
}
bool SbAccessibilityGetDisplaySettings(
    SbAccessibilityDisplaySettings* out) {
  if(!out)return false;
  memset(out,0,sizeof(*out)); // no global high-contrast option
  return true;
}
bool SbAccessibilityGetCaptionSettings(SbAccessibilityCaptionSettings* out) {
  if(!out)return false;
  memset(out,0,sizeof(*out)); // every state == Unsupported, supports=false
  return true;
}
bool SbAccessibilitySetCaptionsEnabled(bool enabled) {
  (void)enabled;
  return false; // Vita port cannot change system-wide caption policy
}
bool SbSpeechSynthesisIsSupported() {return false;}
void SbSpeechSynthesisSpeak(const char* text) {(void)text;}
void SbSpeechSynthesisCancel() {}

int SbMicrophoneGetAvailable(SbMicrophoneInfo* out,int capacity) {
  (void)out;
  return capacity<0?-1:0; // zero *supported by this port* microphones
}
bool SbMicrophoneIsSampleRateSupported(SbMicrophoneId id,int hz){
  (void)id;(void)hz;return false;
}
SbMicrophone SbMicrophoneCreate(SbMicrophoneId id,int hz,int bytes){
  (void)id;(void)hz;(void)bytes;return kSbMicrophoneInvalid;
}
bool SbMicrophoneOpen(SbMicrophone mic){(void)mic;return false;}
bool SbMicrophoneClose(SbMicrophone mic){(void)mic;return false;}
int SbMicrophoneRead(SbMicrophone mic,void* out,int count){
  (void)mic;(void)out;(void)count;return -1;
}
void SbMicrophoneDestroy(SbMicrophone mic){(void)mic;}

SbDrmSystem SbDrmCreateSystem(
    const char* key_system,void* context,
    SbDrmSessionUpdateRequestFunc update_request,
    SbDrmSessionUpdatedFunc session_updated,
    SbDrmSessionKeyStatusesChangedFunc key_status,
    SbDrmServerCertificateUpdatedFunc cert_updated,
    SbDrmSessionClosedFunc session_closed){
  (void)key_system;(void)context;(void)update_request;
  (void)session_updated;(void)key_status;(void)cert_updated;
  (void)session_closed;
  return kSbDrmSystemInvalid; // no CDM/Widevine installed or advertised
}
void SbDrmGenerateSessionUpdateRequest(SbDrmSystem drm,int ticket,
    const char* type,const void* initialization_data,int length){
  (void)drm;(void)ticket;(void)type;(void)initialization_data;(void)length;
  // This is unreachable for a valid system, because Create always fails.
}
void SbDrmUpdateSession(SbDrmSystem drm,int ticket,
    const void* key,int key_size,const void* session_id,int session_size){
  (void)drm;(void)ticket;(void)key;(void)key_size;
  (void)session_id;(void)session_size;
}
void SbDrmCloseSession(SbDrmSystem drm,const void* session_id,int length){
  (void)drm;(void)session_id;(void)length;
}
bool SbDrmIsServerCertificateUpdatable(SbDrmSystem drm){
  (void)drm;return false;
}
void SbDrmUpdateServerCertificate(SbDrmSystem drm,int ticket,
    const void* cert,int cert_size){
  (void)drm;(void)ticket;(void)cert;(void)cert_size;
}
const void* SbDrmGetMetrics(SbDrmSystem drm,int* size){
  (void)drm;if(size)*size=0;return nullptr;
}
void SbDrmDestroySystem(SbDrmSystem drm){(void)drm;}
}

