#pragma once
// Bounded disk source backend. Audio owns write queues/onset windows; only the
// I/O worker opens, reads, writes, syncs, renames or closes session audio files.
#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>

namespace loopdisk {
constexpr unsigned ChunkFrames=16384,ChunkBytes=ChunkFrames*8,HeaderBytes=4096;
constexpr unsigned CachePages=12,OnsetFrames=2*ChunkFrames,WritePages=14;
constexpr size_t TrackBytes=size_t(CachePages+6+WritePages)*ChunkBytes;
struct Frame {float l,r;};
inline void sanitize(Frame* frames,unsigned n){for(unsigned i=0;i<n;++i){frames[i].l=std::isfinite(frames[i].l)?std::clamp(frames[i].l,-4.f,4.f):0;frames[i].r=std::isfinite(frames[i].r)?std::clamp(frames[i].r,-4.f,4.f):0;}}
static_assert(sizeof(off_t)>=8,"build with _FILE_OFFSET_BITS=64");
static_assert(sizeof(Frame)==8,"stereo float frame");
static_assert(std::atomic<unsigned>::is_always_lock_free,"32-bit atomics required");
inline thread_local bool audioThread=false;
inline void nonAudio(){if(audioThread)std::abort();}
inline std::string token(){nonAudio();static std::atomic<unsigned> sequence{0};char id[40];
 auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
 std::snprintf(id,sizeof id,"%016llx%08x%08x",(unsigned long long)stamp,unsigned(::getpid()),sequence.fetch_add(1));return id;
}
inline void put32(uint8_t* p,uint32_t value){for(int i=0;i<4;++i)p[i]=uint8_t(value>>(i*8));}
inline uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void putFloat(uint8_t* p,float v){uint32_t b;std::memcpy(&b,&v,4);put32(p,b);}
inline float getFloat(const uint8_t* p){uint32_t b=get32(p);float v;std::memcpy(&v,&b,4);return v;}
inline bool readAll(int fd,void* data,size_t bytes,off_t offset){nonAudio();auto* p=static_cast<uint8_t*>(data);
 while(bytes){ssize_t n=::pread(fd,p,bytes,offset);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;offset+=n;bytes-=size_t(n);}return true;
}
inline bool writeAll(int fd,const void* data,size_t bytes,off_t offset){nonAudio();auto* p=static_cast<const uint8_t*>(data);
 while(bytes){ssize_t n=::pwrite(fd,p,bytes,offset);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;offset+=n;bytes-=size_t(n);}return true;
}
struct Metadata {unsigned rate=0,length=0;float bpm=120;};
inline Metadata header(int fd){nonAudio();std::array<uint8_t,32> h{};struct stat st{};
 if(!readAll(fd,h.data(),h.size(),0)||std::memcmp(h.data(),"MPCDFD01",8)!=0||::fstat(fd,&st))throw std::runtime_error("Invalid disk source");
 Metadata m{get32(h.data()+8),get32(h.data()+12),getFloat(h.data()+16)};
 if(m.rate<8000||m.rate>96000||m.length<128||!std::isfinite(m.bpm)||m.bpm<20||m.bpm>400||uint64_t(st.st_size)!=HeaderBytes+uint64_t(m.length)*8)throw std::runtime_error("Incomplete disk source");
 return m;
}
class Store;struct Track;
struct Page {
 unsigned buffer=0,generation=0,index=0,count=0,retireEpoch=0;uint64_t age=0;
 Frame* data=nullptr;enum Phase{Free,Ready,Retired} phase=Free;
};
struct Packet {unsigned buffer=0,generation=0,start=0,count=0,length=0;float bpm=120;bool end=false;Frame* data=nullptr;};
struct Request {unsigned buffer=0,generation=0,index=0;};
struct Stream {
 Store* store=nullptr;Track* track=nullptr;unsigned buffer=0;
 std::atomic<unsigned> generation{0},done{0},committed{0},failed{0},saved{0};
 std::atomic<unsigned> length{0},onsetStart{0},onsetCount{0},cursor{0},abortFence{0},abortGeneration{0};
 std::atomic<bool> active{false};
 Frame* onset=nullptr;
 // Audio-only scratch: reset each callback; page pointers are protected by epoch.
 std::array<Page*,8> hot{};unsigned hotCount=0;
 std::array<unsigned,8> missing{};unsigned missingCount=0;
 // I/O-worker state; non-audio snapshots only access these after committed.
 int fd=-1;unsigned fileGeneration=0,written=0,start=0;bool fileFailed=false,owned=false;
 std::string relativePath,tempPath,absolutePath,collectedPath;float fileBpm=120;
 bool begin(unsigned origin) noexcept;
 bool write(unsigned at,float l,float r) noexcept;
 bool seal(unsigned frames,float bpm) noexcept;
 void abort() noexcept;
 bool reusable()const noexcept{unsigned g=generation.load();return g==0||done.load()==g;}
 bool complete()const noexcept{unsigned g=generation.load();return g&&committed.load(std::memory_order_acquire)==g;}
 bool error()const noexcept{return generation.load()&&failed.load()==generation.load();}
 float sample(int channel,unsigned at) noexcept;
 bool available(unsigned first,unsigned count) noexcept;
 void requestCursor(unsigned at) noexcept{cursor.store(at);active.store(true);}
};
struct Track {
 Store* store=nullptr;unsigned number=0;Frame* memory=nullptr;bool locked=false;
 std::array<Stream,3> streams;
 std::array<Page,CachePages> pages;
 std::array<std::atomic<Page*>,CachePages> published{};
 std::array<Packet,WritePages> packets;
 std::atomic<unsigned> head{0},tail{0};unsigned filling=0,producerIndex=0,consumerIndex=0;
 std::array<Request,64> requests{};std::atomic<unsigned> requestHead{0},requestTail{0};
 explicit Track(Store* owner,unsigned n):store(owner),number(n){
  void* allocation=nullptr;if(::posix_memalign(&allocation,4096,TrackBytes))throw std::bad_alloc();memory=static_cast<Frame*>(allocation);std::memset(memory,0,TrackBytes);
  locked=::mlock(memory,TrackBytes)==0;
  for(unsigned i=0;i<CachePages;++i)pages[i].data=memory+i*ChunkFrames;
  for(unsigned i=0;i<3;++i){streams[i].store=owner;streams[i].track=this;streams[i].buffer=i;streams[i].onset=memory+(CachePages+2*i)*ChunkFrames;}
  for(unsigned i=0;i<WritePages;++i)packets[i].data=memory+(CachePages+6+i)*ChunkFrames;
 }
 ~Track(){for(auto& s:streams)if(s.fd>=0)::close(s.fd);if(locked)::munlock(memory,TrackBytes);std::free(memory);}
 bool flush() noexcept {unsigned h=head.load(std::memory_order_relaxed);if(h-tail.load(std::memory_order_acquire)>=WritePages)return false;head.store(h+1,std::memory_order_release);producerIndex=(producerIndex+1)%WritePages;filling=0;return true;}
 bool begin(Stream& s,unsigned origin) noexcept {
  unsigned h=head.load(std::memory_order_relaxed);if(filling||h-tail.load(std::memory_order_acquire)>=WritePages)return false;
  auto& p=packets[producerIndex];p.buffer=s.buffer;p.generation=s.generation.load();p.start=origin;p.count=0;p.end=false;return flush();
 }
 bool queue(Stream& s,unsigned at,float l,float r) noexcept;
 bool end(Stream& s,unsigned frames,float bpm) noexcept;
 void ask(unsigned buffer,unsigned generation,unsigned index) noexcept {
  unsigned h=requestHead.load(std::memory_order_relaxed);if(h-requestTail.load(std::memory_order_acquire)>=requests.size())return;
  requests[h%requests.size()]={buffer,generation,index};requestHead.store(h+1,std::memory_order_release);
 }
 Page* find(unsigned buffer,unsigned generation,unsigned index) noexcept {
  for(auto& cell:published){auto* p=cell.load(std::memory_order_seq_cst);if(p&&p->buffer==buffer&&p->generation==generation&&p->index==index)return p;}return nullptr;
 }
};
class Store {
 std::mutex pathMutex_;
 std::filesystem::path content_,directory_;std::string relativeDirectory_;
 std::vector<std::unique_ptr<Track>> tracks_;
 std::thread worker_;std::atomic<bool> running_{false};
 std::atomic<unsigned> epoch_{0};bool inBlock_=false;uint64_t tick_=0;
 std::atomic<unsigned> readMisses_{0},writeErrors_{0},ioErrors_{0};
#ifdef LOOPER_DISK_TEST_HOOKS
 std::atomic<bool> readPaused_{false};std::atomic<unsigned> readDelay_{0},writeDelay_{0};std::atomic<int> writeBudget_{-1};
#endif
 bool ioRead(int fd,void* p,size_t n,off_t at){nonAudio();
#ifdef LOOPER_DISK_TEST_HOOKS
  while(readPaused_.load()&&running_.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if(unsigned ms=readDelay_.load())std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
  return readAll(fd,p,n,at);
 }
 bool ioWrite(int fd,const void* p,size_t n,off_t at){nonAudio();
#ifdef LOOPER_DISK_TEST_HOOKS
  if(unsigned ms=writeDelay_.load())std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  int budget=writeBudget_.load();if(budget==0){errno=ENOSPC;return false;}if(budget>0)writeBudget_.fetch_sub(1);
#endif
  return writeAll(fd,p,n,at);
 }
 void fail(Stream& s){s.fileFailed=true;s.failed.store(s.fileGeneration);ioErrors_.fetch_add(1);}
 bool openWrite(Stream& s,unsigned gen){
  std::lock_guard<std::mutex> lock(pathMutex_);s.collectedPath.clear();
  if(s.fd>=0){::close(s.fd);s.fd=-1;}
  if(s.owned&&!s.absolutePath.empty()&&s.saved.load()!=s.fileGeneration){std::error_code ec;std::filesystem::remove(s.absolutePath,ec);}
  if(!s.tempPath.empty()){std::error_code ec;std::filesystem::remove(s.tempPath,ec);}
  s.fileGeneration=gen;s.written=0;s.start=0;s.fileFailed=false;s.owned=true;
  s.relativePath=relativeDirectory_+"/L"+std::to_string(s.track->number+1)+"-B"+std::to_string(s.buffer)+"-V"+std::to_string(gen)+".f32";
  s.absolutePath=(content_/s.relativePath).string();s.tempPath=s.absolutePath+".tmp";
  std::error_code directoryError;std::filesystem::create_directories(directory_,directoryError);
  if(directoryError){s.fd=-1;fail(s);return false;}
  s.fd=::open(s.tempPath.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(s.fd<0){fail(s);return false;}return true;
 }
 bool consumeWrite(Track& t){
  unsigned tail=t.tail.load(std::memory_order_relaxed);if(tail==t.head.load(std::memory_order_acquire))return false;
  auto& p=t.packets[t.consumerIndex];auto& s=t.streams[p.buffer];
  if(s.fileGeneration!=p.generation)openWrite(s,p.generation);
  if(!s.fileFailed&&p.count){
   if(s.written==0)s.start=p.start;
   if(!ioWrite(s.fd,p.data,size_t(p.count)*8,HeaderBytes+off_t(p.start)*8))fail(s);
   else s.written+=p.count;
  }
  if(p.end){
   if(!s.fileFailed&&(s.written!=p.length||p.length<128))fail(s);
   if(!s.fileFailed){std::array<uint8_t,HeaderBytes> h{};std::memcpy(h.data(),"MPCDFD01",8);put32(h.data()+8,rate_);put32(h.data()+12,p.length);putFloat(h.data()+16,p.bpm);
    if(::ftruncate(s.fd,HeaderBytes+off_t(p.length)*8)||!ioWrite(s.fd,h.data(),h.size(),0)||::fdatasync(s.fd)||::rename(s.tempPath.c_str(),s.absolutePath.c_str()))fail(s);
   }
   if(!s.fileFailed){s.tempPath.clear();s.fileBpm=p.bpm;s.committed.store(p.generation,std::memory_order_release);}
   s.done.store(p.generation,std::memory_order_release);
  }
  t.tail.store(tail+1,std::memory_order_release);t.consumerIndex=(t.consumerIndex+1)%WritePages;return true;
 }
 void aborts(Track& t){for(auto& s:t.streams){unsigned gen=s.abortGeneration.load();if(gen&&gen==s.generation.load()&&s.done.load()!=gen&&int32_t(t.tail.load()-s.abortFence.load())>=0){
   if(s.fileGeneration==gen){s.fileFailed=true;if(s.fd>=0){::close(s.fd);s.fd=-1;}if(!s.tempPath.empty()){std::error_code ec;std::filesystem::remove(s.tempPath,ec);}}
   s.failed.store(gen);s.done.store(gen);
  }}}
 void reclaim(Track& t){unsigned now=epoch_.load(std::memory_order_seq_cst);for(auto& p:t.pages)if(p.phase==Page::Retired&&(!(p.retireEpoch&1u)||now!=p.retireEpoch))p.phase=Page::Free;}
 bool loadPage(Track& t,Stream& s,unsigned gen,unsigned index){
  if(!s.complete()||s.generation.load()!=gen||s.fd<0||s.fileGeneration!=gen||s.fileFailed)return false;
  unsigned length=s.length.load();if(uint64_t(index)*ChunkFrames>=length)return false;
  if(auto* hit=t.find(s.buffer,gen,index)){hit->age=++tick_;return false;}
  reclaim(t);Page* free=nullptr;for(auto& p:t.pages)if(p.phase==Page::Free){free=&p;break;}
  if(!free){Page* victim=nullptr;
   for(auto& p:t.pages)if(p.phase==Page::Ready){auto& owner=t.streams[p.buffer];bool pinned=p.index==0&&owner.generation.load()==p.generation;
    if(owner.active.load()&&owner.generation.load()==p.generation){unsigned pages=(owner.length.load()+ChunkFrames-1)/ChunkFrames;
     if(pages){unsigned cursor=owner.cursor.load()/ChunkFrames;unsigned forward=(p.index+pages-cursor)%pages;pinned=pinned||forward<=2||forward==pages-1;}}
    if(!pinned&&(!victim||p.age<victim->age))victim=&p;}
   if(!victim)return false;
   unsigned at=unsigned(victim-t.pages.data());t.published[at].store(nullptr,std::memory_order_seq_cst);victim->retireEpoch=epoch_.load(std::memory_order_seq_cst);victim->phase=Page::Retired;reclaim(t);if(victim->phase==Page::Free)free=victim;else return true;
  }
  unsigned count=std::min(ChunkFrames,length-index*ChunkFrames);
  if(!ioRead(s.fd,free->data,size_t(count)*8,HeaderBytes+off_t(index)*ChunkFrames*8)){fail(s);return true;}
  sanitize(free->data,count);
  free->buffer=s.buffer;free->generation=gen;free->index=index;free->count=count;free->age=++tick_;free->phase=Page::Ready;
  unsigned at=unsigned(free-t.pages.data());t.published[at].store(free,std::memory_order_seq_cst);return true;
 }
 bool serviceReads(Track& t){
  unsigned tail=t.requestTail.load(std::memory_order_relaxed);if(tail!=t.requestHead.load(std::memory_order_acquire)){
   auto p=t.requests[tail%t.requests.size()];t.requestTail.store(tail+1,std::memory_order_release);auto& s=t.streams[p.buffer];if(s.generation.load()==p.generation)loadPage(t,s,p.generation,p.index);return true;
  }
  // Keep each immutable version's beginning available for Undo/All Play.
  for(auto& s:t.streams)if(s.complete()&&loadPage(t,s,s.generation.load(),0))return true;
  for(auto& s:t.streams)if(s.complete()&&s.active.load()){
   unsigned length=s.length.load();if(!length)continue;unsigned gen=s.generation.load(),cursor=s.cursor.load()%length;
   for(int ahead=-1;ahead<=6;++ahead){int64_t at=int64_t(cursor)+int64_t(ahead)*ChunkFrames;at%=length;if(at<0)at+=length;
    if(loadPage(t,s,gen,unsigned(at)/ChunkFrames))return true;
   }
  }return false;
 }
 void run(){while(running_.load()){bool worked=false;for(auto& tp:tracks_){auto& t=*tp;reclaim(t);aborts(t);
   // Alternate I/O classes; an export never runs on this worker.
   worked=consumeWrite(t)||worked;worked=serviceReads(t)||worked;
  }if(!worked){bool active=false;for(auto& t:tracks_)for(auto& s:t->streams)active=active||s.active.load();std::this_thread::sleep_for(std::chrono::milliseconds(active?2:20));}}}
public:
 const unsigned rate_;const int count;
 Store(const std::filesystem::path& content,const std::string& session,unsigned rate,int loops):content_(content),rate_(rate),count(loops<=2?2:4){
  nonAudio();if(session.size()!=32||session.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid disk session ID");
  std::filesystem::create_directories(content_);content_=std::filesystem::weakly_canonical(content_);
  relativeDirectory_="Sessions/"+session+"/Audio/"+token();directory_=resolve(relativeDirectory_);
  for(int n=0;n<count;++n)tracks_.push_back(std::make_unique<Track>(this,unsigned(n)));
 }
 ~Store(){nonAudio();running_.store(false);if(worker_.joinable())worker_.join();
  for(auto& t:tracks_)for(auto& s:t->streams){std::error_code ec;if(!s.tempPath.empty())std::filesystem::remove(s.tempPath,ec);
   if(s.owned&&!s.absolutePath.empty()&&s.saved.load()!=s.fileGeneration)std::filesystem::remove(s.absolutePath,ec);}
  tracks_.clear();std::error_code ec;std::filesystem::remove(directory_,ec);
 }
 void start(){nonAudio();if(!worker_.joinable()){running_.store(true);worker_=std::thread([this]{try{run();}catch(...){ioErrors_.fetch_add(1);for(auto& t:tracks_)for(auto& s:t->streams)s.failed.store(s.generation.load());running_.store(false);}});}}
 Stream& stream(unsigned loop,unsigned buffer){return tracks_[loop]->streams[buffer];}
 void beginAudio() noexcept {epoch_.fetch_add(1,std::memory_order_seq_cst);inBlock_=true;audioThread=true;for(auto& t:tracks_)for(auto& s:t->streams){s.hotCount=s.missingCount=0;s.active.store(false);}}
 void endAudio() noexcept {audioThread=false;inBlock_=false;epoch_.fetch_add(1,std::memory_order_seq_cst);}
 bool inAudio()const noexcept{return inBlock_;}
 struct FrameGuard {Store* store=nullptr;explicit FrameGuard(Store* s):store(s&&!s->inAudio()?s:nullptr){if(store)store->beginAudio();}~FrameGuard(){if(store)store->endAudio();}};
 size_t cacheBytes()const noexcept{return size_t(count)*TrackBytes;}
 bool locked()const noexcept{for(auto& t:tracks_)if(!t->locked)return false;return true;}
 unsigned readMisses()const noexcept{return readMisses_.load();}unsigned writeErrors()const noexcept{return writeErrors_.load();}unsigned ioErrors()const noexcept{return ioErrors_.load();}
 void miss()noexcept{readMisses_.fetch_add(1);}void writeFailure()noexcept{writeErrors_.fetch_add(1);}
 const std::filesystem::path& contentRoot()const{return content_;}
 std::string sourcePath(Stream& s){nonAudio();std::lock_guard<std::mutex> lock(pathMutex_);if(!s.complete()||s.error())throw std::runtime_error("Disk commit pending or source unavailable");return s.relativePath;}
 std::string savedPath(Stream& s){nonAudio();std::lock_guard<std::mutex> lock(pathMutex_);if(!s.complete())throw std::runtime_error("Disk commit pending or source unavailable");if(!s.collectedPath.empty())return s.collectedPath;unsigned g=s.generation.load();s.saved.store(g);return s.relativePath;}
 void useCollected(Stream& s,const std::string& path){nonAudio();std::lock_guard<std::mutex> lock(pathMutex_);s.collectedPath=path;}
 std::filesystem::path resolve(const std::string& relative)const{nonAudio();std::filesystem::path p(relative);
  if(relative.empty()||p.is_absolute())throw std::runtime_error("Invalid source path");
  for(auto& part:p)if(part=="..")throw std::runtime_error("Invalid source path");
  auto full=std::filesystem::weakly_canonical(content_/p);auto rel=full.lexically_relative(content_);if(rel.empty()||*rel.begin()=="..")throw std::runtime_error("Source outside Content");return full;
 }
 Metadata load(unsigned loop,unsigned buffer,const std::string& relative){nonAudio();auto& s=stream(loop,buffer);auto path=resolve(relative);int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("Missing disk source");
  try{auto m=header(fd);if(m.rate!=rate_)throw std::runtime_error("Source rate differs; migrate off-line");unsigned n=std::min(m.length,ChunkFrames);
   if(!readAll(fd,s.onset,size_t(n)*8,HeaderBytes))throw std::runtime_error("Cannot preload source");
   unsigned tail=m.length-n,at=tail%ChunkFrames,first=std::min(n,ChunkFrames-at);
   if(!readAll(fd,s.onset+ChunkFrames+at,size_t(first)*8,HeaderBytes+off_t(tail)*8)||(first<n&&!readAll(fd,s.onset+ChunkFrames,size_t(n-first)*8,HeaderBytes+off_t(tail+first)*8)))throw std::runtime_error("Cannot preload source tail");
   sanitize(s.onset,OnsetFrames);
   s.relativePath=relative;s.absolutePath=path.string();s.fd=fd;s.fileGeneration=1;s.owned=false;s.fileBpm=m.bpm;s.length.store(m.length);s.onsetStart.store(0);s.onsetCount.store(m.length);
   s.generation.store(1);s.done.store(1);s.committed.store(1);s.saved.store(1);return m;
  }catch(...){::close(fd);throw;}
 }
 // Legacy migration/import: bounded source reader supplied by the caller.
 template<class Reader> void import(unsigned loop,unsigned buffer,unsigned frames,float bpm,Reader&& reader){nonAudio();auto& s=stream(loop,buffer);
  openWrite(s,1);if(s.fd<0)throw std::runtime_error("Cannot create migrated source");
  auto block=std::make_unique<Frame[]>(ChunkFrames);for(unsigned offset=0;offset<frames;){unsigned n=std::min(ChunkFrames,frames-offset);
   for(unsigned i=0;i<n;++i)block[i]=reader(offset+i);
   for(unsigned i=0;i<n;++i){unsigned at=offset+i;if(at<ChunkFrames)s.onset[at]=block[i];s.onset[ChunkFrames+at%ChunkFrames]=block[i];}
   if(!writeAll(s.fd,block.get(),size_t(n)*8,HeaderBytes+off_t(offset)*8))throw std::runtime_error("Cannot write migrated source");
   offset+=n;
  }
  std::array<uint8_t,HeaderBytes> h{};std::memcpy(h.data(),"MPCDFD01",8);put32(h.data()+8,rate_);put32(h.data()+12,frames);putFloat(h.data()+16,bpm);
  if(!writeAll(s.fd,h.data(),h.size(),0)||::fdatasync(s.fd)||::rename(s.tempPath.c_str(),s.absolutePath.c_str()))throw std::runtime_error("Cannot commit migrated source");
  s.tempPath.clear();s.written=frames;s.fileBpm=bpm;s.length.store(frames);s.onsetStart.store(0);s.onsetCount.store(frames);s.generation.store(1);s.done.store(1);s.committed.store(1);
 }
#ifdef LOOPER_DISK_TEST_HOOKS
 void pauseReads(bool paused){readPaused_.store(paused);}
 void delays(unsigned readMs,unsigned writeMs){readDelay_.store(readMs);writeDelay_.store(writeMs);}void failWritesAfter(int count){writeBudget_.store(count);}
#endif
};
inline bool Track::queue(Stream& s,unsigned at,float l,float r) noexcept {
 unsigned h=head.load(std::memory_order_relaxed);if(h-tail.load(std::memory_order_acquire)>=WritePages)return false;
 auto* p=&packets[producerIndex];unsigned gen=s.generation.load();
 if(filling&&(p->buffer!=s.buffer||p->generation!=gen||p->start+filling!=at)){p->count=filling;if(!flush())return false;h=head.load();if(h-tail.load()>=WritePages)return false;p=&packets[producerIndex];}
 if(!filling){p->buffer=s.buffer;p->generation=gen;p->start=at;p->count=0;p->end=false;}
 p->data[filling++]={l,r};p->count=filling;if(filling==ChunkFrames-p->start%ChunkFrames)return flush();return true;
}
inline bool Track::end(Stream& s,unsigned frames,float bpm) noexcept {
 unsigned h=head.load(std::memory_order_relaxed);if(h-tail.load(std::memory_order_acquire)>=WritePages)return false;auto* p=&packets[producerIndex];
 if(filling&&(p->buffer!=s.buffer||p->generation!=s.generation.load())){p->count=filling;if(!flush())return false;h=head.load();if(h-tail.load()>=WritePages)return false;p=&packets[producerIndex];}
 if(!filling){p->buffer=s.buffer;p->generation=s.generation.load();p->count=0;p->start=0;}
 p->count=filling;p->end=true;p->length=frames;p->bpm=bpm;return flush();
}
inline bool Stream::begin(unsigned origin) noexcept {
 if(!reusable())return false;
 unsigned gen=generation.load()+1;if(!gen)gen=1;generation.store(gen);abortGeneration.store(0);length.store(0);onsetStart.store(origin);onsetCount.store(0);active.store(false);hotCount=missingCount=0;if(!track->begin(*this,origin)){store->writeFailure();abort();return false;}return true;
}
inline bool Stream::write(unsigned at,float l,float r) noexcept {
 if(!track->queue(*this,at,l,r)){store->writeFailure();abort();return false;}
 unsigned n=onsetCount.load(std::memory_order_relaxed);if(n<ChunkFrames)onset[n]={l,r};onset[ChunkFrames+n%ChunkFrames]={l,r};onsetCount.store(n+1,std::memory_order_release);return true;
}
inline bool Stream::seal(unsigned frames,float bpm) noexcept {length.store(frames);if(!track->end(*this,frames,bpm)){store->writeFailure();abort();return false;}return true;}
inline void Stream::abort() noexcept {unsigned h=track->head.load();auto& p=track->packets[track->producerIndex];if(track->filling&&p.buffer==buffer&&p.generation==generation.load())track->filling=0;
 abortFence.store(h);abortGeneration.store(generation.load());failed.store(generation.load());}
inline float Stream::sample(int channel,unsigned at) noexcept {
 // These scalar fields are written by this audio owner (or initialized before
 // engine publication). Page publication/retirement retains sequential ordering.
 unsigned len=length.load(std::memory_order_relaxed);if(!len)return 0;if(at>=len)at%=len;unsigned start=onsetStart.load(std::memory_order_relaxed);unsigned rel=at>=start?at-start:at+len-start;
 unsigned total=onsetCount.load(std::memory_order_relaxed);
 if(rel<std::min(total,ChunkFrames)){const auto& f=onset[rel];return channel?f.r:f.l;}
 if(total>=len&&rel>=total-std::min(total,ChunkFrames)){const auto& f=onset[ChunkFrames+rel%ChunkFrames];return channel?f.r:f.l;}
 unsigned gen=generation.load(std::memory_order_relaxed),index=at/ChunkFrames;
 for(unsigned i=0;i<hotCount;++i){auto* p=hot[i];if(p->generation==gen&&p->index==index&&at%ChunkFrames<p->count){const auto& f=p->data[at%ChunkFrames];return channel?f.r:f.l;}}
 if(auto* p=track->find(buffer,gen,index)){if(hotCount<hot.size())hot[hotCount++]=p;const auto& f=p->data[at%ChunkFrames];return channel?f.r:f.l;}
 bool asked=false;for(unsigned i=0;i<missingCount;++i)if(missing[i]==index)asked=true;
 if(!asked){if(missingCount<missing.size())missing[missingCount++]=index;track->ask(buffer,gen,index);store->miss();}return 0;
}
inline bool Stream::available(unsigned first,unsigned count) noexcept {
 unsigned len=length.load(std::memory_order_relaxed);if(!len)return false;first%=len;count=std::min(count,len);requestCursor(first);unsigned gen=generation.load(std::memory_order_relaxed);bool ready=true;
 for(unsigned doneFrames=0;doneFrames<count;){unsigned at=(first+doneFrames)%len;unsigned onset=onsetStart.load(std::memory_order_relaxed);unsigned rel=at>=onset?at-onset:at+len-onset;
  unsigned n=std::min(count-doneFrames,std::min(ChunkFrames-at%ChunkFrames,len-at));unsigned have=onsetCount.load(std::memory_order_relaxed);unsigned head=std::min(have,ChunkFrames),tail=have-std::min(have,ChunkFrames);
  if(rel<head)n=std::min(n,head-rel);
  else if(have>=len&&rel>=tail)n=std::min(n,have-rel);
  else if(auto* page=track->find(buffer,gen,at/ChunkFrames)){
   // Hold the page through this block: publication may be retired after the
   // readiness check, but epoch protection keeps this captured pointer valid.
   bool held=false;for(unsigned i=0;i<hotCount;++i)held=held||hot[i]==page;
   if(!held&&hotCount<hot.size())hot[hotCount++]=page;
  }else {track->ask(buffer,gen,at/ChunkFrames);ready=false;}
  doneFrames+=std::max(1u,n);
 }return ready;
}
// Non-audio, bounded 4-page cache for WAV export and sample-rate conversion.
class Reader {
 int fd_=-1;Metadata meta_{};struct Block{unsigned index=~0u,count=0;uint64_t age=0;std::unique_ptr<Frame[]> data;};
 std::array<Block,4> blocks_;uint64_t tick_=0;
public:
 explicit Reader(const std::filesystem::path& path){nonAudio();fd_=::open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd_<0)throw std::runtime_error("Missing disk source");
  try{meta_=header(fd_);for(auto& b:blocks_)b.data=std::make_unique<Frame[]>(ChunkFrames);}catch(...){::close(fd_);throw;}}
 ~Reader(){if(fd_>=0)::close(fd_);}const Metadata& metadata()const{return meta_;}
 Frame sample(unsigned at){nonAudio();at%=meta_.length;unsigned index=at/ChunkFrames;for(auto& b:blocks_)if(b.index==index){b.age=++tick_;return b.data[at%ChunkFrames];}
  auto* b=&blocks_[0];for(auto& candidate:blocks_)if(candidate.age<b->age)b=&candidate;
  b->count=std::min(ChunkFrames,meta_.length-index*ChunkFrames);if(!readAll(fd_,b->data.get(),size_t(b->count)*8,HeaderBytes+off_t(index)*ChunkFrames*8))throw std::runtime_error("Source read failed");
  sanitize(b->data.get(),b->count);b->index=index;b->age=++tick_;return b->data[at%ChunkFrames];
 }
 static float callback(const void* ctx,int channel,unsigned at){auto f=const_cast<Reader*>(static_cast<const Reader*>(ctx))->sample(at);return channel?f.r:f.l;}
};
}
