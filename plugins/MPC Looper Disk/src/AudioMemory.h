#pragma once
#include <vector>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/memfd.h>

namespace loop4 {
// RAM-backed mappings grow only on the non-audio memory worker. Older views
// remain valid until engine destruction, so audio-held sample pointers never
// dangle on growth. memfd is anonymous RAM, not an SD-card audio read path.
class AudioMemory {
    struct View {void* address;size_t bytes;};
    std::vector<float> dense_;
    std::vector<View> views_;
    std::atomic<float*> address_{nullptr};
    float* prepared_=nullptr;size_t logical_=0,mapped_=0;int fd_=-1;bool growing_=false;
public:
    AudioMemory()=default;AudioMemory(const AudioMemory&)=delete;AudioMemory& operator=(const AudioMemory&)=delete;
    ~AudioMemory(){for(auto view:views_)::munmap(view.address,view.bytes);if(fd_>=0)::close(fd_);}
    void resize(size_t count,bool growing=false){logical_=count;growing_=growing;
        if(!growing){dense_.resize(count);address_.store(dense_.data());}
    }
    bool ensure(size_t frames){
        if(!growing_||frames<=mapped_)return true;
        if(fd_<0){fd_=int(::syscall(SYS_memfd_create,"mpclooper-ram",MFD_CLOEXEC));if(fd_<0)return false;}
        size_t target=std::min(logical_,std::max(frames,std::max(size_t(4096),mapped_*2)));
        if(::ftruncate(fd_,off_t(target*sizeof(float)))!=0)return false;
        void* view=::mmap(nullptr,target*sizeof(float),PROT_READ|PROT_WRITE,MAP_SHARED,fd_,0);
        if(view==MAP_FAILED)return false;
        try{views_.push_back({view,target*sizeof(float)});}catch(...){::munmap(view,target*sizeof(float));throw;}
        mapped_=target;prepared_=static_cast<float*>(view);return true;
    }
    void touch(unsigned first,unsigned end){
        // A fresh alias needs page-table entries for its OLD prefix too.
        // These volatile reads only warm mappings; their values are discarded.
        if(prepared_!=address_.load()){volatile const unsigned char* bytes=reinterpret_cast<const unsigned char*>(prepared_);
            for(size_t offset=0;offset<size_t(first)*sizeof(float);offset+=4096){auto value=bytes[offset];(void)value;}}
        std::memset(prepared_+first,0,size_t(end-first)*sizeof(float));
    }
    void publish(){if(growing_)address_.store(prepared_,std::memory_order_release);}
    float* data(){return address_.load(std::memory_order_acquire);}
    const float* data()const{return address_.load(std::memory_order_acquire);}
    size_t size()const{return logical_;}bool empty()const{return logical_==0;}
    float& operator[](size_t i){return data()[i];}const float& operator[](size_t i)const{return data()[i];}
    float* begin(){if(growing_)throw std::logic_error("Use published Buffer length for growing RAM");return data();}float* end(){auto p=data();return p?p+logical_:p;}
    const float* begin()const{if(growing_)throw std::logic_error("Use published Buffer length for growing RAM");return data();}const float* end()const{auto p=data();return p?p+logical_:p;}
    bool operator==(const AudioMemory& other)const{return logical_==other.logical_&&std::equal(begin(),end(),other.begin());}
};
}
