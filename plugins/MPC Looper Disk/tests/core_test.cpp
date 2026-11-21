#include "LoopFiles.h"
#include <cassert>
#include <thread>
#include <iostream>
#include <cstdlib>
thread_local bool inAudio=false;
[[gnu::noinline]] void* operator new(std::size_t size){if(inAudio)std::abort();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
using namespace loop4;
static const std::string ID(32,'a');
struct Host {
    Engine engine{8000,ID,4};double beat=0;std::array<float,4> levels{{1,1,1,1}};
    void run(int frames,float input=0){inAudio=true;for(int i=0;i<frames;++i){float l,r;engine.frame(input,-input,l,r,beat,levels,false);assert(std::isfinite(l)&&std::isfinite(r));beat+=2./8000;}inAudio=false;}
    void command(int n,unsigned action,int quant=0,int bars=0){inAudio=true;engine.command(n,action,beat,120,4,quant,bars);inAudio=false;}
    void record(int n,int frames,float input){command(n,Rec);run(frames,input);command(n,Rec);run(1);}
};
int main(){
    Host h;h.record(0,512,.2f);auto first=h.engine.pin(0);assert(first&&first.buffer->length==512);assert(first.buffer->l[127]==.2f);
    h.record(1,768,.4f);auto second=h.engine.pin(1);assert(second&&second.buffer->length==768);
    assert(first.buffer->l[127]==.2f&&second.buffer->l[127]==.4f);
    first=Pin{};second=Pin{};
    h.command(1,Dub);h.run(1600,.1f);h.command(1,Dub);h.run(800); // finish at the loop boundary
    {auto a=h.engine.pin(0),b=h.engine.pin(1);assert(a.buffer->l[100]==.2f);assert(b.buffer->l[100]>.49f);}
    h.command(1,Undo);h.run(1);{auto b=h.engine.pin(1);assert(b.buffer->l[100]==.4f);}
    h.command(1,Undo);h.run(1);{auto b=h.engine.pin(1);assert(b.buffer->l[100]>.49f);}
    // Recording a new slot never overdubs an earlier slot.
    h.command(0,Dub);h.run(600,.05f);h.record(2,1024,.6f);h.run(600);
    {auto b=h.engine.pin(2);assert(b&&b.buffer->length==1024&&b.buffer->l[100]==.6f);}
    // First capture supports undo-to-empty and redo.
    h.command(2,Undo);h.run(1);assert(!h.engine.pin(2));h.command(2,Undo);h.run(1);assert(h.engine.pin(2));
    // Simultaneous snapshot readers: published samples are stable even during overdub.
    std::atomic<bool> stop{false};std::atomic<unsigned> snapshots{0};
    std::thread reader([&]{while(!stop.load()){auto b=h.engine.pin(0);if(b){auto hash=[&]{uint32_t h=2166136261u;for(unsigned i=0;i<b.buffer->length;++i){uint32_t bits;std::memcpy(&bits,&b.buffer->l[i],4);h=(h^bits)*16777619u;}return h;};auto before=hash();std::this_thread::yield();assert(before==hash());++snapshots;}}});
    while(!snapshots.load())std::this_thread::yield();
    h.command(0,Dub);h.run(120000,.02f);h.command(0,Dub);h.run(1024);stop.store(true);reader.join();
    std::vector<uint8_t> bytes;encode(&h.engine,bytes);auto copy=std::unique_ptr<Engine>(decode(bytes.data(),bytes.size(),8000));
    for(int n=0;n<4;++n){auto a=h.engine.pin(n),b=copy->pin(n);assert(bool(a)==bool(b));if(a){assert(a.buffer->length==b.buffer->length);assert(a.buffer->l==b.buffer->l || std::equal(a.buffer->l.begin(),a.buffer->l.begin()+a.buffer->length,b.buffer->l.begin()));}}
    auto directory=std::filesystem::temp_directory_path()/"sttm-loop4-core-test";std::filesystem::create_directories(directory);
    {auto b=h.engine.pin(1);wav(directory/"one.wav",*b.buffer,h.engine.rate);auto raw=read(directory/"one.wav");assert(raw.size()==44+size_t(b.buffer->length)*6);assert(u32(raw.data()+24)==8000&&raw[34]==24);}
    auto broken=bytes;broken[44]=255;broken[45]=255;broken[46]=255;broken[47]=127;
    bool rejected=false;try{delete decode(broken.data(),broken.size(),8000);}catch(const std::exception&){rejected=true;}assert(rejected);
    Host quant;quant.beat=.5;quant.command(0,Rec,2,1);assert(quant.engine.view(0).state==Armed);quant.run(13999,.3f);assert(quant.engine.view(0).state==Armed);quant.run(16002,.3f);assert(quant.engine.view(0).frames==16000);
    Host limit;limit.command(0,Rec,0,4);assert(limit.engine.view(0).state==TooLong);
    std::filesystem::remove_all(directory);
    std::cout<<"PASS independent slots, target switching, quantized recording, undo/redo, concurrent pinned snapshots, PCM24 WAV, state recall, malformed state, no audio allocations\n";
}
