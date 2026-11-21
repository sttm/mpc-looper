#pragma once
#include "LoopEngine.h"
#include <cstring>
#include <memory>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace loop4 {
inline void put32(std::vector<uint8_t>& out,uint32_t v){for(int k=0;k<4;++k)out.push_back(uint8_t(v>>(k*8)));}
inline void putFloat(std::vector<uint8_t>& out,float f){uint32_t bits;std::memcpy(&bits,&f,4);put32(out,bits);}
inline uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline float f32(const uint8_t* p){uint32_t bits=u32(p);float f;std::memcpy(&f,&bits,4);return f;}
inline void encode(Engine* engine,std::vector<uint8_t>& out){
    out.insert(out.end(),{'S','T','T','M','L','O','P','1'});put32(out,engine?uint32_t(engine->rate):44100);
    std::string id=engine?engine->sessionId:std::string(32,'0');id.resize(32,'0');out.insert(out.end(),id.begin(),id.begin()+32);
    std::array<Pin,Slots> pins;for(int n=0;n<Slots;++n)if(engine)pins[n]=engine->pin(n);
    for(auto& pin:pins){auto* b=pin.buffer;put32(out,b?b->length:0);putFloat(out,b?float(b->bpm):120);
        if(b)for(unsigned i=0;i<b->length;++i){putFloat(out,b->l[i]);putFloat(out,b->r[i]);}}
}
inline Engine* decode(const uint8_t* data,size_t bytes,double targetRate,int requestedSlots=4,size_t memoryLimit=0){
    if(bytes<76||std::memcmp(data,"STTMLOP1",8)!=0)throw std::runtime_error("Invalid session");
    uint32_t sourceRate=u32(data+8);if(sourceRate<8000||sourceRate>96000)throw std::runtime_error("Invalid sample rate");
    std::string id(reinterpret_cast<const char*>(data+12),32);
    for(char c:id)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))throw std::runtime_error("Invalid session ID");
    size_t pos=44;std::array<size_t,Slots> starts{};std::array<unsigned,Slots> counts{};std::array<double,Slots> tempos{};
    for(int n=0;n<Slots;++n){if(bytes-pos<8)throw std::runtime_error("Truncated session");unsigned count=u32(data+pos);float bpm=f32(data+pos+4);pos+=8;
        if(count>sourceRate*MaxSourceSeconds||!std::isfinite(bpm)||bpm<20||bpm>400||size_t(count)>(bytes-pos)/8)throw std::runtime_error("Invalid loop");
        starts[n]=pos;counts[n]=count;tempos[n]=bpm;pos+=size_t(count)*8;}
    if(pos!=bytes)throw std::runtime_error("Trailing session data");
    int count=requestedSlots;for(int n=2;n<Slots;++n)if(counts[n])count=4;
    double seconds=32;for(auto frames:counts)seconds=std::max(seconds,double(frames)/sourceRate);
    auto engine=std::make_unique<Engine>(targetRate,id,memoryLimit?MaxSourceSeconds:std::ceil(seconds),count,memoryLimit);
    for(int n=0;n<Slots;++n){unsigned len=std::min(engine->capacity,unsigned(std::llround(counts[n]*targetRate/sourceRate)));
        if(len<128)continue;
        std::vector<float> l(len),r(len);
        for(unsigned j=0;j<len;++j){double p=j*double(sourceRate)/targetRate;unsigned a=std::min(unsigned(p),counts[n]-1),b=std::min(a+1,counts[n]-1);float t=float(p-a);
            l[j]=Engine::clean(f32(data+starts[n]+size_t(a)*8)*(1-t)+f32(data+starts[n]+size_t(b)*8)*t);
            r[j]=Engine::clean(f32(data+starts[n]+size_t(a)*8+4)*(1-t)+f32(data+starts[n]+size_t(b)*8+4)*t);}
        engine->import(n,l.data(),r.data(),len,tempos[n]);}
    return engine.release();
}
inline void writeAtomic(const std::filesystem::path& file,const std::vector<uint8_t>& data){
    std::filesystem::create_directories(file.parent_path());auto temp=file;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out||!out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()))||!out.flush())throw std::runtime_error("Cannot write file");}
    std::filesystem::rename(temp,file);
}
inline std::vector<uint8_t> read(const std::filesystem::path& file){
    std::ifstream in(file,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("No saved backup");auto size=in.tellg();
    if(size<0||size>400000000)throw std::runtime_error("Invalid backup size");
    std::vector<uint8_t> data(size_t(size),uint8_t{});in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(data.data()),size))throw std::runtime_error("Cannot read backup");
    return data;
}
inline void wav(const std::filesystem::path& path,const Buffer& b,double rate){
    std::vector<uint8_t> out;out.reserve(44+size_t(b.length)*6);
    auto tag=[&](const char* s){out.insert(out.end(),s,s+4);};auto put16=[&](unsigned v){out.push_back(uint8_t(v));out.push_back(uint8_t(v>>8));};
    tag("RIFF");put32(out,36+b.length*6);tag("WAVE");tag("fmt ");put32(out,16);put16(1);put16(2);put32(out,uint32_t(rate));put32(out,uint32_t(rate)*6);put16(6);put16(24);tag("data");put32(out,b.length*6);
    unsigned fade=std::min(unsigned(rate*.003),b.length/4);
    for(unsigned i=0;i<b.length;++i){float seam=1;
        if(fade){if(i<fade)seam=float(i)/fade;else if(i>=b.length-fade)seam=float(b.length-1-i)/fade;}
        for(float sample:{b.l[i],b.r[i]}){int32_t value=int32_t(std::lround(std::clamp(sample*seam,-1.f,1.f)*8388607.f));for(int k=0;k<3;++k)out.push_back(uint8_t(uint32_t(value)>>(8*k)));}}

    writeAtomic(path,out);
}
// Non-realtime, streaming export: bounded buffers even for very slow tempos.
inline unsigned tempoFrames(const Buffer& b,double targetBpm){auto frames=std::max(1ll,std::llround(b.length*b.bpm/targetBpm));if(frames>(4294967295ll-36)/6)throw std::runtime_error("Rendered WAV exceeds RIFF size; export Original");return unsigned(frames);}
inline void wavTempo(const std::filesystem::path& path,const Buffer& b,double sampleRate,double targetBpm,bool warp){
    if(std::fabs(targetBpm-b.bpm)<1e-8){wav(path,b,sampleRate);return;}
    unsigned length=tempoFrames(b,targetBpm);double ratio=double(b.length)/length;
    AudioSource source{b.l.data(),b.r.data(),b.length,&b};int band=SincTable::band(ratio);(void)SincTable::table();
    signalsmith::stretch::SignalsmithStretch<float> stretch(42);double cursor=0,fraction=0;
    if(warp){stretch.presetDefault(2,float(sampleRate),true);int seek=stretch.outputSeekLength(float(ratio));stretch.outputSeek(source.at(0),seek);cursor=seek;}
    std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=".tmp";
    std::ofstream file(temp,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Cannot create WAV");
    std::vector<uint8_t> header;auto tag=[&](const char* s){header.insert(header.end(),s,s+4);};auto put16=[&](unsigned v){header.push_back(uint8_t(v));header.push_back(uint8_t(v>>8));};
    tag("RIFF");put32(header,36+length*6);tag("WAVE");tag("fmt ");put32(header,16);put16(1);put16(2);put32(header,unsigned(sampleRate));put32(header,unsigned(sampleRate)*6);put16(6);put16(24);tag("data");put32(header,length*6);
    file.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));
    std::array<float,512> l{},r{};std::array<uint8_t,512*6> bytes{};unsigned fade=std::min(unsigned(sampleRate*.003),length/4);
    for(unsigned offset=0;offset<length;){unsigned count=std::min(512u,length-offset);
        if(warp){double wanted=count*ratio+fraction;int input=int(std::floor(wanted));fraction=wanted-input;float* outputs[]={l.data(),r.data()};stretch.process(source.at(cursor),input,outputs,int(count));cursor+=input;}
        else for(unsigned i=0;i<count;++i)SincTable::read(source,(offset+i)*ratio,band,l[i],r[i]);
        unsigned at=0;for(unsigned i=0;i<count;++i){unsigned p=offset+i;float seam=1;
            if(fade){if(p<fade)seam=float(p)/fade;else if(p>=length-fade)seam=float(length-1-p)/fade;}
            for(float value:{l[i],r[i]}){if(!std::isfinite(value))value=0;int32_t pcm=int32_t(std::lround(std::clamp(value*seam,-1.f,1.f)*8388607.f));for(int k=0;k<3;++k)bytes[at++]=uint8_t(uint32_t(pcm)>>(8*k));}}
        if(!file.write(reinterpret_cast<const char*>(bytes.data()),at))throw std::runtime_error("Cannot write WAV");
        offset+=count;
    }
    file.flush();if(!file)throw std::runtime_error("Cannot flush WAV");file.close();std::filesystem::rename(temp,path);
}

}
