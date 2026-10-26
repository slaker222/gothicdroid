#include "opussound.h"

#include <opus.h>
#include <algorithm>
#include <cstring>

static constexpr int32_t OpusRate = 48000;

static uint16_t rd16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1]<<8));
  }

static int64_t rd64(const uint8_t* p) {
  uint64_t v = 0;
  for(int i=7; i>=0; --i)
    v = (v<<8) | p[i];
  return int64_t(v);
  }

static void wr16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v>>8));
  }

static void wr32(std::vector<uint8_t>& out, uint32_t v) {
  wr16(out,uint16_t(v));
  wr16(out,uint16_t(v>>16));
  }

static void wrTag(std::vector<uint8_t>& out, const char* tag) {
  out.insert(out.end(),tag,tag+4);
  }

namespace {
struct Decoder final {
  ~Decoder() {
    if(dec!=nullptr)
      opus_decoder_destroy(dec);
    }

  bool packet(const uint8_t* p, size_t size) {
    if(packetId++==0) {
      // OpusHead
      if(size<19 || std::memcmp(p,"OpusHead",8)!=0)
        return false;
      channels = p[9];
      preSkip  = rd16(p+10);
      if(channels<1 || channels>2 || p[18]!=0)
        return false;
      int err = 0;
      dec = opus_decoder_create(OpusRate,channels,&err);
      return err==OPUS_OK;
      }
    if(packetId==2)
      return true; // OpusTags
    const int maxFrame = OpusRate*120/1000;
    size_t    at       = pcm.size();
    pcm.resize(at + size_t(maxFrame*channels));
    int n = opus_decode(dec,p,opus_int32(size),pcm.data()+at,maxFrame,0);
    if(n<0)
      return false;
    pcm.resize(at + size_t(n*channels));
    return true;
    }

  OpusDecoder*         dec      = nullptr;
  int                  packetId = 0;
  int                  channels = 0;
  uint16_t             preSkip  = 0;
  std::vector<int16_t> pcm;
  };
}

bool OpusSound::isOpus(const uint8_t* data, size_t size) {
  return size>=36 && std::memcmp(data,"OggS",4)==0 && std::memcmp(data+28,"OpusHead",8)==0;
  }

std::vector<uint8_t> OpusSound::decodeToWav(const uint8_t* data, size_t size) {
  Decoder              d;
  std::vector<uint8_t> packet;
  int64_t              granule = -1;

  size_t pos = 0;
  while(pos+27<=size) {
    if(std::memcmp(data+pos,"OggS",4)!=0)
      return {};
    const int64_t gp   = rd64(data+pos+6);
    const uint8_t nseg = data[pos+26];
    size_t        off  = pos+27+nseg;
    if(off>size)
      return {};
    for(size_t i=0; i<nseg; ++i) {
      const uint8_t lace = data[pos+27+i];
      if(off+lace>size)
        return {};
      packet.insert(packet.end(),data+off,data+off+lace);
      off += lace;
      if(lace<255) {
        if(!d.packet(packet.data(),packet.size()))
          return {};
        packet.clear();
        }
      }
    if(gp>=0)
      granule = gp;
    pos = off;
    }

  if(d.channels==0)
    return {};

  // trim encoder delay at the start and padding of the last frame
  size_t begin = size_t(d.preSkip)*size_t(d.channels);
  size_t end   = d.pcm.size();
  if(granule>=0)
    end = std::min(end,size_t(granule)*size_t(d.channels));
  if(begin>end)
    begin = end;

  const uint32_t bytes = uint32_t((end-begin)*sizeof(int16_t));
  std::vector<uint8_t> wav;
  wav.reserve(44+bytes);
  wrTag(wav,"RIFF");
  wr32 (wav,36+bytes);
  wrTag(wav,"WAVE");
  wrTag(wav,"fmt ");
  wr32 (wav,16);
  wr16 (wav,1); // PCM
  wr16 (wav,uint16_t(d.channels));
  wr32 (wav,OpusRate);
  wr32 (wav,uint32_t(OpusRate*d.channels*2));
  wr16 (wav,uint16_t(d.channels*2));
  wr16 (wav,16);
  wrTag(wav,"data");
  wr32 (wav,bytes);
  auto src = reinterpret_cast<const uint8_t*>(d.pcm.data()+begin);
  wav.insert(wav.end(),src,src+bytes);
  return wav;
  }
