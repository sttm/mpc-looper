#pragma once
// Original sttm four-slot engine. Host-clock/capture concepts informed by Slow Ops
// Looper (MIT, copyright 2026 Slow Ops); no Slow Ops implementation is copied.
// Only the audio thread mutates transport/slots. Published buffers are immutable.
// Readers pin a published buffer; audio reuses only unpinned spare buffers.
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <limits>
#include "TempoDSP.h"
#include "AudioMemory.h"
#include "DiskAudio.h"

namespace loop4 {
constexpr int Slots=4, Copies=3;
constexpr unsigned MaxBars=32,MaxSourceSeconds=12288;
constexpr size_t ProductionMemoryLimit=size_t(320)*1024*1024;
enum State { Empty, Armed, Recording, Playing, Stopped, Overdub, Finishing, Busy, TooLong, Paused, MemoryFull, DiskError };
enum Command : unsigned { Rec=1, Dub=2, Stop=4, Undo=8, Clear=16, Start=32, Launch=64 };
struct Buffer {
    AudioMemory l,r;loopdisk::Stream* disk=nullptr;
    AudioSource source()const{if(disk)return {nullptr,nullptr,length,this,disk,
        [](const void* p,int c,unsigned at){return const_cast<loopdisk::Stream*>(static_cast<const loopdisk::Stream*>(p))->sample(c,at);},
        [](const void* p,unsigned first,unsigned count){return const_cast<loopdisk::Stream*>(static_cast<const loopdisk::Stream*>(p))->available(first,count);},disk->generation.load()};
        return {l.data(),r.data(),length,this};}
    std::atomic<unsigned> ready{0},request{0};std::atomic<bool> denied{false};
    std::atomic<unsigned> readers{0};
    unsigned length=0; double bpm=120;
};
struct Slot {
    std::array<Buffer,Copies> data;
    std::atomic<int> published{-1},audible{-1};
    int current=-1,undo=-1,work=-1;
    double pos=0;unsigned written=0,targetLength=0,dubWritten=0,dubLength=0,dubStart=0;
    int plannedBars=0;double plannedBarBeats=4,dubBpm=120;bool boundary=true;
    std::unique_ptr<WarpPlayer> warpPlayer;float audibleWarp=0;double cachedRatio=-1;int sincBand=0;
    bool singleButton=false,recToDub=true;
    bool loopSynced=true,playSingle=false;int stopTiming=0;
    bool playing=false,recording=false,dubbing=false,dubArmed=false,finishDub=false;
    bool armRecord=false,finishRecord=false,clearPending=false,stopAfter=false,undoValid=false,undoAfter=false;
    double deadline=0,recordBpm=120;
    float level=1,gate=0,diskFade=1,lastRenderedL=0,lastRenderedR=0;unsigned cacheCheck=0;bool sourceReady=true,writeFailed=false;
};
struct View { State state=Empty; float progress=0; unsigned frames=0; double bpm=120; };
struct Pin {
    Buffer* buffer=nullptr;
    Pin()=default; Pin(const Pin&)=delete; Pin& operator=(const Pin&)=delete;
    Pin(Pin&& other) noexcept : buffer(other.buffer) {other.buffer=nullptr;}
    Pin& operator=(Pin&& other) noexcept {if(this!=&other){if(buffer)buffer->readers.fetch_sub(1);buffer=other.buffer;other.buffer=nullptr;}return *this;}
    ~Pin(){if(buffer)buffer->readers.fetch_sub(1);}
    explicit operator bool()const{return buffer!=nullptr;}
};
class Engine {
public:
    const double rate;
    const unsigned capacity;
    const int activeSlots;
    std::string sessionId;
    std::array<Slot,Slots> slots;
    Engine(double sr,const std::string& id,double seconds=32,int count=4,size_t memoryLimit=0,std::shared_ptr<loopdisk::Store> disk={}) : rate(sr),capacity(unsigned(std::ceil(sr*seconds))),activeSlots(count<=2?2:4),sessionId(id),memoryLimit_(disk?0:memoryLimit),disk_(std::move(disk)) {
        for(int n=0;n<activeSlots;++n){auto& s=slots[n];for(int copy=0;copy<Copies;++copy){auto& b=s.data[copy];if(disk_){b.disk=&disk_->stream(unsigned(n),unsigned(copy));continue;}b.l.resize(capacity,memoryLimit!=0);b.r.resize(capacity,memoryLimit!=0);b.request.store(std::min(capacity,unsigned(rate*2)));if(!commit(b,b.request.load()))throw std::runtime_error("Cannot reserve loop RAM");}s.warpPlayer=std::make_unique<WarpPlayer>(sr);}
    }
    loopdisk::Store* diskStore()const{return disk_.get();}
    void beginAudio(){if(disk_){disk_->beginAudio();for(auto& s:slots)s.cacheCheck=0;}}
    void endAudio(){if(disk_){for(auto& s:slots)s.audible.store(s.current,std::memory_order_release);disk_->endAudio();}}
    void playback(double bpm,bool sync,bool warp){
        bpm=std::clamp(bpm,20.,400.);warp=warp&&sync;
        if(std::fabs(bpm-tempo_)>.001||sync!=sync_||warp!=warp_){
            for(int n=0;n<activeSlots;++n){auto& s=slots[n];
                if(s.recording){finishCapture(s,false);tempoEditEnded_=true;}
                if(s.dubbing){abortWork(s);s.dubbing=false;s.work=-1;s.finishDub=false;tempoEditEnded_=true;}
            }
        }
        tempo_=bpm;sync_=sync;warp_=warp;
    }
    // Non-audio worker only: bounded chunks, physical pages touched before use.
    bool prefault(){bool worked=false;if(!memoryLimit_)return false;for(int n=0;n<activeSlots;++n)for(auto& b:slots[n].data){unsigned target=std::min(capacity,b.request.load());unsigned ready=b.ready.load();if(target>ready&&!b.denied.load()){worked=true;try{commit(b,std::min(target,ready+131072u));}catch(...){b.denied.store(true);memoryIssue_.store(true);}}}return worked;}
    size_t memoryBytes()const{return disk_?disk_->cacheBytes():allocated_.load();}
    bool takeMemoryIssue(){return memoryIssue_.exchange(false);}
    void recordAction(int n,int action){if(n>=0&&n<activeSlots){slots[n].singleButton=true;slots[n].recToDub=action==0;}}
    void clockRunning(bool running){clockRunning_=running;}
    bool takeEditEnded(){bool value=tempoEditEnded_;tempoEditEnded_=false;return value;}
    bool warming()const{for(int n=0;n<activeSlots;++n)if(slots[n].warpPlayer->priming())return true;return false;}
    bool pendingDiskCommits()const{if(!disk_)return false;for(int n=0;n<activeSlots;++n){const auto& s=slots[n];if(s.current>=0&&!s.data[s.current].disk->complete()&&!s.data[s.current].disk->error())return true;}return false;}
    bool diskProblem()const{for(int n=0;n<activeSlots;++n)if(diskError_[n]||slots[n].writeFailed)return true;return false;}
    bool hasPlayback()const{for(int n=0;n<activeSlots;++n)if(slots[n].playing)return true;return false;}

