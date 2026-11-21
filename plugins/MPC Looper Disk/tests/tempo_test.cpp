#define LOOP4_TEST_DSP_COUNTER
#include "LoopFiles.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <cstdlib>
thread_local bool audio=false;
[[gnu::noinline]] void* operator new(std::size_t n){if(audio){std::fputs("ALLOCATION IN AUDIO\n",stderr);std::abort();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static double frequency(const std::vector<float>& x,int sr){unsigned crossings=0;size_t start=x.size()/3;
 for(size_t i=start+1;i<x.size();++i)if(x[i-1]<=0&&x[i]>0)++crossings;
 return crossings*double(sr)/(x.size()-start);
}
int main(){
 constexpr int sr=44100;std::vector<float> original(sr),right(sr);
 for(int i=0;i<sr;++i)original[i]=right[i]=float(.3*std::sin(6.283185307179586*440*i/sr));
 std::array<float,4> levels{{1,1,1,1}};
 for(bool warp:{false,true})for(double ratio:{.75,1.25,1.5}){
  loop4::Engine e(sr,std::string(32,'a'),2,2);e.import(0,original.data(),right.data(),sr,120);e.playback(120*ratio,true,warp);
  std::vector<float> output(sr*3);float stereo=0,peak=0;audio=true;
  auto begin=std::chrono::steady_clock::now();
  for(size_t i=0;i<output.size();++i){float l,r;e.frame(0,0,l,r,i*120*ratio/(60*sr),levels,false);assert(std::isfinite(l)&&std::isfinite(r));output[i]=l;stereo=std::max(stereo,std::fabs(l-r));peak=std::max(peak,std::fabs(l));}
  audio=false;double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
  double hz=frequency(output,sr),expected=warp?440:440*ratio;
  std::cout<<(warp?"WARP":"VARISPEED")<<" ratio="<<ratio<<" Hz="<<hz<<" expected="<<expected<<" stereo="<<stereo<<" peak="<<peak<<" CPU="<<elapsed/3*100<<"%\n"<<std::flush;
  assert(std::fabs(hz-expected)<3);assert(stereo<.005f);assert(peak<1);
  double expectedPhase=std::fmod(output.size()*ratio,sr);assert(std::fabs(e.slots[0].pos-expectedPhase)<.001);
 }
 loop4::Engine two(sr,std::string(32,'b'),1,2);assert(two.slots[2].data[0].l.empty()&&!two.slots[2].warpPlayer);
 // Export must match the selected tempo mode, not silently export original duration.
 loop4::Engine source(sr,std::string(32,'c'),2,2);source.import(0,original.data(),right.data(),sr,120);
 auto pin=source.pin(0);auto directory=std::filesystem::temp_directory_path()/"sttm-tempo-quality";
 for(bool warp:{false,true}){
  auto file=directory/(warp?"warp.wav":"varispeed.wav");loop4::wavTempo(file,*pin.buffer,sr,150,warp);auto data=loop4::read(file);
  assert(loop4::u32(data.data()+40)==35280u*6);std::vector<float> samples(35280);
  for(size_t i=0;i<samples.size();++i){auto* p=data.data()+44+i*6;int32_t value=int32_t(p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16));if(value&0x800000)value|=int32_t(0xff000000u);samples[i]=value/8388608.f;}
  double hz=frequency(samples,sr);std::cout<<"WAV "<<(warp?"warp":"varispeed")<<" frames=35280 frequency="<<hz<<"\n";assert(std::fabs(hz-(warp?440:550))<3);
 }
 // Two/four-loop measurements are native Docker numbers, not MPC hardware CPU.
 for(int count:{2,4}){
  loop4::Engine e(sr,std::string(32,'d'),2,count);for(int n=0;n<count;++n)e.import(n,original.data(),right.data(),sr,120);e.playback(150,true,true);
  double total=0,worst=0;int blocks=sr*2/128;audio=true;
  for(int b=0;b<blocks;++b){auto begin=std::chrono::steady_clock::now();for(int i=0;i<128;++i){float l,r;e.frame(0,0,l,r,(b*128+i)*2.5/sr,levels,false);assert(std::isfinite(l)&&std::isfinite(r));}
   double t=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();total+=t;worst=std::max(worst,t);}
  audio=false;
  std::cout<<"BENCH loops="<<count<<" poolMiB="<<double(count)*3*e.capacity*8/1048576<<" avgCPU="<<100*total/(blocks*128./sr)<<"% worstBlockMs="<<worst*1000<<"\n";
  e.all(false);for(int i=0;i<1024;++i){float l,r;e.frame(0,0,l,r,0,levels,false);}
  unsigned before=0;for(int n=0;n<count;++n)before+=e.slots[n].warpPlayer->processCalls;
  for(int i=0;i<sr;++i){float l,r;e.frame(0,0,l,r,0,levels,false);assert(l==0&&r==0);}
  unsigned after=0;for(int n=0;n<count;++n)after+=e.slots[n].warpPlayer->processCalls;assert(before==after);
 }
 // Dynamic BPM changes: bounded output and preserved read-only source audio.
 {loop4::Engine e(sr,std::string(32,'e'),2,2);e.import(0,original.data(),right.data(),sr,120);e.playback(120,true,true);
  audio=true;for(int block=0;block<1000;++block){e.playback(90+double(block)*.09,true,true);for(int i=0;i<128;++i){float l,r;e.frame(0,0,l,r,0,levels,false);assert(std::isfinite(l)&&std::fabs(l)<1&&std::isfinite(r));}}audio=false;
  auto unchanged=e.pin(0);assert(std::equal(original.begin(),original.end(),unchanged.buffer->l.begin()));}
 // Overdub at a changed tempo commits a new cycle; undo keeps original source.
 for(bool warp:{false,true})for(double ratio:{.75,1.25}){
  loop4::Engine e(sr,std::string(32,'f'),2,2);e.import(0,original.data(),right.data(),sr,120);e.playback(120*ratio,true,warp);
  audio=true;e.command(0,loop4::Dub,0,120*ratio,4,0,0);float l,r;e.frame(.01f,.01f,l,r,0,levels,false);
  e.command(0,loop4::Dub,0,120*ratio,4,0,0);
  unsigned limit=unsigned(sr/ratio)+10;while(e.slots[0].dubbing&&limit--){e.frame(.01f,.01f,l,r,0,levels,false);assert(std::isfinite(l)&&std::isfinite(r));}
  audio=false;assert(!e.slots[0].dubbing);auto committed=e.pin(0);
  assert(committed.buffer->length==unsigned(std::llround(sr/ratio)));assert(committed.buffer->bpm==120*ratio);
  e.command(0,loop4::Undo,0,120*ratio,4,0,0);auto undone=e.pin(0);assert(undone.buffer->length==sr&&undone.buffer->bpm==120);
  assert(std::equal(original.begin(),original.end(),undone.buffer->l.begin()));
 }
 // Extreme ratios: robustness only, not an audio-quality promise.
 for(double ratio:{.05,20.}){
  constexpr int lowSr=8000;std::vector<float> noise(lowSr);uint32_t seed=1;for(auto& v:noise){seed=seed*1664525u+1013904223u;v=(float(seed>>8)/16777216.f-.5f)*.2f;}
  loop4::Engine e(lowSr,std::string(32,'f'),2,2);e.import(0,noise.data(),noise.data(),lowSr,ratio<1?400:20);e.playback(ratio<1?20:400,true,true);
  audio=true;for(int i=0;i<lowSr;++i){float l,r;e.frame(0,0,l,r,0,levels,false);assert(std::isfinite(l)&&std::isfinite(r));}audio=false;
 }
 // Legacy four-slot payload with active 3/4 must never silently become two.
 {loop4::Engine e(sr,std::string(32,'f'),2,4);e.import(3,original.data(),right.data(),sr,120);std::vector<uint8_t> bytes;loop4::encode(&e,bytes);
  std::unique_ptr<loop4::Engine> loaded(loop4::decode(bytes.data(),bytes.size(),sr,2));assert(loaded->activeSlots==4&&loaded->pin(3));}
 std::filesystem::remove_all(directory);
 std::cout<<"PASS pitch/duration/stereo at tempo ratios 0.75, 1.25, 1.5; no audio allocation; true two-slot allocation\n";
}
