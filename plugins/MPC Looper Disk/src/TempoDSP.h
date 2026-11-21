#pragma once
#include <signalsmith-stretch.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace loop4 {
struct AudioSource {
    using Sample=float(*)(const void*,int,unsigned);using Available=bool(*)(const void*,unsigned,unsigned);
    const float* l=nullptr;const float* r=nullptr;unsigned length=0;const void* identity=nullptr;
    const void* context=nullptr;Sample callback=nullptr;Available availability=nullptr;unsigned generation=0;
    static double wrap(double p,unsigned length){double n=length;if(p>=0&&p<n)return p;if(p>=n&&p<2*n)return p-n;if(p<0&&p>=-n)return p+n;p=std::fmod(p,n);return p<0?p+n:p;}
    float read(int c,unsigned at)const{return callback?callback(context,c,at):(c?r:l)[at];}
    bool ready(unsigned first,unsigned count)const{return availability?availability(context,first,count):true;}
    struct Channel {
        const float* data;unsigned length,base;const void* context;Sample callback;int channel;
        float operator[](int i)const{uint64_t p=uint64_t(base)+unsigned(i);unsigned at=p<length?unsigned(p):unsigned(p%length);return callback?callback(context,channel,at):data[at];}
    };
    struct Circular {
        const float* l;const float* r;unsigned length;double start;const void* context;Sample callback;
        Channel operator[](int c)const{return {c?r:l,length,unsigned(AudioSource::wrap(std::floor(start),length)),context,callback,c};}
    };
    Circular at(double start)const{return {l,r,length,start,context,callback};}
};
// Zero-phase, windowed-sinc interpolation. All kernels are built off the audio
// thread. Cutoff banks prevent the worst downsampling aliasing in varispeed.
struct SincTable {
    static constexpr int Bands=64,Phases=256,Taps=32;
    std::array<float,Bands*Phases*Taps> kernels{};
    SincTable(){for(int band=0;band<Bands;++band){double maxRate=std::pow(20.,double(band)/(Bands-1));double cutoff=1/maxRate;
        for(int phase=0;phase<Phases;++phase){double sum=0;for(int k=0;k<Taps;++k){double x=k-15-double(phase)/Phases;
            double sinc=std::fabs(x)<1e-12?cutoff:std::sin(3.141592653589793*cutoff*x)/(3.141592653589793*x);
            double window=.42+.5*std::cos(3.141592653589793*x/16)+.08*std::cos(2*3.141592653589793*x/16);
            float value=float(sinc*window);kernels[(band*Phases+phase)*Taps+k]=value;sum+=value;}
            for(int k=0;k<Taps;++k)kernels[(band*Phases+phase)*Taps+k]/=float(sum);}}
    }
    static const SincTable& table(){static const SincTable value;return value;}
    static int band(double rate){return rate<=1?0:std::clamp(int(std::ceil(std::log(rate)/std::log(20.)*(Bands-1))),0,Bands-1);}
    static void read(const AudioSource& source,double pos,int band,float& l,float& r){
        pos=AudioSource::wrap(pos,source.length);int64_t integer=int64_t(pos);double fraction=pos-integer;
        if(band==0&&fraction<1e-10){l=source.read(0,unsigned(integer));r=source.read(1,unsigned(integer));return;}
        int phase=std::min(Phases-1,int(fraction*Phases));auto* kernel=table().kernels.data()+(band*Phases+phase)*Taps;
        l=r=0;for(int k=0;k<Taps;++k){int64_t p=integer+k-15;
            if(p<0)p+=source.length;else if(p>=source.length)p-=source.length;
            // Recorded loops have >=128 frames, greater than this FIR's support.
            l+=source.read(0,unsigned(p))*kernel[k];r+=source.read(1,unsigned(p))*kernel[k];}
    }
};
// A linked-stereo phase-vocoder renderer. Signalsmith owns preallocated FFT/STFT
// state; 64-frame work quanta plus splitComputation spread spectral work.
// Loop audio is known ahead: circular look-ahead and discard preroll compensate
// the stretcher's input/output latency without delaying live dry monitoring.
class WarpPlayer {
    static constexpr int Q=64;
    signalsmith::stretch::SignalsmithStretch<float> stretch_{42};
    std::array<float,Q> outL_{},outR_{};
    const void* key_=nullptr;unsigned keyGeneration_=0;bool active_=false,chunkIsPreroll_=false;
    int index_=Q,preroll_=0;double cursor_=0,fraction_=0,previousRate_=1,expected_=0,rawCursor_=0;
    float mix_=0;
    void begin(const AudioSource& source,double pos,double rate){
        stretch_.reset();key_=source.identity;keyGeneration_=source.generation;active_=true;index_=Q;fraction_=0;mix_=0;previousRate_=rate;rawCursor_=pos;
        preroll_=((stretch_.outputLatency()+Q-1)/Q)*Q;
        double origin=pos+stretch_.outputLatency()*rate;
        stretch_.seek(source.at(origin),stretch_.inputLatency(),rate);
        cursor_=origin+stretch_.inputLatency();expected_=pos;
    }
public:
#ifdef LOOP4_TEST_DSP_COUNTER
    unsigned processCalls=0;
#endif
    explicit WarpPlayer(double rate){stretch_.presetDefault(2,float(rate),true);(void)SincTable::table();}
    void suspend(){active_=false;mix_=0;index_=Q;key_=nullptr;}
    unsigned lookBehind()const{return unsigned(stretch_.inputLatency()+stretch_.seekLength()+32);}
    unsigned lookAhead(double rate)const{return unsigned(stretch_.inputLatency()+stretch_.seekLength()+std::ceil(rate*(stretch_.outputLatency()+Q))+32);}
    bool active()const{return active_;}
    bool priming()const{return active_&&(preroll_>0||chunkIsPreroll_);}
    void sample(const AudioSource& source,double pos,double rate,int band,bool wanted,float& l,float& r){
        if(!wanted&&!active_){SincTable::read(source,pos,band,l,r);return;}
        double distance=std::fabs(pos-expected_);distance=std::min(distance,double(source.length)-std::min(distance,double(source.length)));
        if(!active_||key_!=source.identity||keyGeneration_!=source.generation||distance>std::max(2.,rate*2))begin(source,pos,rate);
        expected_=AudioSource::wrap(pos+rate,source.length);
        if(index_==Q){
            if(std::fabs(rate-previousRate_)>1e-5){
                double next=pos+stretch_.inputLatency()+rate*stretch_.outputLatency();
                stretch_.seek(source.at(next-stretch_.seekLength()),stretch_.seekLength(),rate);
                cursor_=next;fraction_=0;previousRate_=rate;
            }
            double count=Q*rate+fraction_;int input=int(std::floor(count));fraction_=count-input;
            float* output[]={outL_.data(),outR_.data()};
#ifdef LOOP4_TEST_DSP_COUNTER
            ++processCalls;
#endif
            stretch_.process(source.at(cursor_),input,output,Q);cursor_+=input;
            index_=0;chunkIsPreroll_=preroll_>0;if(chunkIsPreroll_)preroll_-=Q;
        }
        float rawL,rawR;
        if(chunkIsPreroll_){SincTable::read(source,rawCursor_,0,rawL,rawR);rawCursor_=AudioSource::wrap(rawCursor_+1,source.length);l=rawL;r=rawR;}
        else {
            // No duplicate resampling once the spectral path is fully mixed in.
            mix_+=std::clamp((wanted?1.f:0.f)-mix_,-1.f/256,1.f/256);
            if(wanted&&mix_>=1){l=outL_[index_];r=outR_[index_];}
            else {
                if(wanted){SincTable::read(source,rawCursor_,0,rawL,rawR);rawCursor_=AudioSource::wrap(rawCursor_+1,source.length);}
                else SincTable::read(source,pos,band,rawL,rawR);
                l=rawL+(outL_[index_]-rawL)*mix_;r=rawR+(outR_[index_]-rawR)*mix_;
            }

        }
        ++index_;if(!wanted&&mix_<=0)suspend();
    }
};
}
