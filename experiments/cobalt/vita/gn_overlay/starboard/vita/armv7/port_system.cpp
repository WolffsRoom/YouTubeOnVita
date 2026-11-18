/*
 * Starboard 15 system identification and entropy on Sony PlayStation Vita.
 *
 * The platform MUST NOT impersonate a certified YouTube device or Nintendo
 * Switch. Unsupported property IDs return false. Randomness uses the genuine
 * Vita kernel CSPRNG and fails closed rather than using insecure rand().
 */
#include "starboard/system.h"
#include "starboard/log.h"
#include "starboard/cpu_features.h"
#include <psp2/kernel/rng.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/sysmodule.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace {
const char* const kLocales[] = {
  "ja_JP", "en_US", "fr_FR", "es_ES", "de_DE",
  "it_IT", "nl_NL", "pt_PT", "ru_RU", "ko_KR",
  "zh_TW", "zh_CN", "fi_FI", "sv_SE", "da_DK",
  "nb_NO", "pl_PL", "pt_BR", "en_GB", "tr_TR"
};
bool get_console_parameter(int id,int* result) {
  if(!result)return false;
  // sceAppUtilSystemParamGetInt works in Vita3K's emulated system.
  // If unavailable, do not infer a user's region from their IP.
  return sceAppUtilSystemParamGetInt(id,result)>=0;
}
bool copy_value(const char* value,char* out,int cap) {
  if(!value || !out || cap<=0)return false;
  size_t len=strlen(value);
  if(len+1>static_cast<size_t>(cap))return false;
  memcpy(out,value,len+1);
  return true;
}
}

extern "C" {
const char* SbSystemGetLocaleId() {
  int id=-1;
  if(get_console_parameter(SCE_SYSTEM_PARAM_ID_LANG,&id) &&
     id>=0 && id<(int)(sizeof(kLocales)/sizeof(kLocales[0])))
    return kLocales[id];
  return "en_US"; // explicit safe default when system locale is unavailable
}

bool SbSystemGetProperty(SbSystemPropertyId id,char* out,int len) {
  const char* property=nullptr;
  switch(id) {
    case kSbSystemPropertyBrandName: property="Sony"; break;
    case kSbSystemPropertyModelName: property="PlayStation Vita"; break;
    case kSbSystemPropertyModelYear: property="2011"; break;
    case kSbSystemPropertyPlatformName: property="PS Vita"; break;
    case kSbSystemPropertyFriendlyName: property="PlayStation Vita"; break;
    // Real platform type; not a claim of official YouTube certification.
    case kSbSystemPropertyDeviceType: property="GAME_CONSOLE"; break;
    // Deliberately DO NOT claim a certification scope, Speech API key,
    // firmware build, chipset part number, ad ID, or system integrator.
    default: return false;
  }
  return copy_value(property,out,len);
}
bool SbSystemHasCapability(SbSystemCapabilityId id){
  switch(id) {
    case kSbSystemCapabilityReversedEnterAndBack: {
      int button=-1;
      return get_console_parameter(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON,&button) &&
             button==SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
    }
    case kSbSystemCapabilityCanQueryGPUMemoryStats: return false;
    default:return false;
  }
}
int SbSystemGetNumberOfProcessors() {
  // PS Vita has four Cortex-A9 cores; three allocated to normal applications.
  return 3;
}
int64_t SbSystemGetTotalCPUMemory() {
  // Conservative application memory budget, not the physical RAM amount.
  return 256LL*1024*1024;
}
int64_t SbSystemGetUsedCPUMemory(){
  // VitaSDK provides no total resident-process-memory counter here.
  // Return zero instead of a fake measurement; do not claim it's measured.
  return 0;
}
bool SbCPUFeaturesGet(SbCPUFeatures* out) {
  if(!out)return false;
  memset(out,0,sizeof(*out));
  out->architecture=kSbCPUFeaturesArchitectureArm;
  out->brand="ARM Cortex-A9 (PS Vita)";
  out->icache_line_size=32;
  out->dcache_line_size=32;
  out->has_fpu=true;
  out->arm.implementer=0x41;
  out->arm.architecture_generation=7;
  out->arm.part=0xC09;
  out->arm.has_neon=true;
  out->arm.has_thumb2=true;
  out->arm.has_vfp=true;
  out->arm.has_vfp3=true;
  out->arm.has_vfp3_d32=true;
  // CPU variant, revision and unsupported instruction extensions remain
  // conservative zeros, not guessed from emulator host capabilities.
  return true;
}
void SbSystemGetRandomData(void* output,int bytes){
  if(!output || bytes<=0)return;
  unsigned char* p=static_cast<unsigned char*>(output);
  int offset=0;
  while(offset<bytes){
    int n=bytes-offset>64?64:bytes-offset;
    int status=sceKernelGetRandomNumber(p+offset,(SceSize)n);
    if(status<0){
      memset(p,0,(size_t)bytes);
      SbLogRaw("ERROR: Vita CSPRNG unavailable; refusing insecure randomness\n");
      abort();
    }
    offset+=n;
  }
}
uint64_t SbSystemGetRandomUInt64(){
  uint64_t random=0;
  SbSystemGetRandomData(&random,(int)sizeof(random));
  return random;
}
SbSystemError SbSystemGetLastError() {return errno;}
void SbSystemClearLastError() {errno=0;}
int SbSystemGetErrorString(SbSystemError error,char* out,int size){
  const char* message=strerror(error);
  if(!message)message="Unknown error";
  int len=(int)strlen(message);
  if(out && size>0){
    int n=len<size-1?len:size-1;
    memcpy(out,message,(size_t)n);
    out[n]=0;
  }
  return len;
}
const void* SbSystemGetExtension(const char* name){
  (void)name;
  return nullptr; // no fake Starboard extensions
}
bool SbSystemSupportsResume(){return false;}
bool SbSystemIsDebuggerAttached(){return false;}
void SbSystemHideSplashScreen(){} // No native Vita startup splash is shown.
}

