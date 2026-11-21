#define LOOPER_DISK_TEST_HOOKS
#include "DiskAudio.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
[[gnu::noinline]] void* operator new(std::size_t n){if(loopdisk::audioThread)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
template<class Predicate> void wait(Predicate p){for(int i=0;i<5000&&!p();++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(p());}
int main(){
 auto root=std::filesystem::current_path()/"build/disk-store-test/Content";std::filesystem::remove_all(root.parent_path());std::string saved;
 {loopdisk::Store store(root,std::string(32,'a'),8000,2);assert(store.cacheBytes()==8u*1024*1024);auto& s=store.stream(0,0);
  store.beginAudio();assert(s.begin(0));for(unsigned i=0;i<4096;++i)assert(s.write(i,.25f,-.25f));assert(s.seal(4096,120));assert(s.available(0,4096));for(unsigned i=0;i<4096;++i)assert(s.sample(0,i)==.25f&&s.sample(1,i)==-.25f);store.endAudio();
  assert(!s.complete());store.start();wait([&]{return s.complete();});saved=store.savedPath(s);
  assert(std::filesystem::exists(root/saved));loopdisk::Reader reader(root/saved);assert(reader.metadata().length==4096&&reader.sample(256).l==.25f);
 }
 {loopdisk::Store store(root,std::string(32,'b'),8000,2);auto meta=store.load(0,0,saved);assert(meta.length==4096);store.start();auto& s=store.stream(0,0);
  store.beginAudio();assert(s.available(0,4096));assert(s.sample(1,100)==-.25f);store.endAudio();}
 {loopdisk::Store store(root,std::string(32,'c'),8000,4);store.start();auto& s=store.stream(2,0);store.beginAudio();assert(s.begin(0));store.endAudio();
  for(unsigned offset=0;offset<512000;offset+=128){store.beginAudio();for(unsigned i=offset;i<offset+128;++i)assert(s.write(i,.125f,-.125f));store.endAudio();std::this_thread::sleep_for(std::chrono::microseconds(250));}
  store.beginAudio();assert(s.seal(512000,120));store.endAudio();wait([&]{return s.complete();});
  bool ready=false;wait([&]{store.beginAudio();ready=s.available(100000,20000);store.endAudio();return ready;});
  store.beginAudio();assert(s.available(100000,20000));for(unsigned i=100000;i<120000;++i)assert(s.sample(0,i)==.125f&&s.sample(1,i)==-.125f);store.endAudio();assert(store.cacheBytes()==16u*1024*1024);
 }
 {loopdisk::Store store(root,std::string(32,'d'),8000,2);store.delays(0,1000);store.start();auto& s=store.stream(0,0);bool failed=false;
  auto begin=std::chrono::steady_clock::now();store.beginAudio();assert(s.begin(0));for(unsigned i=0;i<1000000;++i)if(!s.write(i,.1f,.1f)){failed=true;break;}store.endAudio();
  assert(failed&&s.error()&&store.writeErrors()>0);assert(std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<.8);store.delays(0,0);wait([&]{return s.reusable();});assert(!s.complete());
 }
 {loopdisk::Store store(root,std::string(32,'e'),8000,2);store.failWritesAfter(0);store.start();auto& s=store.stream(0,0);store.beginAudio();assert(s.begin(0));for(unsigned i=0;i<4096;++i)assert(s.write(i,.2f,.2f));assert(s.seal(4096,120));store.endAudio();wait([&]{return s.error();});assert(!s.complete());}
 size_t descriptors=std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});
 for(int i=0;i<12;++i){loopdisk::Store store(root,std::string(32,'f'),8000,2);store.load(0,0,saved);store.start();}
 assert(size_t(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{}))==descriptors);
 std::cout<<"PASS fixed 8/16 MiB pools, onset playback before commit, disk reload/cache, bounded slow-disk queue failure, write errors and descriptor cleanup; no audio allocations or I/O\n";
}
