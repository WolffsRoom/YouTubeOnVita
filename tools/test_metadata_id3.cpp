#include "port_download_metadata.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>
static void chk(bool c,const char*s){if(!c){std::fprintf(stderr,"FAIL %s\n",s);std::exit(2);}}
static bool frame(const std::vector<uint8_t>&tag,const char four[5]){
  for(size_t i=10;i+4<tag.size();++i)if(!memcmp(tag.data()+i,four,4))return true;
  return false;
}
int main(){
  const char* genre_json=R"({"videoDetails":{"title":"TÍTULO ❤️","author":"Uploader","genre":"Rock"},
    "musicMetadata":{"artist":"Artist","album":"Album","albumArtist":"Main Artist",
      "releaseYear":"2022","trackNumber":"04"}})";
  YtvVideoMetadata m;
  chk(ytv_parse_video_metadata(genre_json,m),"parse metadata");
  chk(m.genre=="Rock","genre Rock");
  chk(m.album=="Album","album Album");
  chk(m.artist=="Artist","actual artist overrides uploader");
  chk(m.album_artist=="Main Artist","album artist");
  chk(m.year=="2022","release year");
  chk(m.track=="04","track number");
  auto tag=ytv_make_mp3_id3(m,{});
  chk(tag.size()>40 && tag[0]=='I' && tag[1]=='D' && tag[2]=='3',"id3 generated");
  for(auto id:{"TIT2","TPE1","TPE2","TALB","TCON","TYER","TRCK"})
    chk(frame(tag,id),id);
  const char* generic=R"({"videoDetails":{"title":"Music","author":"Uploader"},
                           "microformat":{"playerMicroformatRenderer":{"category":"Music"}}})";
  chk(ytv_parse_video_metadata(generic,m),"generic parsed");
  chk(m.genre.empty(),"Music is not musical genre");
  tag=ytv_make_mp3_id3(m,{});
  chk(!frame(tag,"TCON"),"Music not emitted as TCON");
  chk(frame(tag,"TIT2"),"title preserved");
  std::puts("METADATA_ID3_TEST_PASS");
  return 0;
}
