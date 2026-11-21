#define LOOPER_DISK_TEST_HOOKS
#include "DiskFiles.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
[[gnu::noinline]] void* operator new(std::size_t n){if(loopdisk::audioThread)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
struct Rig {
 std::shared_ptr<loopdisk::Store> store;std::unique_ptr<loop4::Engine> engine;std::array<float,4> levels{{1,1,1,1}};double beat=0;unsigned sr=8000;
 explicit Rig(const std::filesystem::path& root,int count=2){store=std::make_shared<loopdisk::Store>(root,std::string(32,'a'),sr,count);engine=std::make_unique<loop4::Engine>(sr,std::string(32,'a'),loop4::MaxSourceSeconds,count,0,store);for(int n=0;n<count;++n)engine->recordAction(n,1);store->start();}
 void render(unsigned frames,float value=0,bool pace=false){for(unsigned offset=0;offset<frames;offset+=128){engine->beginAudio();for(unsigned i=offset;i<std::min(frames,offset+128);++i){float l,r;engine->frame(value,-value,l,r,beat,levels,false);assert(std::isfinite(l)&&std::isfinite(r));beat+=2./sr;}engine->endAudio();if(pace)std::this_thread::sleep_for(std::chrono::microseconds(250));}}
 void command(int n,unsigned command,int bars=0){engine->beginAudio();engine->command(n,command,beat,120,4,0,bars);engine->endAudio();}
 void settle(){for(int i=0;i<1000&&engine->pendingDiskCommits();++i){render(128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(!engine->pendingDiskCommits());}
};
static double frequency(const std::vector<float>& x,unsigned rate){unsigned crossings=0;size_t start=x.size()/3;for(size_t i=start+1;i<x.size();++i)if(x[i-1]<=0&&x[i]>0)++crossings;return crossings*double(rate)/(x.size()-start);}
int main(){auto root=std::filesystem::current_path()/"build/disk-engine-test/Content";std::filesystem::remove_all(root.parent_path());
 Rig rig(root);rig.command(0,loop4::Rec,32);rig.render(512000,.2f,true);rig.settle();assert(rig.engine->view(0).frames==512000);assert(rig.engine->memoryBytes()==8u*1024*1024);
 std::vector<uint8_t> manifest;loop4::encodeDisk(rig.engine.get(),manifest);assert(manifest.size()<2048);assert(std::memcmp(manifest.data(),"STTMDSK1",8)==0);
 std::unique_ptr<loop4::Engine> copy(loop4::decodeDisk(manifest.data(),manifest.size(),8000,2,root));assert(copy->view(0).frames==512000&&copy->memoryBytes()==8u*1024*1024);
 {auto p=copy->pin(0);assert(p);loopdisk::Reader read(copy->diskStore()->resolve(copy->diskStore()->sourcePath(*p.buffer->disk)));assert(read.sample(100000).l==.2f);}
 // Traverse more source pages than fit in RAM, then force a read stall.
 copy->all(true);copy->playback(120,false,false);
 double beat=0;std::array<float,4> levels{{1,1,1,1}};
 auto play=[&](unsigned frames){double energy=0;for(unsigned offset=0;offset<frames;offset+=128){copy->beginAudio();for(unsigned i=offset;i<std::min(frames,offset+128);++i){float l,r;copy->frame(0,0,l,r,beat,levels,false);assert(std::isfinite(l)&&std::isfinite(r));energy+=l*l;beat+=2./8000;}copy->endAudio();std::this_thread::sleep_for(std::chrono::microseconds(750));}return energy;};
 play(1050000); // More than two full 64-second cycles, including cache eviction.
 assert(copy->diskStore()->readMisses()==0);assert(copy->memoryBytes()==8u*1024*1024);
 copy->diskStore()->pauseReads(true);play(300000);assert(copy->diskStore()->readMisses()>0);
 copy->diskStore()->pauseReads(false);play(32000);assert(play(32000)>100);
 std::cout<<"PASS cache eviction across two 64-second cycles, injected read-stall fade/recovery\n";
 // An I/O error in a new overdub must retain the previously committed snapshot.
 {Rig broken(root);broken.command(0,loop4::Rec);broken.render(16000,.15f,true);broken.command(0,loop4::Rec);broken.render(1);broken.settle();broken.store->failWritesAfter(0);
  broken.command(0,loop4::Rec);broken.render(16000,.2f,true);broken.render(128);std::this_thread::sleep_for(std::chrono::milliseconds(50));broken.render(128);
  auto p=broken.engine->pin(0);assert(p);loopdisk::Reader read(broken.store->resolve(broken.store->savedPath(*p.buffer->disk)));assert(read.sample(100).l==.15f);}
 // Imported sources longer than RAM windows exercise real disk reads during DSP.
 constexpr unsigned frames=64000;std::vector<float> sine(frames);for(unsigned i=0;i<frames;++i)sine[i]=float(.2*std::sin(6.283185307179586*440*i/8000));
 Rig tone(root);tone.engine->import(0,sine.data(),sine.data(),frames,120);tone.engine->recordAction(0,1);
 for(bool warp:{false,true}){tone.engine->all(true);tone.engine->playback(150,true,warp);std::vector<float> output(40000);unsigned at=0;
  for(unsigned block=0;at<output.size();++block){tone.engine->beginAudio();for(unsigned i=0;i<128&&at<output.size();++i){float l,r;tone.engine->frame(0,0,l,r,tone.beat,tone.levels,false);output[at++]=l;tone.beat+=2.5/8000;}tone.engine->endAudio();std::this_thread::sleep_for(std::chrono::microseconds(500));}
  double hz=frequency(output,8000);std::cout<<"DISK "<<(warp?"WARP":"SYNC")<<" frequency="<<hz<<" misses="<<tone.store->readMisses()<<"\n";assert(std::fabs(hz-(warp?440:550))<4);
 }
 // Partial overdub, immutable old version and undo.
 Rig dub(root);dub.command(0,loop4::Rec);dub.render(16000,.1f,true);dub.command(0,loop4::Rec);dub.render(1);dub.settle();dub.command(0,loop4::Rec);unsigned dubStart=dub.engine->slots[0].dubStart;dub.render(1000,.2f);dub.command(0,loop4::Rec);dub.render(16000,0,true);dub.settle();
 {auto p=dub.engine->pin(0);assert(p);loopdisk::Reader read(dub.store->resolve(dub.store->sourcePath(*p.buffer->disk)));assert(std::fabs(read.sample((dubStart+500)%16000).l-.3f)<1e-5);}
 dub.command(0,loop4::Undo);dub.render(1);{auto p=dub.engine->pin(0);assert(p);loopdisk::Reader read(dub.store->resolve(dub.store->sourcePath(*p.buffer->disk)));assert(read.sample(100).l==.1f);}
 // Streaming export and collected snapshot do not embed complete source audio.
 {auto p=rig.engine->pin(0);loop4::wavDisk(root.parent_path()/"loop.wav",*p.buffer,8000,120,false);auto data=loop4::read(root.parent_path()/"loop.wav");assert(data.size()==44+512000u*6);}
 std::vector<uint8_t> collected;loop4::encodeDisk(rig.engine.get(),collected,"Sessions/"+std::string(32,'a')+"/Collected/test");assert(collected.size()<2048);
 auto restored=std::unique_ptr<loop4::Engine>(loop4::decodeDisk(collected.data(),collected.size(),8000,2,root));assert(restored->view(0).frames==512000);
 // The previous embedded payload migrates without creating full source vectors.
 loop4::Engine old(8000,std::string(32,'b'),10,2);old.import(0,sine.data(),sine.data(),frames,120);std::vector<uint8_t> legacy;loop4::encode(&old,legacy);
 auto migrated=std::unique_ptr<loop4::Engine>(loop4::decodeDisk(legacy.data(),legacy.size(),8000,2,root));assert(migrated->view(0).frames==frames);
 std::cout<<"PASS 32-bar disk record/recall, fixed caches, compact/collected manifests, disk Sync/Warp, partial overdub+undo, streaming WAV and legacy migration\n";
}
