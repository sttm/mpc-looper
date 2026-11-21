#pragma once
#include "LoopFiles.h"
namespace loop4 {
struct DiskEntry {unsigned length=0;float bpm=120;std::string path;};
inline void copyDiskSource(const std::filesystem::path& source,const std::filesystem::path& destination){
 loopdisk::nonAudio();std::filesystem::create_directories(destination.parent_path());auto temp=destination;temp+=".tmp";
 int in=::open(source.c_str(),O_RDONLY|O_CLOEXEC);if(in<0)throw std::runtime_error("Cannot read session source");int out=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
 if(out<0){::close(in);throw std::runtime_error("Cannot collect session");}
 try{std::array<uint8_t,loopdisk::ChunkBytes> bytes{};off_t at=0;struct stat st{};if(::fstat(in,&st))throw std::runtime_error("Source stat failed");
  while(at<st.st_size){size_t n=size_t(std::min<off_t>(bytes.size(),st.st_size-at));if(!loopdisk::readAll(in,bytes.data(),n,at)||!loopdisk::writeAll(out,bytes.data(),n,at))throw std::runtime_error("Collect session failed");at+=off_t(n);}
  if(::fdatasync(out)||::rename(temp.c_str(),destination.c_str()))throw std::runtime_error("Collect commit failed");
  ::close(in);::close(out);
 }catch(...){::close(in);::close(out);std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
inline void encodeDisk(Engine* engine,std::vector<uint8_t>& out,const std::string& collectDirectory=""){
 if(!engine||!engine->diskStore()){encode(engine,out);return;}
 out.insert(out.end(),{'S','T','T','M','D','S','K','1'});put32(out,unsigned(engine->rate));
 auto id=engine->sessionId;id.resize(32,'0');out.insert(out.end(),id.begin(),id.begin()+32);put32(out,unsigned(engine->activeSlots));
 std::array<Pin,4> pins;for(int n=0;n<4;++n)pins[n]=engine->pin(n);
 unsigned index=0;for(auto& p:pins){auto* b=p.buffer;std::string path;
  if(b){auto* store=engine->diskStore();if(!collectDirectory.empty()){
    path=collectDirectory+"/Loop-"+std::to_string(index+1)+".f32";copyDiskSource(store->resolve(store->sourcePath(*b->disk)),store->resolve(path));store->useCollected(*b->disk,path);
   }else path=store->savedPath(*b->disk);}++index;
  put32(out,b?b->length:0);putFloat(out,b?float(b->bpm):120);put32(out,unsigned(path.size()));out.insert(out.end(),path.begin(),path.end());
 }
}
inline Engine* makeDiskEngine(double rate,const std::string& id,int count,const std::filesystem::path& content){
 auto store=std::make_shared<loopdisk::Store>(content,id,unsigned(rate),count);
 return new Engine(rate,id,MaxSourceSeconds,count,0,std::move(store));
}
inline Engine* decodeDisk(const uint8_t* data,size_t bytes,double targetRate,int requested,const std::filesystem::path& content){
 if(bytes<48)throw std::runtime_error("Invalid session");
 unsigned sourceRate=u32(data+8);if(sourceRate<8000||sourceRate>96000)throw std::runtime_error("Invalid source rate");std::string id(reinterpret_cast<const char*>(data+12),32);
 if(id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid session ID");
 bool manifest=std::memcmp(data,"STTMDSK1",8)==0;bool legacy=std::memcmp(data,"STTMLOP1",8)==0;if(!manifest&&!legacy)throw std::runtime_error("Unknown session format");
 size_t pos=44;int count=requested;std::array<DiskEntry,4> entries;std::array<size_t,4> offsets{};
 if(manifest){if(bytes>16384)throw std::runtime_error("Manifest too large");unsigned declared=u32(data+pos);pos+=4;if(declared!=2&&declared!=4)throw std::runtime_error("Invalid loop count");if(requested!=2&&requested!=4)count=int(declared);}
 for(int n=0;n<4;++n){if(bytes-pos<(manifest?12u:8u))throw std::runtime_error("Truncated session");auto& e=entries[n];e.length=u32(data+pos);e.bpm=f32(data+pos+4);pos+=8;
  if(e.length>uint64_t(sourceRate)*MaxSourceSeconds||!std::isfinite(e.bpm)||e.bpm<20||e.bpm>400)throw std::runtime_error("Invalid loop metadata");
  if(manifest){unsigned size=u32(data+pos);pos+=4;if(size>1024||size>bytes-pos)throw std::runtime_error("Invalid source reference");e.path.assign(reinterpret_cast<const char*>(data+pos),size);pos+=size;if(bool(e.length)!=!e.path.empty()||e.path.find('\0')!=std::string::npos)throw std::runtime_error("Invalid source reference");}
  else {if(e.length>(bytes-pos)/8)throw std::runtime_error("Truncated audio");offsets[n]=pos;pos+=size_t(e.length)*8;}
  if(n>=2&&e.length)count=4;
 }
 if(pos!=bytes)throw std::runtime_error("Trailing session data");
 auto engine=std::unique_ptr<Engine>(makeDiskEngine(targetRate,id,count,content));auto* store=engine->diskStore();
 for(int n=0;n<4;++n){auto& e=entries[n];if(e.length<128)continue;unsigned frames=unsigned(std::llround(e.length*targetRate/sourceRate));if(frames<128||frames>engine->capacity)throw std::runtime_error("Invalid resampled length");
  auto& s=engine->slots[n];auto& b=s.data[0];
  if(manifest&&sourceRate==unsigned(targetRate)){auto meta=store->load(unsigned(n),0,e.path);if(meta.length!=e.length||std::fabs(meta.bpm-e.bpm)>.01f)throw std::runtime_error("Source metadata differs");}
  else if(manifest){loopdisk::Reader source(store->resolve(e.path));if(source.metadata().length!=e.length||source.metadata().rate!=sourceRate)throw std::runtime_error("Source metadata differs");
   store->import(unsigned(n),0,frames,e.bpm,[&](unsigned i){double p=i*double(sourceRate)/targetRate;unsigned a=std::min(unsigned(p),e.length-1),next=std::min(a+1,e.length-1);float mix=float(p-a);auto x=source.sample(a),y=source.sample(next);return loopdisk::Frame{Engine::clean(x.l+(y.l-x.l)*mix),Engine::clean(x.r+(y.r-x.r)*mix)};});
  }else {store->import(unsigned(n),0,frames,e.bpm,[&](unsigned i){double p=i*double(sourceRate)/targetRate;unsigned a=std::min(unsigned(p),e.length-1),next=std::min(a+1,e.length-1);float mix=float(p-a);auto* x=data+offsets[n]+size_t(a)*8;auto* y=data+offsets[n]+size_t(next)*8;
    return loopdisk::Frame{Engine::clean(f32(x)+(f32(y)-f32(x))*mix),Engine::clean(f32(x+4)+(f32(y+4)-f32(x+4))*mix)};});}
  b.length=frames;b.bpm=e.bpm;s.current=0;s.playing=true;s.published.store(0);s.audible.store(0);
 }
 store->start();return engine.release();
}
// File output always uses bounded buffers; the source reader has four cache pages.
inline void wavDisk(const std::filesystem::path& path,const Buffer& b,double sampleRate,double targetBpm,bool warp){
 if(!b.disk)throw std::runtime_error("Not a disk source");
 auto* store=b.disk->store;loopdisk::Reader reader(store->resolve(store->sourcePath(*b.disk)));
 unsigned length=tempoFrames(b,targetBpm);double ratio=double(b.length)/length;
 AudioSource source{nullptr,nullptr,b.length,&b,&reader,&loopdisk::Reader::callback};int band=SincTable::band(ratio);(void)SincTable::table();
 signalsmith::stretch::SignalsmithStretch<float> stretch(42);double cursor=0,fraction=0;
 if(warp&&std::fabs(ratio-1)>1e-8){stretch.presetDefault(2,float(sampleRate),true);int seek=stretch.outputSeekLength(float(ratio));stretch.outputSeek(source.at(0),seek);cursor=seek;}else warp=false;
 std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=".tmp";std::ofstream file(temp,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Cannot create WAV");
 std::vector<uint8_t> header;auto tag=[&](const char* p){header.insert(header.end(),p,p+4);};auto put16=[&](unsigned v){header.push_back(uint8_t(v));header.push_back(uint8_t(v>>8));};
 tag("RIFF");put32(header,36+length*6);tag("WAVE");tag("fmt ");put32(header,16);put16(1);put16(2);put32(header,unsigned(sampleRate));put32(header,unsigned(sampleRate)*6);put16(6);put16(24);tag("data");put32(header,length*6);file.write(reinterpret_cast<const char*>(header.data()),std::streamsize(header.size()));
 std::array<float,512> left{},right{};std::array<uint8_t,512*6> bytes{};unsigned fade=std::min(unsigned(sampleRate*.003),length/4);
 for(unsigned offset=0;offset<length;){unsigned n=std::min(512u,length-offset);
  if(warp){double count=n*ratio+fraction;int input=int(std::floor(count));fraction=count-input;float* outputs[]={left.data(),right.data()};stretch.process(source.at(cursor),input,outputs,int(n));cursor+=input;}
  else for(unsigned i=0;i<n;++i)SincTable::read(source,(offset+i)*ratio,band,left[i],right[i]);
  unsigned at=0;for(unsigned i=0;i<n;++i){unsigned p=offset+i;float seam=1;if(fade){if(p<fade)seam=float(p)/fade;else if(p>=length-fade)seam=float(length-1-p)/fade;}
   for(float value:{left[i],right[i]}){if(!std::isfinite(value))value=0;int32_t v=int32_t(std::lround(std::clamp(value*seam,-1.f,1.f)*8388607.f));for(int k=0;k<3;++k)bytes[at++]=uint8_t(uint32_t(v)>>(8*k));}}
  if(!file.write(reinterpret_cast<const char*>(bytes.data()),at))throw std::runtime_error("WAV write failed");
  offset+=n;
 }
 file.flush();if(!file)throw std::runtime_error("WAV flush failed");file.close();std::filesystem::rename(temp,path);
}
}
