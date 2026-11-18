from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
needle='port_constants.cpp")'
assert needle in s
s=s.replace(needle,'port_constants.cpp" "'+chr(36)+'{VITA}/port_optional_unsupported.cpp")')
cm.write_text(s,encoding='utf-8')
lookup=r/'experiments/cobalt/vita/import_lookup.cpp'
s=lookup.read_text(encoding='utf-8-sig')
needle='#include "starboard/event.h"'
assert needle in s
s=s.replace(needle,needle+'''
#include "starboard/accessibility.h"
#include "starboard/microphone.h"
#include "starboard/speech_synthesis.h"
#include "starboard/drm.h"''')
names=[
'SbAccessibilityGetTextToSpeechSettings',
'SbAccessibilityGetDisplaySettings','SbAccessibilityGetCaptionSettings',
'SbAccessibilitySetCaptionsEnabled','SbSpeechSynthesisIsSupported',
'SbSpeechSynthesisSpeak','SbSpeechSynthesisCancel','SbMicrophoneGetAvailable',
'SbMicrophoneIsSampleRateSupported','SbMicrophoneCreate','SbMicrophoneOpen',
'SbMicrophoneClose','SbMicrophoneRead','SbMicrophoneDestroy',
'SbDrmCreateSystem','SbDrmGenerateSessionUpdateRequest','SbDrmUpdateSession',
'SbDrmCloseSession','SbDrmIsServerCertificateUpdatable',
'SbDrmUpdateServerCertificate','SbDrmGetMetrics','SbDrmDestroySystem']
needed=set((r/'experiments/cobalt/evergreen/external_requirements.txt').read_text().split())
assert set(names)<=needed,set(names)-needed
needle='    {"SbLog",'
assert needle in s
s=s.replace(needle,''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in names)+needle)
lookup.write_text(s,encoding='utf-8')
test=r/'experiments/cobalt/vita/main_test.cpp'
s=test.read_text(encoding='utf-8-sig')
needle='#include "starboard/event.h"'
assert needle in s
s=s.replace(needle,needle+'''
#include "starboard/accessibility.h"
#include "starboard/microphone.h"
#include "starboard/speech_synthesis.h"
#include "starboard/drm.h"''')
marker='  char result[96];'
assert marker in s
checks=r'''  SbAccessibilityTextToSpeechSettings tts={};
  check(SbAccessibilityGetTextToSpeechSettings(&tts) &&
        !tts.has_text_to_speech_setting,
        "Vita reports no system-wide text-to-speech settings");
  SbAccessibilityDisplaySettings display={};
  check(SbAccessibilityGetDisplaySettings(&display) &&
        !display.has_high_contrast_text_setting,
        "Vita reports no system-wide high-contrast text setting");
  SbAccessibilityCaptionSettings captions={};
  check(SbAccessibilityGetCaptionSettings(&captions) &&
        captions.background_color_state==
           kSbAccessibilityCaptionStateUnsupported &&
        !captions.supports_is_enabled,
        "Vita reports unsupported system-level caption preferences");
  check(!SbAccessibilitySetCaptionsEnabled(true),
        "Vita refuses to claim captions were changed system-wide");
  check(!SbSpeechSynthesisIsSupported(),
        "Vita does not advertise an unimplemented speech synthesizer");
  check(SbMicrophoneGetAvailable(nullptr,0)==0 &&
        !SbMicrophoneIsSampleRateSupported(nullptr,44100) &&
        !SbMicrophoneIsValid(SbMicrophoneCreate(nullptr,44100,4096)),
        "Vita does not falsely advertise microphone capture support");
  check(SbDrmCreateSystem("com.widevine.alpha",nullptr,nullptr,
         nullptr,nullptr,nullptr,nullptr)==kSbDrmSystemInvalid,
        "Vita refuses unsupported Widevine DRM initialization");
  int drm_metrics=-1;
  check(SbDrmGetMetrics(nullptr,&drm_metrics)==nullptr && drm_metrics==0,
        "unsupported DRM returns no fabricated metrics");
'''
s=s.replace(marker,checks+marker)
test.write_text(s,encoding='utf-8')
print('TRUE_NEGATIVE_OPTIONAL_API_EXPORTS',len(names),'CHECKS_ADDED',8)

