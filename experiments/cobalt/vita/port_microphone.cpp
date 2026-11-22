/*
 * Starboard 15 microphone on the PS Vita built-in mic (sceAudioIn), for
 * voice search. Same capture model as DeltaruneVita's native_mic.c: the
 * blocking sceAudioInInput runs on its own thread (VOICE port, 16 kHz mono
 * S16, 256-sample grain) and fills a ring buffer that SbMicrophoneRead
 * drains without blocking. The port is open only between Open and Close;
 * PCM is never written to storage.
 */
#include "starboard/microphone.h"
#include "starboard/log.h"
// The existing UI SFX worker handles short cues asynchronously.
extern "C" void ytv_ui_sound(int kind);
#include <psp2/audioin.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <cstring>
#include <cstdio>

struct SbMicrophonePrivate {
  int buffer_bytes;
  int16_t* ring;
  int ring_samples, read_pos, count;
  bool open;
  volatile bool stop;
  pthread_t thread;
  bool thread_running;
  int port;
  pthread_mutex_t lock;
  int errors;
};

// A single built-in microphone; its id is the address of this object.
struct SbMicrophoneIdPrivate { int unused; };
static SbMicrophoneIdPrivate g_builtin_id;

namespace {
constexpr int kRate = 16000;
constexpr int kGrain = 256;
SbMicrophone g_current = nullptr;
pthread_mutex_t g_global = PTHREAD_MUTEX_INITIALIZER;

int capture_thread(SceSize, void* argp) {
  SbMicrophone m = *static_cast<SbMicrophone*>(argp);
  int16_t pcm[kGrain];
  while (!m->stop) {
    if (sceAudioInInput(m->port, pcm) < 0) {
      pthread_mutex_lock(&m->lock);
      m->errors++;
      pthread_mutex_unlock(&m->lock);
      sceKernelDelayThread(16000);
      continue;
    }
    pthread_mutex_lock(&m->lock);
    for (int i = 0; i < kGrain; i++) {
      int w = (m->read_pos + m->count) % m->ring_samples;
      m->ring[w] = pcm[i];
      if (m->count < m->ring_samples) m->count++;
      else m->read_pos = (m->read_pos + 1) % m->ring_samples;  // overwrite oldest
    }
    pthread_mutex_unlock(&m->lock);
  }
  return 0;
}
// pthread entry: pthread mutexes are only safe on threads pthread created
// (a raw kernel thread crashed inside pthread-embedded on hardware).
void* capture_pthread(void* arg) {
  sceKernelChangeThreadPriority(0, 96);
  SbMicrophone m = static_cast<SbMicrophone>(arg);
  capture_thread(sizeof(m), &m);
  return nullptr;
}
}  // namespace

extern "C" {
int SbMicrophoneGetAvailable(SbMicrophoneInfo* out, int capacity) {
  if (capacity < 0) return -1;
  if (out && capacity >= 1) {
    memset(&out[0], 0, sizeof(out[0]));
    out[0].id = &g_builtin_id;
    out[0].type = kSbMicrophoneUnknown;  // built-in console microphone
    out[0].max_sample_rate_hz = kRate;
    out[0].min_read_size = kGrain * 2;
    snprintf(out[0].label, sizeof(out[0].label), "PS Vita microphone");
  }
  return 1;
}
bool SbMicrophoneIsSampleRateSupported(SbMicrophoneId id, int hz) {
  return id == &g_builtin_id && hz == kRate;
}
SbMicrophone SbMicrophoneCreate(SbMicrophoneId id, int hz, int buffer_bytes) {
  if (id != &g_builtin_id || hz != kRate || buffer_bytes < kGrain * 2) return kSbMicrophoneInvalid;
  pthread_mutex_lock(&g_global);
  if (g_current) { pthread_mutex_unlock(&g_global); return kSbMicrophoneInvalid; }
  SbMicrophone m = new SbMicrophonePrivate();
  m->buffer_bytes = buffer_bytes;
  m->ring_samples = buffer_bytes / 2;
  m->ring = new int16_t[m->ring_samples];
  m->thread_running = false;
  m->port = -1;
  pthread_mutex_init(&m->lock, nullptr);
  g_current = m;
  pthread_mutex_unlock(&g_global);
  return m;
}
bool SbMicrophoneOpen(SbMicrophone m) {
  if (!m) return false;
  if (m->open) return true;
  m->port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE, kGrain, kRate,
                               SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
  if (m->port < 0) {
    char line[96];
    snprintf(line, sizeof(line), "YTV_MIC open failed 0x%x\n", (unsigned)m->port);
    SbLogRaw(line);
    m->port = -1;
    return false;
  }
  m->read_pos = m->count = 0;
  m->stop = false;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 0x4000);
  int r = pthread_create(&m->thread, &attr, capture_pthread, m);
  pthread_attr_destroy(&attr);
  if (r != 0) {
    sceAudioInReleasePort(m->port);
    m->port = -1;
    return false;
  }
  m->thread_running = true;
  m->open = true;
  // Only after sceAudioIn successfully opened: do not beep for denied or
  // failed microphone requests. UI Sounds OFF suppresses this automatically.
  SbLogRaw("YTV_MIC_ACTIVE signal=recording audio_cue=7\n");
  ytv_ui_sound(7);
  return true;
}
bool SbMicrophoneClose(SbMicrophone m) {
  if (!m) return false;
  if (!m->open) return true;
  m->stop = true;
  if (m->thread_running) pthread_join(m->thread, nullptr);
  m->thread_running = false;
  sceAudioInReleasePort(m->port);
  m->port = -1;
  m->open = false;
  // Sound on microphone cancellation / capture close, not on failed opens.
  ytv_ui_sound(8);
  SbLogRaw("YTV_MIC_INACTIVE audio_cue=8\n");
  pthread_mutex_lock(&m->lock);
  m->read_pos = m->count = 0;
  pthread_mutex_unlock(&m->lock);
  return true;
}
int SbMicrophoneRead(SbMicrophone m, void* out, int bytes) {
  if (!m || !out || bytes < 0) return -1;
  if (!m->open) return -1;
  pthread_mutex_lock(&m->lock);
  int samples = bytes / 2;
  if (samples > m->count) samples = m->count;
  int16_t* dst = static_cast<int16_t*>(out);
  for (int i = 0; i < samples; i++) dst[i] = m->ring[(m->read_pos + i) % m->ring_samples];
  m->read_pos = (m->read_pos + samples) % m->ring_samples;
  m->count -= samples;
  pthread_mutex_unlock(&m->lock);
  return samples * 2;
}
void SbMicrophoneDestroy(SbMicrophone m) {
  if (!m) return;
  SbMicrophoneClose(m);
  pthread_mutex_lock(&g_global);
  if (g_current == m) g_current = nullptr;
  pthread_mutex_unlock(&g_global);
  pthread_mutex_destroy(&m->lock);
  delete[] m->ring;
  delete m;
}
}  // extern "C"
