// stereo input -> four independent loops + optional dry monitor -> stereo sum.
// Disk/state work never runs in process(). Host notifications use a 20 Hz worker.
#include "mpc_plugin.h"
#include "DiskFiles.h"
#include "HostTimeline.h"
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <unistd.h>

namespace {
enum { Reserved,Monitor,Quantize,Metronome,AllPlay,AllStop,ExportAll,SaveSession,RestoreSession,Tempo,FileStatus,FirstSlot };
enum { REC,DUB,STOP,UNDO,CLEAR,EXPORT,LEVEL,LENGTH,STATUS,PROGRESS,Stride };
constexpr int ix(int bank,int field){return FirstSlot+bank*Stride+field;}
constexpr unsigned ExportBits=15,SaveBit=16,RestoreBit=32;
std::mutex ownerMutex;std::unordered_map<std::string,const void*> sessionOwners;
constexpr int SelectedLoop=FirstSlot+4*Stride,RecordSelected=SelectedLoop+1,SelectedLevel=SelectedLoop+2,NextLoop=SelectedLoop+3;
constexpr int LoopMode=NextLoop+1,SyncTempo=LoopMode+1,Warp=LoopMode+2,ExportMode=LoopMode+3,WarpState=LoopMode+4,RecAction=LoopMode+5,DiskState=RecAction+4,DiskMisses=DiskState+1,DiskErrors=DiskState+2,DiskLock=DiskState+3,DiskCache=DiskState+4;
constexpr int GroupFirst=DiskCache+1,GroupPlay=GroupFirst+4,GroupStop=GroupPlay+1,RestartLoops=GroupStop+1;
constexpr int LoopSyncFirst=RestartLoops+1,PlayModeFirst=LoopSyncFirst+4,ResumeMode=PlayModeFirst+4,StopModeFirst=ResumeMode+1;
constexpr int FaderMax=StopModeFirst+4,UiLevelFirst=FaderMax+1;
std::string newId(){static std::atomic<unsigned> sequence{0};char id[33];
    auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    std::snprintf(id,sizeof id,"%016llx%016llx",(unsigned long long)stamp,((unsigned long long)getpid()<<32)|sequence.fetch_add(1));return id;}
}
class MPCLooper final : public mpc::Plugin {
    std::atomic<loop4::Engine*> active_{nullptr},pending_{nullptr},retired_{nullptr};
    std::atomic<unsigned> readers_{0},rate_{44100},jobs_{0},editing_{0},playing_{0},loadGeneration_{0};
    std::atomic<bool> tempoEditEnded_{false};
    std::array<std::atomic<unsigned>,4> actions_{};
    std::atomic<int> pendingFaderSelection_{-1};
    static inline thread_local bool syncing_=false;
    std::atomic<unsigned> groupActions_{0};
    std::array<std::atomic<unsigned>,3> notify_{};
    std::mutex garbageMutex_,messageMutex_;
    std::vector<loop4::Engine*> garbage_;
    std::string message_="Ready / export to Content/Exports";
    std::string root_,instanceId_=newId();
    std::thread worker_;std::atomic<bool> running_{true};
    double beat_=0;HostTimeline timeline_;
    unsigned exportSequence_=0,lastDiskMisses_=0;int statusHold_=0,diskMissHold_=0;
    struct Reader {
        MPCLooper& p;loop4::Engine* engine;
        explicit Reader(MPCLooper& owner):p(owner){p.readers_.fetch_add(1);engine=p.active_.load();}
        ~Reader(){p.readers_.fetch_sub(1);}
    };
    static mpc::PluginInfo identity(){mpc::PluginInfo i;i.name="MPC Looper";i.vendor="sttm";i.version=601;i.uniqueId=MPC_FOURCC('S','n','Y','q');i.isInstrument=false;i.numInputs=2;i.numOutputs=2;return i;}
    float faderCeiling()const{return getInt(FaderMax)?6.f:0.f;}
    void syncFaderUi(int n){
        bool previous=syncing_;syncing_=true;
        float value=(std::clamp(get(ix(n,LEVEL)),-60.f,faderCeiling())+60)/(60+faderCeiling());
        if(std::fabs(getNormalised(UiLevelFirst+n)-value)>1e-7f){setNormalised(UiLevelFirst+n,value);mark(UiLevelFirst+n);}
        syncing_=previous;
    }
    void selectFromFader(int n){
        bool previous=syncing_;syncing_=true;
        set(SelectedLoop,float(n));set(SelectedLevel,std::min(get(ix(n,LEVEL)),faderCeiling()));
        syncing_=previous;mark(SelectedLoop);mark(SelectedLevel);
    }
    void flushFaderSelection(){int n=pendingFaderSelection_.exchange(-1);if(n>=0&&n<4)selectFromFader(n);}
    void mark(int i){notify_[size_t(i)/32].fetch_or(1u<<(i%32));}
    void message(const std::string& text){{std::lock_guard<std::mutex> lock(messageMutex_);message_=text;}set(FileStatus,1-get(FileStatus));mark(FileStatus);}
    void adopt(loop4::Engine& engine){std::lock_guard<std::mutex> lock(ownerMutex);
        auto found=sessionOwners.find(engine.sessionId);
        if(found!=sessionOwners.end()&&found->second!=this){engine.sessionId=instanceId_;return;}
        sessionOwners.erase(instanceId_);instanceId_=engine.sessionId;sessionOwners[instanceId_]=this;
    }
    void queueEngine(loop4::Engine* engine){auto* old=pending_.exchange(engine);if(old){std::lock_guard<std::mutex> lock(garbageMutex_);garbage_.push_back(old);}}
    void files(unsigned jobs){
        try {Reader reader(*this);auto* e=reader.engine;if(!e)throw std::runtime_error("Start audio first");
            if(jobs&RestoreBit){auto path=std::filesystem::path(root_)/"Content/Sessions"/e->sessionId/"SessionDisk.loop4";if(!std::filesystem::exists(path))path=path.parent_path()/"Session.loop4";auto bytes=loop4::read(path);
                bool oldLengthSchema=bytes.size()>=8&&std::memcmp(bytes.data(),"STLPSAV1",8)==0;
                if(bytes.size()<12 || (!oldLengthSchema&&std::memcmp(bytes.data(),"STLPSAV2",8)!=0))throw std::runtime_error("Invalid backup");
                unsigned stored=loop4::u32(bytes.data()+8);if(stored<55||stored>unsigned(numParams())||bytes.size()<12+size_t(stored)*4)throw std::runtime_error("Invalid backup settings");
                size_t offset=12+size_t(stored)*4;
                auto restored=std::unique_ptr<loop4::Engine>(loop4::decodeDisk(bytes.data()+offset,bytes.size()-offset,rate_.load(),4,std::filesystem::path(root_)/"Content"));
                for(unsigned i=0;i<stored;++i){float value=loop4::f32(bytes.data()+12+size_t(i)*4);if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("Invalid setting");}
                if(stored<=unsigned(Warp)){set(SyncTempo,0);set(Warp,0);set(ExportMode,1);}
                setNormalised(FaderMax,stored>unsigned(FaderMax)?loop4::f32(bytes.data()+12+size_t(FaderMax)*4):0.f);
                for(int i=0;i<int(stored);++i)if((i>=LoopSyncFirst&&i<StopModeFirst+4)||(i>=GroupFirst&&i<GroupFirst+4)||i==Monitor||i==Quantize||i==Metronome||i==SelectedLoop||i==SelectedLevel||i==SyncTempo||i==Warp||i==ExportMode||(i>=RecAction&&i<RecAction+4)||(i>=FirstSlot&&((i-FirstSlot)%Stride==LEVEL||(i-FirstSlot)%Stride==LENGTH))){
                    float value=loop4::f32(bytes.data()+12+size_t(i)*4);
                    if(oldLengthSchema&&i>=FirstSlot&&i<SelectedLoop&&(i-FirstSlot)%Stride==LENGTH)set(i,std::round(value*4));
                    else setNormalised(i,value);
                }
                adopt(*restored);loadGeneration_.fetch_add(1);queueEngine(restored.release());message("Backup queued / audio restored next block");return;}
            if(editing_.load()||e->pendingDiskCommits())throw std::runtime_error("Finish REC/DUB and wait for disk commit");
            if(jobs&SaveBit){std::vector<uint8_t> data{'S','T','L','P','S','A','V','2'};loop4::put32(data,unsigned(numParams()));
                for(int i=0;i<numParams();++i)loop4::putFloat(data,getNormalised(i));
                loop4::encodeDisk(e,data,"Sessions/"+e->sessionId+"/Collected/"+loopdisk::token());loop4::writeAtomic(std::filesystem::path(root_)/"Content/Sessions"/e->sessionId/"SessionDisk.loop4",data);message("Session collected; save MPC project to retain collected references");}
            unsigned mask=jobs&ExportBits;if(!mask)return;
            std::array<loop4::Pin,4> pins;unsigned count=0;
            for(int n=0;n<4;++n)if(mask&(1u<<n)){pins[n]=e->pin(n);if(pins[n])++count;}
            if(!count)throw std::runtime_error("No recorded loops to export");
            auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
            auto folder=std::filesystem::path(root_)/"Content/Exports"/(e->sessionId.substr(0,8)+"-"+std::to_string(stamp)+"-"+std::to_string(++exportSequence_));
            std::string manifest="MPC Looper WAV export\nOne original cycle per file. Position starts at the loop downbeat; different loop lengths are preserved.\n";
            bool current=getInt(ExportMode)==1&&getBool(SyncTempo),warped=current&&getBool(Warp);double targetBpm=std::clamp(double(get(Tempo)),20.,400.);
            manifest+=current?(warped?"Current tempo / pitch-preserving Warp\n":"Current tempo / varispeed\n"):"Original capture tempo\n";
            for(int n=0;n<4;++n)if(pins[n]){auto& b=*pins[n].buffer;double bpm=current?targetBpm:b.bpm;unsigned frames=current?loop4::tempoFrames(b,bpm):b.length;
                char file[100];std::snprintf(file,sizeof file,"Loop-%d_%.2fbpm_%uframes.wav",n+1,bpm,frames);
                if(b.disk)loop4::wavDisk(folder/file,b,e->rate,bpm,warped);else if(current)loop4::wavTempo(folder/file,b,e->rate,bpm,warped);else loop4::wav(folder/file,b,e->rate);
                manifest+=std::string(file)+"\n";}
            loop4::writeAtomic(folder/"LOOPS.txt",std::vector<uint8_t>(manifest.begin(),manifest.end()));
            message("WAV "+std::to_string(count)+": Content/Exports/"+folder.filename().string());
        }catch(const std::exception& ex){message(std::string("File: ")+ex.what());}
    }
    void work(){std::vector<float> previous(size_t(numParams()),-1);
        while(running_.load()){
            if(auto* old=retired_.exchange(nullptr)){std::lock_guard<std::mutex> lock(garbageMutex_);garbage_.push_back(old);}
            if(readers_.load()==0){std::lock_guard<std::mutex> lock(garbageMutex_);for(auto* old:garbage_)delete old;garbage_.clear();}
            unsigned job=jobs_.exchange(0);
            if(job&63u)files(job&63u);
            {Reader reader(*this);if(reader.engine&&reader.engine->takeMemoryIssue())message("RAM limit / buffering: shorten loop; completed source kept");}
            if(tempoEditEnded_.exchange(false))message("Tempo/mode changed: unfinished edit ended");
            std::array<unsigned,3> forced{{notify_[0].exchange(0),notify_[1].exchange(0),notify_[2].exchange(0)}};
            for(int i=0;i<numParams();++i){float value=getNormalised(i);if(value!=previous[size_t(i)]||(forced[size_t(i)/32]&(1u<<(i%32)))){previous[size_t(i)]=value;notifyHostParameterChanged(i);}}
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
public:
    MPCLooper():mpc::Plugin(identity()){
        auto reserved=mpc::floatParam("reserved","Host",0,1,0);reserved.automatable=false;addParam(reserved);
        addParam(mpc::boolParam("monitor","Monitor",true));
        addParam(mpc::choiceParam("quantize","Quantize",{"Off","Beat","Bar"},2));
        auto legacyMetro=mpc::boolParam("metronome","Legacy (off)",false);legacyMetro.automatable=false;addParam(legacyMetro);
        for(const auto& p:std::array<std::pair<const char*,const char*>,5>{{{"all_play","All Play"},{"all_stop","All Stop"},{"export_all","Export All"},{"save_session","Save Session"},{"restore_session","Load Backup"}}})addParam(mpc::boolParam(p.first,p.second,false));
        auto tempo=mpc::floatParam("tempo","Host BPM",20,400,120,"BPM",1);tempo.automatable=false;addParam(tempo);
        auto status=mpc::floatParam("file_status","File Status",0,1,0);status.automatable=false;addParam(status);
        for(int n=0;n<4;++n){std::string prefix="loop"+std::to_string(n+1)+"_",label="L"+std::to_string(n+1)+" ";
            const char* ids[]={"rec","dub","stop","undo","clear","export"};const char* names[]={"Rec/Play","Overdub","Stop","Undo/Redo","Clear","Export WAV"};
            for(int k=0;k<6;++k)addParam(mpc::boolParam((prefix+ids[k]).c_str(),(label+names[k]).c_str(),false));
            addParam(mpc::floatParam((prefix+"level").c_str(),(label+"Level").c_str(),-60,6,0,"dB",1));
            addParam(mpc::choiceParam((prefix+"bars").c_str(),(label+"Length").c_str(),{"Free","1 bar","2 bars","4 bars","8 bars","16 bars","32 bars"},3));
            auto s=mpc::choiceParam((prefix+"status").c_str(),(label+"State").c_str(),{"EMPTY","ARMED","REC","PLAY","STOP","DUB","FINISH","BUSY","TOO LONG","PAUSED","RAM LIMIT","DISK ERROR"},0);s.automatable=false;addParam(s);
            auto p=mpc::floatParam((prefix+"progress").c_str(),(label+"Position").c_str(),0,1,0);p.automatable=false;addParam(p);
        }
        addParam(mpc::choiceParam("selected_loop","Selected Loop",{"Loop 1","Loop 2","Loop 3","Loop 4"},0));
        addParam(mpc::boolParam("record_selected","Record Selected",false));
        addParam(mpc::floatParam("selected_level","Selected Level",-60,6,0,"dB",1));
        addParam(mpc::boolParam("next_loop","Next Loop",false));
        // Keep index 55 and its range for old projects; it no longer changes the engine.
        auto count=mpc::choiceParam("loop_mode","Loop Count (fixed)",{"2 loops","4 loops"},1);count.automatable=false;addParam(count);
        addParam(mpc::boolParam("sync_tempo","Sync Tempo",false));
        addParam(mpc::boolParam("warp","Warp",false));
        addParam(mpc::choiceParam("export_mode","WAV Tempo",{"Original","Current Tempo"},1));
        auto warpState=mpc::choiceParam("warp_state","Tempo State",{"OFF","VARISPEED","PRIMING","WARP"},0);warpState.automatable=false;addParam(warpState);
        for(int n=0;n<4;++n)addParam(mpc::choiceParam(("loop"+std::to_string(n+1)+"_rec_action").c_str(),("L"+std::to_string(n+1)+" Rec Action").c_str(),{"REC > DUB","REC > PLAY"},0));
        auto diskState=mpc::choiceParam("disk_state","Disk",{"READY","WRITING","BUFFERING","ERROR"},0);diskState.automatable=false;addParam(diskState);
        for(auto p:{mpc::floatParam("disk_read_misses","Read Misses",0,1000000000,0),mpc::floatParam("disk_errors","Disk Errors",0,1000000000,0),mpc::boolParam("disk_locked","Cache Locked",false),mpc::floatParam("disk_cache_mib","Audio Cache",0,32,0,"MiB",0)}){p.automatable=false;addParam(p);}
        for(int n=0;n<4;++n)addParam(mpc::boolParam(("group_loop"+std::to_string(n+1)).c_str(),("Group Loop "+std::to_string(n+1)).c_str(),n<2));
        addParam(mpc::boolParam("group_play","Group Play",false));
        addParam(mpc::boolParam("group_stop","Group Stop",false));
        addParam(mpc::boolParam("restart_loops","Restart Loops",false));
        for(int n=0;n<4;++n)addParam(mpc::boolParam(("loop"+std::to_string(n+1)+"_sync").c_str(),("L"+std::to_string(n+1)+" Loop Sync").c_str(),true));
        for(int n=0;n<4;++n)addParam(mpc::choiceParam(("loop"+std::to_string(n+1)+"_play_mode").c_str(),("L"+std::to_string(n+1)+" Play Mode").c_str(),{"MULTI","SINGLE"},0));
        addParam(mpc::choiceParam("resume_mode","Loop Sync Mode",{"PHASE","MEASURE"},0));
        for(int n=0;n<4;++n)addParam(mpc::choiceParam(("loop"+std::to_string(n+1)+"_stop_mode").c_str(),("L"+std::to_string(n+1)+" Stop Mode").c_str(),{"IMMEDIATE","BEAT","MEASURE"},0));
        addParam(mpc::choiceParam("fader_max","Fader Max",{"0 dB","+6 dB"},0));
        for(int n=0;n<4;++n)addParam(mpc::floatParam(("loop"+std::to_string(n+1)+"_fader").c_str(),("L"+std::to_string(n+1)+" Fader").c_str(),0,1,1,"dB",1));
        root_=mpc::detail::computePluginFolder();
        {std::lock_guard<std::mutex> lock(ownerMutex);sessionOwners[instanceId_]=this;}
    }
    ~MPCLooper() override {running_.store(false);if(worker_.joinable())worker_.join();delete active_.load();delete pending_.load();delete retired_.load();for(auto* p:garbage_)delete p;std::lock_guard<std::mutex> lock(ownerMutex);sessionOwners.erase(instanceId_);}
    void prepare(double sr,int) override {
        unsigned rate=std::isfinite(sr)?unsigned(std::clamp(sr,8000.,96000.)):44100;rate_.store(rate);
        {Reader reader(*this);if(!reader.engine){auto next=std::unique_ptr<loop4::Engine>(loop4::makeDiskEngine(rate,instanceId_,4,std::filesystem::path(root_)/"Content"));next->diskStore()->start();active_.store(next.release());}
            else if(reader.engine->rate!=rate){std::vector<uint8_t> bytes;loop4::encodeDisk(reader.engine,bytes);queueEngine(loop4::decodeDisk(bytes.data(),bytes.size(),rate,4,std::filesystem::path(root_)/"Content"));}}
        if(!worker_.joinable())worker_=std::thread([this]{work();});
    }
    void reset() override {pendingFaderSelection_.store(-1);for(int n=0;n<4;++n)syncFaderUi(n);tempoEditEnded_.store(false);statusHold_=int(rate_.load()/5);for(auto& a:actions_)a.store(0);jobs_.store(0);groupActions_.store(0);
        for(int i=GroupPlay;i<=RestartLoops;++i){set(i,0);mark(i);}
        for(int n=0;n<4;++n)for(int k=0;k<6;++k){set(ix(n,k),0);mark(ix(n,k));}
        for(int i=AllPlay;i<=RestoreSession;++i){set(i,0);mark(i);}
        set(RecordSelected,0);set(NextLoop,0);mark(RecordSelected);mark(NextLoop);
    }
    void parameterChanged(int index,float value) override {
        // Keep the old parameter index for project compatibility; even a saved On is inert.
        if(index==Metronome)return;
        if(index>=GroupPlay&&index<=RestartLoops){
            if(value>=.5f){unsigned mask=0;for(int n=0;n<4;++n)if(getBool(GroupFirst+n))mask|=1u<<n;
                groupActions_.fetch_or(index==RestartLoops?256u:index==GroupStop?mask<<4:mask);
            }
            return;
        }
        bool& syncing=syncing_;
        if(!syncing){
            if(index>=UiLevelFirst&&index<UiLevelFirst+4){
                int n=index-UiLevelFirst;float db=-60.f+std::clamp(value,0.f,1.f)*(60.f+faderCeiling());
                syncing=true;set(ix(n,LEVEL),db);mark(ix(n,LEVEL));
                if(getInt(SelectedLoop)==n){set(SelectedLevel,db);mark(SelectedLevel);}
                syncing=false;pendingFaderSelection_.store(n);return;
            }
            if(index==FaderMax){
                syncing=true;
                for(int n=0;n<4;++n){if(get(ix(n,LEVEL))>faderCeiling()){set(ix(n,LEVEL),faderCeiling());mark(ix(n,LEVEL));}syncFaderUi(n);}
                set(SelectedLevel,get(ix(getInt(SelectedLoop),LEVEL)));mark(SelectedLevel);syncing=false;return;
            }

            if(index==LoopMode)return; // Legacy selector is inert; all four loops stay available.
            if(index==Warp&&value>=.5f&&!getBool(SyncTempo)){syncing=true;set(SyncTempo,1);syncing=false;mark(SyncTempo);}
            if(index==SyncTempo&&value<.5f&&getBool(Warp)){syncing=true;set(Warp,0);syncing=false;mark(Warp);}
            if(index==SelectedLoop){pendingFaderSelection_.store(-1);syncing=true;setNormalised(SelectedLevel,getNormalised(ix(getInt(SelectedLoop),LEVEL)));syncing=false;mark(SelectedLoop);mark(SelectedLevel);return;}
            if(index==SelectedLevel){flushFaderSelection();syncing=true;int bank=getInt(SelectedLoop);
                float db=std::min(value,faderCeiling());set(SelectedLevel,db);set(ix(bank,LEVEL),db);syncFaderUi(bank);
                syncing=false;mark(ix(bank,LEVEL));mark(SelectedLevel);return;}
            if(index>=FirstSlot&&index<SelectedLoop&&((index-FirstSlot)%Stride==LEVEL||(index-FirstSlot)%Stride==LENGTH)){
                int bank=(index-FirstSlot)/Stride;pendingFaderSelection_.store(-1);syncing=true;
                if(get(ix(bank,LEVEL))>faderCeiling()){set(ix(bank,LEVEL),faderCeiling());mark(ix(bank,LEVEL));}
                syncFaderUi(bank);set(SelectedLoop,float(bank));setNormalised(SelectedLevel,getNormalised(ix(bank,LEVEL)));syncing=false;mark(SelectedLoop);mark(SelectedLevel);return;
            }
        }
        bool action=(index>=AllPlay&&index<=RestoreSession)||index==RecordSelected||index==NextLoop||
            (index>=FirstSlot&&index<SelectedLoop&&(index-FirstSlot)%Stride<6);
        if(value<.5f){if(action&&getNormalised(index)!=0)setNormalised(index,0);return;}
        if(index==RecordSelected){flushFaderSelection();actions_[getInt(SelectedLoop)].fetch_or(loop4::Rec);return;}
        if(index==NextLoop){flushFaderSelection();syncing=true;set(SelectedLoop,float((getInt(SelectedLoop)+1)%(4)));setNormalised(SelectedLevel,getNormalised(ix(getInt(SelectedLoop),LEVEL)));syncing=false;mark(SelectedLoop);mark(SelectedLevel);return;}
        if(index>=FirstSlot){int n=(index-FirstSlot)/Stride,k=(index-FirstSlot)%Stride;if(n<4&&k<6)actions_[n].fetch_or(1u<<k);}
    }
    void saveExtraState(std::vector<uint8_t>& out) override {Reader reader(*this);loop4::encodeDisk(reader.engine,out);}
    void loadExtraState(const uint8_t* data,size_t size) override {
        try{loadGeneration_.fetch_add(1);auto next=std::unique_ptr<loop4::Engine>(loop4::decodeDisk(data,size,rate_.load(),4,std::filesystem::path(root_)/"Content"));adopt(*next);queueEngine(next.release());}catch(const std::exception& ex){message(std::string("Load: ")+ex.what());}
    }
    bool formatValue(int index,float value,std::string& out) override {
        if(index>=UiLevelFirst&&index<UiLevelFirst+4){char text[32];std::snprintf(text,sizeof text,"%.1f dB",double(-60.f+value*(60.f+faderCeiling())));out=text;return true;}if(index==LoopMode){out="4 loops";return true;}if(index==DiskLock){out=getBool(DiskLock)?"LOCKED":"PREFILLED";return true;}if(index==Metronome){out="DISABLED";return true;}if(index==FileStatus){std::lock_guard<std::mutex> lock(messageMutex_);out=message_;return true;}return false;}
    void process(mpc::ProcessContext& ctx) override {
        flushFaderSelection();
        if(!retired_.load()){if(auto* next=pending_.exchange(nullptr)){auto* old=active_.exchange(next);retired_.store(old);}}
        auto* e=active_.load();if(!e||ctx.numInputs<1||ctx.numOutputs<1)return;
        e->beginAudio();
        auto& t=ctx.transport;double bpm=t.valid&&t.tempoValid&&std::isfinite(t.bpm)?std::clamp(t.bpm,20.,400.):120;
        double bar=double(std::clamp(t.timeSigNumerator,1,32))*4/std::clamp(t.timeSigDenominator,1,32);
        bool hostPlaying=t.valid&&t.playing;
        bool clockRunning=!t.valid||hostPlaying;e->clockRunning(clockRunning);
        e->playback(bpm,getBool(SyncTempo),getBool(Warp));
        e->launchMode(getInt(ResumeMode)==1);
        for(int n=0;n<4;++n)e->loopOptions(n,getBool(LoopSyncFirst+n),getInt(PlayModeFirst+n)==1,getInt(StopModeFirst+n));
        e->enforceSingle();
        auto position=timeline_.update(t,unsigned(ctx.numFrames),e->rate);
        if(position.restart)e->restart(beat_);
        else if(position.anchor)beat_=position.beat;
        if(e->takeEditEnded()&&statusHold_<=0)tempoEditEnded_.store(true);
        if(statusHold_<=0){set(Tempo,float(bpm));set(WarpState,!getBool(SyncTempo)?0.f:!getBool(Warp)?1.f:e->warming()?2.f:3.f);}
        double gridBeat=position.available?position.beat:beat_;
        if(getInt(Quantize)==2&&t.ppqValid&&std::isfinite(t.barStartPpq))gridBeat-=t.barStartPpq;
        double measureGrid=position.available&&t.ppqValid&&std::isfinite(t.barStartPpq)?position.beat-t.barStartPpq:gridBeat;
        auto consume=[&](int i){if(getBool(i)){set(i,0);mark(i);return true;}return false;};
        consume(RecordSelected);consume(NextLoop);
        unsigned group=groupActions_.exchange(0);
        consume(GroupPlay);consume(GroupStop);consume(RestartLoops);
        if(group&256u)e->restart(beat_);
        if(group&15u){bool measure=false;for(int n=0;n<4;++n)if((group&(1u<<n))&&getBool(LoopSyncFirst+n)&&getInt(ResumeMode)==1)measure=true;
            e->queueTransport(group&15u,true,beat_,measure?measureGrid:gridBeat,bar,measure?2:getInt(Quantize));}
        auto stopTiming=[&](unsigned mask){int timing=0;for(int n=0;n<4;++n)if(mask&(1u<<n))timing=std::max(timing,getInt(StopModeFirst+n));return timing;};
        if(group&240u){unsigned mask=(group>>4)&15u;e->queueTransport(mask,false,beat_,stopTiming(mask)==2?measureGrid:(position.available?position.beat:beat_),bar,stopTiming(mask));}
        // Snapshot every independent touch intent before any slot is changed.
        std::array<unsigned,4> commandsSnapshot{};
        for(int n=0;n<4;++n)commandsSnapshot[n]=actions_[n].exchange(0);
        if(consume(AllPlay))e->all(true);
        if(consume(AllStop))e->queueTransport(15,false,beat_,stopTiming(15)==2?measureGrid:(position.available?position.beat:beat_),bar,stopTiming(15));
        if(consume(ExportAll))jobs_.fetch_or(ExportBits);
        if(consume(SaveSession))jobs_.fetch_or(SaveBit);
        if(consume(RestoreSession))jobs_.fetch_or(RestoreBit);
        std::array<float,4> levels{};
        for(int n=0;n<4;++n){unsigned commands=commandsSnapshot[n];
            for(int k=0;k<6;++k)if(commands&(1u<<k)){set(ix(n,k),0);mark(ix(n,k));}
            if(commands&(1u<<EXPORT))jobs_.fetch_or(1u<<n);
            e->recordAction(n,getInt(RecAction+n));
            unsigned core=commands&31u;int choice=getInt(ix(n,LENGTH)),bars=choice?1<<(choice-1):0;
            double commandGrid=gridBeat;
            if((core&loop4::Stop)&&getInt(StopModeFirst+n)==2)commandGrid=measureGrid;
            else if((core&loop4::Stop)&&getInt(StopModeFirst+n)==1)commandGrid=position.available?position.beat:beat_;
            else if((core&loop4::Rec)&&e->slots[n].current>=0&&!e->slots[n].playing&&getInt(ResumeMode)==1)commandGrid=measureGrid;
            if(core)e->command(n,core,beat_,bpm,bar,getInt(Quantize),bars,commandGrid);
            levels[n]=std::pow(10.f,std::min(get(ix(n,LEVEL)),faderCeiling())/20.f);
        }
        const float* inL=ctx.inputs[0];const float* inR=ctx.numInputs>1?ctx.inputs[1]:inL;
        float* outL=ctx.outputs[0];float* outR=ctx.numOutputs>1?ctx.outputs[1]:outL;
        bool monitor=getBool(Monitor);double advance=bpm/(60*e->rate);
        for(int i=0;i<ctx.numFrames;++i){float l,r;float a=inL?inL[i]:0,b=inR?inR[i]:a;e->frame(a,b,l,r,beat_,levels,monitor);
            outL[i]=l;outR[i]=r;if(clockRunning)beat_+=advance;
        }
        e->endAudio();
        if(auto* disk=e->diskStore()){unsigned misses=disk->readMisses();if(misses!=lastDiskMisses_)diskMissHold_=int(e->rate/2);else diskMissHold_=std::max(0,diskMissHold_-ctx.numFrames);lastDiskMisses_=misses;
            if(statusHold_<=0){set(DiskState,e->diskProblem()?3.f:e->pendingDiskCommits()?1.f:diskMissHold_>0?2.f:0.f);set(DiskMisses,float(misses));set(DiskErrors,float(disk->writeErrors()+disk->ioErrors()));set(DiskLock,disk->locked()?1.f:0.f);set(DiskCache,float(disk->cacheBytes()/1048576.));}}
        editing_.store(e->editing()?1:0);playing_.store(e->hasPlayback()?1:0);
        if(statusHold_>0)statusHold_-=ctx.numFrames;
        else for(int n=0;n<4;++n){auto v=e->view(n);set(ix(n,STATUS),float(v.state));set(ix(n,PROGRESS),std::round(v.progress*255)/255);}
    }
};
MPC_PLUGIN_ENTRY(MPCLooper)