    Pin pin(int n) {
        Slot& s=slots[n]; Pin p;
        for(;;){int i=s.published.load();if(disk_){int a=s.audible.load(std::memory_order_acquire);if(a>=0&&s.data[a].disk->complete()&&!s.data[a].disk->error())i=a;}if(i<0)return p;
            s.data[i].readers.fetch_add(1);
            bool unchanged=s.published.load()==i;if(disk_&&s.audible.load()==i&&s.data[i].disk->complete()&&!s.data[i].disk->error())unchanged=true;
            if(unchanged){p.buffer=&s.data[i];return p;}
            s.data[i].readers.fetch_sub(1);
        }
    }
    // Non-real-time only, before this engine is handed to the audio thread.
    void import(int n,const float* l,const float* r,unsigned frames,double bpm) {
        auto& s=slots[n];auto& b=s.data[0];frames=std::min(frames,capacity);
        if(disk_){disk_->import(unsigned(n),0,frames,float(bpm),[&](unsigned i){return loopdisk::Frame{clean(l[i]),clean(r[i])};});b.length=frames;b.bpm=bpm;s.current=frames?0:-1;s.playing=frames>0;s.published.store(s.current);s.audible.store(s.current);return;}
        if(!commit(b,frames))throw std::runtime_error("Loop exceeds RAM budget");
        for(unsigned i=0;i<frames;++i){b.l[i]=clean(l[i]);b.r[i]=clean(r[i]);}
        b.length=frames;b.bpm=bpm;s.current=frames?0:-1;s.playing=frames>0;
        s.published.store(s.current);
    }
    static float clean(float x){return std::isfinite(x)?std::clamp(x,-4.f,4.f):0.f;}
    void stopOthers(int target) {
        for(int i=0;i<Slots;++i)if(i!=target){auto& s=slots[i];
            if(s.armRecord){abortWork(s);s.work=-1;}s.armRecord=false;s.dubArmed=false;
            if(s.recording)finishCapture(s,false);
            if(s.dubbing)s.finishDub=true;
        }
    }
    void loopOptions(int n,bool synced,bool single,int stop){auto& s=slots[n];s.loopSynced=synced;s.playSingle=single;s.stopTiming=std::clamp(stop,0,2);}
    void launchMode(bool measure){measureLaunch_=measure;}
    void enforceSingle(){
        int keep=-1;for(int n=0;n<activeSlots;++n)if(slots[n].playSingle&&(slots[n].playing||slots[n].recording)){if(keep<0)keep=n;}
        if(keep>=0)activateSingle(keep,false);
    }
    std::array<bool,Slots> transportFromZero_{};
    std::array<int,Slots> transportIntent_{{-1,-1,-1,-1}};
    std::array<double,Slots> transportDeadline_{};
    void queueTransport(unsigned mask,bool play,double beat,double gridBeat,double bar,int quant){
        double deadline=beat+boundary(gridBeat,bar,quant)-gridBeat;
        for(int n=0;n<activeSlots;++n)if((mask&(1u<<n))&&(slots[n].current>=0||(!play&&(slots[n].recording||slots[n].armRecord)))){transportIntent_[n]=play?1:0;transportFromZero_[n]=play&&(!slots[n].loopSynced||measureLaunch_);transportDeadline_[n]=deadline;}
    }
    void command(int n,unsigned bits,double beat,double bpm,double beatsPerBar,int quant,int bars,double gridBeat=std::numeric_limits<double>::quiet_NaN()) {
        if(n<0||n>=activeSlots)return;
        auto& s=slots[n];
        busy_[n]=false;
        if(!std::isfinite(gridBeat))gridBeat=beat;
        if(!s.loopSynced)quant=0;
        double deadline=beat+boundary(gridBeat,beatsPerBar,quant)-gridBeat;
        if((bits&Clear)||(!quant&&(bits&(Stop|Start|Launch))))transportIntent_[n]=-1;
        // A timed Stop also closes record/dub at the requested musical boundary.
        // Cancelling an armed capture remains immediate.
        if(!(bits&Clear)&&s.stopTiming&&(s.current>=0||s.recording)&&!s.armRecord){
            if(bits&Stop){queueTransport(1u<<n,false,beat,gridBeat,beatsPerBar,s.stopTiming);bits&=~unsigned(Stop|Rec|Launch);}
        }
        if(bits&Clear){busy_[n]=tooLong_[n]=memoryLimited_[n]=diskError_[n]=false;s.writeFailed=false;abortWork(s);s.armRecord=s.dubArmed=false;s.finishRecord=false;s.recording=s.dubbing=false;s.clearPending=true;if(!clockRunning_)clearSlot(s);return;}
        if(bits&Stop){if(s.armRecord){abortWork(s);s.work=-1;}s.armRecord=s.dubArmed=false;
            if(s.recording)finishCapture(s,true);
            else if(s.dubbing){s.finishDub=true;s.stopAfter=true;if(s.singleButton)s.playing=false;}
            else s.playing=false;
        }
        if(bits&Undo){
            if(s.dubbing){s.finishDub=true;s.undoAfter=true;}
            else if(!s.recording&&s.undoValid){std::swap(s.current,s.undo);publishChosen(s);s.pos=0;s.boundary=true;s.playing=s.current>=0;}
        }
        if(bits&Launch){if(s.current>=0)requestPlayback(n,beat,gridBeat,beatsPerBar);}
        if(bits&Start){if(s.current>=0)launch(n,true,true);}
        if(bits&Rec){
            if(s.recording){s.finishRecord=true;s.deadline=deadline;}
            else if(s.armRecord){abortWork(s);s.armRecord=false;s.work=-1;}
            else if(s.current>=0){
                if(!s.playing)requestPlayback(n,beat,gridBeat,beatsPerBar);
                else if(!s.singleButton)s.playing=false;
                else toggleDub(n,true);
            }
            else {
                stopOthers(n);s.work=spare(s);if(s.work<0){busy_[n]=true;return;}
                const double frames=bars?bars*beatsPerBar*60/bpm*rate:std::min(double(capacity),MaxBars*beatsPerBar*60/bpm*rate);
                if(frames>capacity+.5){tooLong_[n]=true;s.work=-1;return;}
                if(s.data[s.work].disk&&!s.data[s.work].disk->begin(0)){busy_[n]=true;s.work=-1;return;}
                s.targetLength=std::max(128u,unsigned(std::llround(frames)));
                s.plannedBars=bars;s.plannedBarBeats=beatsPerBar;s.recordBpm=bpm;s.deadline=deadline;s.armRecord=true;s.clearPending=false;tooLong_[n]=false;memoryLimited_[n]=false;diskError_[n]=false;s.writeFailed=false;busy_[n]=false;
            }
        }
        if((bits&Dub)&&s.current>=0&&!s.recording)toggleDub(n,s.singleButton);
    }
    // Start completed loops at their own zero after an explicit stopped-cursor
    // move. Never map song beats to loop phase or discard an unfinished edit.
    void restart(double beat=0){
        lastMixL_=lastMixR_=0;clockGate_=0;
        for(int n=0;n<activeSlots;++n){auto& s=slots[n];
            if(transportIntent_[n]>=0)transportDeadline_[n]=beat;
            if(s.recording||s.dubbing||s.armRecord||s.dubArmed){
                if(s.armRecord||s.finishRecord)s.deadline=beat;
                continue;
            }
            s.pos=0;if(s.current>=0&&s.data[s.current].disk)s.data[s.current].disk->requestCursor(0);
            s.boundary=true;s.cacheCheck=0;s.sourceReady=true;s.gate=0;s.diskFade=0;
            s.lastRenderedL=s.lastRenderedR=0;s.audibleWarp=0;s.warpPlayer->suspend();
        }
    }
    void all(bool play){transportIntent_.fill(-1);for(auto& s:slots){if(s.armRecord){abortWork(s);s.work=-1;}s.armRecord=s.dubArmed=false;if(s.recording)finishCapture(s,!play);if(s.dubbing){s.finishDub=true;s.stopAfter=!play;if(s.singleButton)s.playing=play;}else {s.playing=play&&s.current>=0;if(play){s.pos=0;s.boundary=true;}}}if(play)enforceSingle();}
    // Called once per frame. No allocation, host callbacks, file access or locks.
    void frame(float inL,float inR,float& outL,float& outR,double beat,const std::array<float,Slots>& levels,bool monitor) {
        bool automatic=disk_&&!disk_->inAudio();if(automatic)for(auto& s:slots)s.cacheCheck=0;
        loopdisk::Store::FrameGuard diskGuard(disk_.get());
        struct AutomaticEnd {Engine* engine;bool enabled;~AutomaticEnd(){if(enabled)for(auto& slot:engine->slots)slot.audible.store(slot.current);}} automaticEnd{this,automatic};
        for(int n=0;n<activeSlots;++n){auto& s=slots[n];
            if(s.current>=0&&s.data[s.current].disk){auto* stream=s.data[s.current].disk;
                if(stream->error()){diskError_[n]=true;abortWork(s);s.dubbing=false;s.work=-1;
                    int previous=s.published.load();if(previous>=0&&previous!=s.current&&!s.data[previous].disk->error())s.current=previous;else s.playing=false;
                    s.cacheCheck=0;}
                else if(stream->complete())s.published.store(s.current);
            }
        }
        inL=clean(inL);inR=clean(inR);
        const float clockStep=float(1/(rate*.005));
        clockGate_+=std::clamp((clockRunning_?1.f:0.f)-clockGate_,-clockStep,clockStep);
        for(int n=0;n<activeSlots;++n){
            if(transportIntent_[n]>=0&&beat+1e-9>=transportDeadline_[n]){
                bool play=transportIntent_[n]!=0;transportIntent_[n]=-1;
                if(play)launch(n,transportFromZero_[n]);else {int timing=slots[n].stopTiming;slots[n].stopTiming=0;command(n,Stop,beat,tempo_,4,0,0);slots[n].stopTiming=timing;}
            }
        }
        if(!clockRunning_){outL=lastMixL_*clockGate_+(monitor?inL:0);outR=lastMixR_*clockGate_+(monitor?inR:0);return;}
        for(int n=0;n<activeSlots;++n)if(slots[n].playSingle&&slots[n].armRecord&&beat+1e-9>=slots[n].deadline)activateSingle(n);
        float sumL=0,sumR=0;
        for(int n=0;n<activeSlots;++n){auto& s=slots[n];
            if(s.clearPending){s.gate=std::max(0.f,s.gate-float(1/(rate*.005)));
                if(s.gate<=0)clearSlot(s);}
            if(s.armRecord && beat+1e-9>=s.deadline){s.armRecord=false;
                double length=s.plannedBars?s.plannedBars*s.plannedBarBeats*60/tempo_*rate:std::min(double(capacity),MaxBars*s.plannedBarBeats*60/tempo_*rate);
                if(length>capacity+.5){tooLong_[n]=true;abortWork(s);s.work=-1;continue;}
                s.targetLength=std::max(128u,unsigned(std::llround(length)));s.recordBpm=tempo_;
                s.recording=true;s.written=0;s.finishRecord=false;}

            if(s.recording){
                if(s.finishRecord && beat+1e-9>=s.deadline)finishCapture(s,false,s.singleButton&&s.recToDub);
                else {auto& w=s.data[s.work];
                    if(!put(w,s.written,inL,inR)){if(w.disk){diskError_[n]=true;abortWork(s);s.recording=false;s.work=-1;}else {finishCapture(s,false);memoryLimited_[n]=true;memoryIssue_.store(true);}continue;}
                    ++s.written;if(s.written>=s.targetLength){finishCapture(s,false,s.singleButton&&s.recToDub);continue;}}
            }
            if(s.current<0)continue;
            auto& b=s.data[s.current];if(!b.length)continue;
            if(s.pos>=b.length)s.pos=0;
            if(s.dubArmed&&(s.boundary||s.singleButton)&&s.playing){s.dubArmed=false;beginDub(n,!s.singleButton);}
            s.boundary=false;
            double ratio=sync_?tempo_/b.bpm:1;
            if(ratio!=s.cachedRatio){s.cachedRatio=ratio;s.sincBand=SincTable::band(ratio);}
            AudioSource source=b.source();float renderedL,renderedR;
            if(!s.playing&&s.gate<=0&&!s.dubbing){s.warpPlayer->suspend();s.pos=AudioSource::wrap(s.pos+ratio,b.length);continue;}
            float wanted=warp_&&std::fabs(ratio-1)>1e-8?1.f:0.f;
            s.audibleWarp+=std::clamp(wanted-s.audibleWarp,-1.f/256,1.f/256);
            if(b.disk&&(s.cacheCheck++%64==0)){
                unsigned behind=warp_?s.warpPlayer->lookBehind():16;
                unsigned ahead=warp_?s.warpPlayer->lookAhead(ratio):unsigned(std::ceil(64*ratio))+32;
                unsigned first=unsigned(AudioSource::wrap(std::floor(s.pos)-behind,b.length));
                s.sourceReady=source.ready(first,std::min(b.length,behind+ahead));
                b.disk->requestCursor(unsigned(s.pos));if(!s.sourceReady){disk_->miss();s.warpPlayer->suspend();}
            }
            if(!s.sourceReady){
                s.diskFade=std::max(0.f,s.diskFade-float(1/(rate*.005)));renderedL=s.lastRenderedL;renderedR=s.lastRenderedR;
                if(s.dubbing){abortWork(s);s.dubbing=false;s.work=-1;s.finishDub=false;diskError_[n]=true;}
            }else {
                if(warp_||s.warpPlayer->active()){
                    s.warpPlayer->sample(source,s.pos,ratio,s.sincBand,warp_,renderedL,renderedR);
                    if(s.audibleWarp<1){float rawL,rawR;SincTable::read(source,s.pos,s.sincBand,rawL,rawR);
                        renderedL=rawL+(renderedL-rawL)*s.audibleWarp;renderedR=rawR+(renderedR-rawR)*s.audibleWarp;}
                }else SincTable::read(source,s.pos,s.sincBand,renderedL,renderedR);
                s.lastRenderedL=renderedL;s.lastRenderedR=renderedR;s.diskFade=std::min(1.f,s.diskFade+float(1/(rate*.005)));
            }
            if(s.dubbing){auto& w=s.data[s.work];unsigned dest=(s.dubStart+s.dubWritten)%s.dubLength;
                const float addL=s.finishDub?0:inL,addR=s.finishDub?0:inR;
                if(!put(w,dest,std::clamp(renderedL+addL,-1.f,1.f),std::clamp(renderedR+addR,-1.f,1.f))){
                    if(w.disk){diskError_[n]=true;abortWork(s);}else {memoryLimited_[n]=true;memoryIssue_.store(true);}
                    s.dubbing=false;s.work=-1;s.finishDub=false;
                }
            }
            s.level+=.002f*(levels[n]-s.level);
            const float target=s.playing&&!s.clearPending?1.f:0.f;
            if(!s.clearPending)s.gate+=std::clamp(target-s.gate,-float(1/(rate*.005)),float(1/(rate*.005)));
            float seam=1;
            unsigned fade=std::min(unsigned(rate*.003),b.length/4);
            if(fade){if(s.pos<fade)seam=float(s.pos)/fade;else if(s.pos>=b.length-fade)seam=std::max(0.f,float(b.length-1-s.pos)/fade);}
            sumL+=renderedL*s.level*s.gate*seam*s.diskFade;sumR+=renderedR*s.level*s.gate*seam*s.diskFade;
            s.pos+=ratio;
            bool wrapped=s.pos>=b.length;if(wrapped){s.pos=AudioSource::wrap(s.pos,b.length);s.boundary=true;}
            if(s.dubbing&&++s.dubWritten>=s.dubLength){
                auto& w=s.data[s.work];w.length=s.dubLength;w.bpm=s.dubBpm;
                if(w.disk&&!w.disk->seal(w.length,float(w.bpm))){diskError_[n]=true;s.dubbing=false;s.work=-1;continue;}
                s.current=s.work;if(!w.disk||w.disk->complete())s.published.store(s.current);s.cacheCheck=0;s.pos=s.dubStart;s.boundary=s.dubStart==0;
                if(s.finishDub){s.dubbing=false;s.work=-1;busy_[n]=false;if(s.stopAfter)s.playing=false;
                    if(s.undoAfter){std::swap(s.current,s.undo);publishChosen(s);s.undoAfter=false;}}
                else {s.dubWritten=0;s.work=spare(s);if(s.work<0){s.dubbing=false;busy_[n]=true;}else if(s.data[s.work].disk&&!s.data[s.work].disk->begin(s.dubStart)){s.dubbing=false;s.work=-1;busy_[n]=true;}}
            }

        }
        lastMixL_=sumL;lastMixR_=sumR;outL=sumL*clockGate_+(monitor?inL:0);outR=sumR*clockGate_+(monitor?inR:0);
    }
    View view(int n)const {const auto& s=slots[n];View v;
        v.frames=s.current>=0?s.data[s.current].length:0;v.bpm=s.current>=0?s.data[s.current].bpm:120;
        v.progress=s.recording?float(s.written)/std::max(1u,s.targetLength):v.frames?float(s.pos)/v.frames:0;
        v.state=(diskError_[n]||s.writeFailed)?DiskError:memoryLimited_[n]?MemoryFull:tooLong_[n]?TooLong:busy_[n]?Busy:s.armRecord||s.dubArmed?Armed:s.recording?Recording:s.dubbing?(s.finishDub?(s.singleButton?(s.stopAfter?Stopped:s.undoAfter?Finishing:Playing):Finishing):Overdub):s.current<0?Empty:s.playing?Playing:Stopped;
        if(transportIntent_[n]>=0&&s.current>=0)v.state=Armed;
        if(!clockRunning_&&!memoryLimited_[n]&&!diskError_[n]&&!s.writeFailed&&(s.playing||s.recording))v.state=Paused;
        return v;
    }
    bool editing()const {for(const auto& s:slots)if(s.recording||s.dubbing||s.armRecord||s.dubArmed||(s.current>=0&&s.data[s.current].disk&&!s.data[s.current].disk->complete()&&!s.data[s.current].disk->error()))return true;return false;}
private:
    std::array<bool,Slots> busy_{},tooLong_{},memoryLimited_{},diskError_{};
    size_t memoryLimit_=0;std::shared_ptr<loopdisk::Store> disk_;std::atomic<size_t> allocated_{0};std::atomic<bool> memoryIssue_{false};
    static void publishChosen(Slot& s){if(s.current<0||!s.data[s.current].disk||s.data[s.current].disk->complete())s.published.store(s.current);}
    static void abortWork(Slot& s){if(s.work>=0&&s.data[s.work].disk)s.data[s.work].disk->abort();}
    bool put(Buffer& b,unsigned at,float l,float r){if(b.disk)return !b.disk->error()&&b.disk->write(at,l,r);if(!writable(b,at))return false;b.l[at]=l;b.r[at]=r;return true;}
    bool commit(Buffer& b,unsigned count){
        unsigned before=b.ready.load();if(count<=before)return true;
        unsigned end=std::min(capacity,(count+1023u)&~1023u);size_t bytes=size_t(end-before)*sizeof(float)*2;
        if(memoryLimit_&&allocated_.load()+bytes>memoryLimit_){b.denied.store(true);memoryIssue_.store(true);return false;}
        if(!b.l.ensure(end)||!b.r.ensure(end)){b.denied.store(true);memoryIssue_.store(true);return false;}
        if(memoryLimit_){b.l.touch(before,end);b.r.touch(before,end);b.l.publish();b.r.publish();}
        allocated_.fetch_add(bytes);b.ready.store(end,std::memory_order_release);return true;
    }
    void request(Buffer& b,unsigned count){count=std::min(capacity,count);unsigned previous=b.request.load();if(previous<count)b.request.store(count); }
    bool writable(Buffer& b,unsigned at){
        if(memoryLimit_&&(at%1024==0||at>=b.ready.load()))request(b,std::min(capacity,at+unsigned(rate*2)));
        return !memoryLimit_||at<b.ready.load(std::memory_order_acquire);
    }
    bool measureLaunch_=false;
    double tempo_=120;bool sync_=false,warp_=false,tempoEditEnded_=false,clockRunning_=true;
    float clockGate_=1,lastMixL_=0,lastMixR_=0;
    static void clearSlot(Slot& s){s.undo=s.current;s.undoValid=s.current>=0;s.current=s.work=-1;s.pos=s.written=0;s.playing=false;s.published.store(-1);s.clearPending=false;}
    void activateSingle(int target,bool cancelPending=true){
        if(!slots[target].playSingle)return;
        for(int n=0;n<activeSlots;++n)if(n!=target&&slots[n].playSingle){
            auto& s=slots[n];
            if(!cancelPending&&!s.playing&&!s.recording)continue;
            if(cancelPending)transportIntent_[n]=-1;
            if(s.armRecord){abortWork(s);s.work=-1;}s.armRecord=s.dubArmed=false;
            if(s.recording)finishCapture(s,true);
            if(s.dubbing){s.finishDub=true;s.stopAfter=true;}
            s.playing=false;
        }
    }
    void launch(int n,bool fromZero,bool retrigger=false){
        auto& s=slots[n];if(s.current<0)return;
        if(fromZero&&s.playing&&!retrigger)return; // Group Play does not retrigger enabled loops.
        if(fromZero&&(s.dubbing||s.recording)){busy_[n]=true;return;} // Never corrupt an unfinished edit tail.
        activateSingle(n);s.playing=true;s.stopAfter=false;busy_[n]=false;
        if(fromZero){
            s.pos=0;s.boundary=true;s.cacheCheck=0;s.sourceReady=true;s.gate=0;s.diskFade=0;
            s.lastRenderedL=s.lastRenderedR=0;s.audibleWarp=0;s.warpPlayer->suspend();
            if(s.data[s.current].disk)s.data[s.current].disk->requestCursor(0);
        }
    }
    void requestPlayback(int n,double beat,double gridBeat,double bar){
        auto& s=slots[n];
        if(s.loopSynced&&measureLaunch_)queueTransport(1u<<n,true,beat,gridBeat,bar,2);
        else launch(n,!s.loopSynced);
    }
    bool beginDub(int n,bool restart){
        auto& s=slots[n];auto& b=s.data[s.current];double ratio=sync_?tempo_/b.bpm:1;
        double length=b.length/ratio;
        if(length>capacity+.5){tooLong_[n]=true;return false;}
        int work=spare(s);if(work<0){busy_[n]=true;return false;}
        unsigned dest=restart?0:std::min(std::max(128u,unsigned(std::llround(length)))-1,unsigned(std::llround(s.pos/ratio)));auto& writableBuffer=s.data[work];
        if(memoryLimit_&&dest>=writableBuffer.ready.load()){
            request(writableBuffer,dest+unsigned(rate*2));busy_[n]=true;
            if(!writableBuffer.denied.load())s.dubArmed=true;else {memoryLimited_[n]=true;memoryIssue_.store(true);}return false;
        }
        if(s.data[work].disk&&!s.data[work].disk->begin(dest)){busy_[n]=true;return false;}
        busy_[n]=false;diskError_[n]=false;s.writeFailed=false;s.work=work;s.undo=s.current;s.undoValid=true;s.dubbing=true;s.finishDub=false;s.stopAfter=false;s.undoAfter=false;
        s.dubWritten=0;s.dubLength=std::max(128u,unsigned(std::llround(length)));s.dubBpm=sync_?tempo_:b.bpm;
        if(restart)s.pos=0;
        s.dubStart=dest;return true;
    }
    void toggleDub(int n,bool immediate){
        auto& s=slots[n];
        if(s.dubbing){if(s.singleButton&&s.finishDub&&!s.stopAfter&&!s.undoAfter){stopOthers(n);activateSingle(n);s.finishDub=false;}else s.finishDub=true;}
        else if(s.dubArmed)s.dubArmed=false;
        else {stopOthers(n);activateSingle(n);s.playing=true;if(immediate)beginDub(n,false);else s.dubArmed=true;}
    }
    static double boundary(double beat,double bar,int quant){double unit=quant==2?bar:1;return quant?std::ceil(beat/unit-1e-9)*unit:beat;}
    int spare(const Slot& s)const {for(int i=0;i<Copies;++i)if(i!=s.current&&i!=s.undo&&i!=s.published.load()&&(!disk_||i!=s.audible.load())&&s.data[i].readers.load()==0&&(!s.data[i].disk||s.data[i].disk->reusable()))return i;return -1;}
    void finishCapture(Slot& s,bool stopped,bool overdub=false){
        s.recording=s.finishRecord=false;
        if(s.written<128){abortWork(s);s.work=-1;return;}
        auto& b=s.data[s.work];b.length=s.written;b.bpm=s.recordBpm;
        if(b.disk&&!b.disk->seal(b.length,float(b.bpm))){abortWork(s);s.work=-1;s.playing=s.current>=0;s.writeFailed=true;return;}
        s.undo=s.current;s.undoValid=true;s.current=s.work;s.work=-1;s.pos=0;s.cacheCheck=0;s.sourceReady=true;s.boundary=true;s.playing=!stopped;if(!b.disk||b.disk->complete())s.published.store(s.current);s.gate=0;s.dubArmed=overdub&&!stopped;
    }
};
}
