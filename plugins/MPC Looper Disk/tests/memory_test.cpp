#include "LoopFiles.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
thread_local bool audio=false;
[[gnu::noinline]] void* operator new(std::size_t n){if(audio)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static size_t descriptors(){size_t n=0;for(auto& p:std::filesystem::directory_iterator("/proc/self/fd")){(void)p;++n;}return n;}
static void render(loop4::Engine& e,unsigned frames,float value,bool worker=true){std::array<float,4> levels{{1,1,1,1}};
 for(unsigned offset=0;offset<frames;offset+=128){audio=true;for(unsigned i=offset;i<std::min(frames,offset+128);++i){float l,r;e.frame(value,-value,l,r,i*2./8000,levels,false);assert(std::isfinite(l)&&std::isfinite(r));}audio=false;if(worker)e.prefault();}
}
int main(){
 auto fds=descriptors();
 for(int bpm:{120,60}){
  loop4::Engine e(8000,std::string(32,'a'),loop4::MaxSourceSeconds,2,32*1024*1024);
  assert(e.memoryBytes()<1024*1024);e.playback(bpm,false,false);e.recordAction(0,1);
  e.command(0,loop4::Rec,0,bpm,4,0,32);assert(e.view(0).state==loop4::Armed);
  unsigned frames=unsigned(32*4*60./bpm*8000);render(e,1024,.2f);
  const float* old=e.slots[0].data[e.slots[0].work].l.data();render(e,frames-1024,.2f);
  auto p=e.pin(0);assert(p&&p.buffer->length==frames);assert(p.buffer->bpm==bpm);
  assert(p.buffer->l[0]==.2f&&old[0]==.2f&&p.buffer->l[frames-1]==.2f);
  assert(e.memoryBytes()<size_t(frames)*8+1024*1024); // no full-length spare/undo preallocation
  std::vector<uint8_t> data;loop4::encode(&e,data);std::unique_ptr<loop4::Engine> copy(loop4::decode(data.data(),data.size(),8000,2,32*1024*1024));
  auto b=copy->pin(0);assert(b&&b.buffer->length==frames&&b.buffer->l[frames-1]==.2f);
  std::cout<<"32 bars at "<<bpm<<" BPM: "<<frames/8000.<<" seconds, committed MiB="<<e.memoryBytes()/1048576.<<"\n";
 }
 assert(descriptors()==fds);
 // A deliberately stopped allocator can never make audio write into an unprepared page.
 {loop4::Engine e(8000,std::string(32,'b'),loop4::MaxSourceSeconds,2,32*1024*1024);e.recordAction(0,1);
  e.command(0,loop4::Rec,0,120,4,0,32);render(e,30000,.1f,false);
  assert(e.view(0).state==loop4::MemoryFull);auto p=e.pin(0);assert(p&&p.buffer->length>=128&&p.buffer->length<30000);}
 // Budget exhaustion during overdub discards only the unfinished new pass.
 {loop4::Engine e(8000,std::string(32,'c'),loop4::MaxSourceSeconds,2,6*1024*1024);e.recordAction(0,1);
  e.command(0,loop4::Rec,0,120,4,0,32);render(e,512000,.2f);assert(e.pin(0).buffer->length==512000);
  e.command(0,loop4::Rec,0,120,4,0,0);render(e,512000,.3f);assert(e.view(0).state==loop4::MemoryFull);
  auto p=e.pin(0);assert(p.buffer->length==512000&&p.buffer->l[1000]==.2f);assert(e.memoryBytes()<=6*1024*1024);}
 assert(descriptors()==fds);
 // Exercise actual concurrent mapping growth while audio keeps raw source pointers.
 {loop4::Engine e(8000,std::string(32,'d'),loop4::MaxSourceSeconds,2,32*1024*1024);e.recordAction(0,1);
  std::atomic<bool> running{true};std::thread worker([&]{while(running.load()){e.prefault();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
  e.command(0,loop4::Rec,0,120,4,0,32);std::array<float,4> levels{{1,1,1,1}};
  for(unsigned offset=0;offset<512000;offset+=128){audio=true;for(unsigned i=offset;i<offset+128;++i){float l,r;e.frame(.125f,-.125f,l,r,i*2./8000,levels,false);}audio=false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  running.store(false);worker.join();auto p=e.pin(0);assert(p&&p.buffer->length==512000);
  for(unsigned i=0;i<512000;++i){assert(p.buffer->l[i]==.125f&&p.buffer->r[i]==-.125f);}
 }
 assert(descriptors()==fds);
 std::cout<<"PASS 32-bar capture/recall at 120/60 BPM, bounded growing RAM, stable alias pointers, ready-prefix guards, preserved source on RAM limit, no FD leaks or audio allocations\n";
}
