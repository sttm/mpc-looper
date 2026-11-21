#include "../src/MPCLooper.cpp"
#include <cassert>
#include <iostream>
thread_local bool audioCallback=false;
std::atomic<unsigned> audioNotifications{0},notifications{0};
VstIntPtr host(AEffect*,VstInt32 op,VstInt32,VstIntPtr,void*,float){if(op==audioMasterAutomate){++notifications;if(audioCallback)++audioNotifications;}return 0;}
int main(){
    auto root=std::filesystem::path(mpc::detail::computePluginFolder());
    assert(root.filename()=="test-profile"||root.filename()=="sanitize-profile");
    std::filesystem::remove_all(root/"Content");
    AEffect effect{};MPCLooper plugin;plugin.effect_=&effect;plugin.host_=&host;
    assert(plugin.numParams()==94&&plugin.getInt(ResumeMode)==0);
    for(int n=0;n<4;++n){assert(plugin.getInt(ix(n,LENGTH))==3);assert(plugin.getBool(LoopSyncFirst+n));assert(plugin.getInt(PlayModeFirst+n)==0&&plugin.getInt(StopModeFirst+n)==0);}
    std::array<float,128> l{},r{};float* channels[]={l.data(),r.data()};
    mpc::ProcessContext ctx{};ctx.inputs=channels;ctx.outputs=channels;ctx.numInputs=2;ctx.numOutputs=2;ctx.numFrames=128;ctx.sampleRate=8000;
    auto render=[&](int blocks,float value){for(int b=0;b<blocks;++b){l.fill(value);r.fill(-value);audioCallback=true;plugin.process(ctx);audioCallback=false;}};
    auto settle=[&]{for(int i=0;i<2000;++i){render(2,0);std::this_thread::sleep_for(std::chrono::milliseconds(5));if(plugin.getInt(DiskState)==0)return;}assert(false&&"Disk commit did not settle");};
    auto waitState=[&](int slot,int state){for(int i=0;i<2000;++i){render(2,0);if(plugin.getInt(ix(slot,STATUS))==state)return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}assert(false&&"Timed out waiting for state load");};
    // File jobs complete on a worker. Wait for its completion message rather
    // than assuming a fixed 150/200ms delay, especially under ASan/emulation.
    auto fileJob=[&](int parameter,const std::string& prefix){
        float previous=plugin.get(FileStatus);plugin.set(parameter,1);render(1,0);
        for(int i=0;i<2000;++i){
            std::string message;plugin.formatValue(FileStatus,0,message);
            if(plugin.get(FileStatus)!=previous&&message.rfind(prefix,0)==0){render(17,0);return;}
            assert(message.rfind("File:",0)!=0);
            render(1,0);std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(false&&"Timed out waiting for file job completion");
    };
    auto parameterOnly=[](std::vector<uint8_t> state){
        assert(state.size()>=16&&loop4::u32(state.data()+4)==2);
        size_t pos=12;unsigned count=loop4::u32(state.data()+8);
        for(unsigned n=0;n<count;++n){assert(pos<state.size());unsigned length=state[pos++];pos+=length+8;assert(pos<=state.size());}
        state.resize(pos+4);std::fill(state.begin()+pos,state.end(),uint8_t{0});return state;
    };
    // Ceiling changes preserve gain unless reducing the maximum; native UI spans the full range.
    assert(plugin.getInt(FaderMax)==0);
    for(int n=0;n<4;++n)assert(plugin.get(UiLevelFirst+n)==1&&plugin.get(ix(n,LEVEL))==0);
    plugin.set(FaderMax,1);
    assert(plugin.get(ix(0,LEVEL))==0&&std::fabs(plugin.get(UiLevelFirst)-60.f/66)<1e-6);
    plugin.set(UiLevelFirst+2,1);render(1,0);
    assert(plugin.get(ix(2,LEVEL))==6&&plugin.getInt(SelectedLoop)==2&&plugin.get(SelectedLevel)==6);
    plugin.set(FaderMax,0);
    assert(plugin.get(ix(2,LEVEL))==0&&plugin.get(UiLevelFirst+2)==1&&plugin.get(SelectedLevel)==0);
    plugin.set(FaderMax,1);
    for(int n=0;n<4;++n)plugin.set(UiLevelFirst+n,float(60+n)/66);
    render(1,0);plugin.set(SelectedLoop,1);
    auto faderState=parameterOnly(plugin.internalSaveState());
    plugin.set(FaderMax,0);plugin.set(SelectedLoop,0);
    assert(plugin.internalLoadState(faderState.data(),faderState.size()));plugin.reset();render(1,0);
    assert(plugin.getInt(FaderMax)==1&&plugin.getInt(SelectedLoop)==1);
    for(int n=0;n<4;++n)assert(std::fabs(plugin.get(ix(n,LEVEL))-n)<1e-4);
    plugin.set(FaderMax,0);
    for(int n=0;n<4;++n)plugin.set(UiLevelFirst+n,1);
    plugin.reset();
    // A legacy saved On must not synthesize a click after state reload.
    plugin.set(Metronome,1);auto legacyOn=parameterOnly(plugin.internalSaveState());
    assert(plugin.internalLoadState(legacyOn.data(),legacyOn.size()));plugin.reset();
    // Parameter-only round trips above need no live disk engine. Starting it
    // here prevents an old empty queued load from replacing a new capture in
    // this faster-than-realtime host fixture.
    plugin.prepare(8000,128);plugin.reset();
    ctx.transport.valid=true;ctx.transport.playing=true;ctx.transport.tempoValid=true;ctx.transport.bpm=120;
    render(160,0);for(float sample:l)assert(sample==0);for(float sample:r)assert(sample==0);
    ctx.transport.valid=false;ctx.transport.playing=false;
    // On/off transitions used by the two native toggle buttons.
    for(int n=0;n<3;++n){
        plugin.set(SyncTempo,1);assert(plugin.getBool(SyncTempo));
        plugin.set(Warp,1);assert(plugin.getBool(Warp)&&plugin.getBool(SyncTempo));
        plugin.set(Warp,0);assert(!plugin.getBool(Warp)&&plugin.getBool(SyncTempo));
        plugin.set(Warp,1);plugin.set(SyncTempo,0);assert(!plugin.getBool(SyncTempo)&&!plugin.getBool(Warp));
    }
    assert(plugin.getInt(LoopMode)==1); // all four loops are available immediately
    plugin.set(LoopMode,0);render(2,0); // old parameter remains inert
    for(int n=0;n<4;++n)plugin.set(RecAction+n,1); // test capture/export without auto-dub
    plugin.set(Quantize,0);plugin.set(ix(0,LENGTH),0);plugin.set(ix(1,LENGTH),0);
    plugin.set(ix(0,REC),1);render(8,.2f);plugin.set(ix(0,REC),1);render(1,0);
    plugin.set(ix(1,REC),1);render(12,.4f);plugin.set(ix(1,REC),1);render(1,0);
    assert(plugin.getInt(ix(0,STATUS))==loop4::Playing&&plugin.getInt(ix(1,STATUS))==loop4::Playing);
    // An old two-loop manifest still opens with four available columns.
    settle();std::vector<uint8_t> oldTwo;plugin.saveExtraState(oldTwo);
    loopdisk::put32(oldTwo.data()+44,2);plugin.set(ix(0,CLEAR),1);render(3,0);
    assert(plugin.getInt(ix(0,STATUS))==loop4::Empty);
    plugin.loadExtraState(oldTwo.data(),oldTwo.size());plugin.reset();waitState(0,loop4::Playing);
    std::vector<uint8_t> upgraded;plugin.saveExtraState(upgraded);assert(loop4::u32(upgraded.data()+44)==4);
    // Proxy controls are automatable host parameters, independent of direct MIDI delivery.
    // Each native Q-Link bank: level, length, Rec Action; Q4 is unassigned.
    for(int n=0;n<4;++n){plugin.set(ix(n,LEVEL),-float(n));assert(plugin.getInt(SelectedLoop)==n);
        plugin.set(ix(n,LENGTH),0);assert(plugin.getInt(SelectedLoop)==n);}
    plugin.set(ix(0,LEVEL),0);
    plugin.set(SelectedLoop,2);plugin.set(SelectedLevel,-9);assert(plugin.get(ix(2,LEVEL))==-9&&plugin.get(ix(0,LEVEL))==0);
    plugin.set(NextLoop,1);assert(plugin.getInt(SelectedLoop)==3);render(1,0);
    plugin.set(ix(1,LEVEL),-3);assert(plugin.getInt(SelectedLoop)==1&&plugin.get(SelectedLevel)==-3);
    plugin.set(RecAction+2,0);plugin.set(ix(2,LENGTH),0);plugin.set(SelectedLoop,0);plugin.set(UiLevelFirst+2,.85f);plugin.set(RecordSelected,1);assert(plugin.getInt(SelectedLoop)==2);render(9,.3f);plugin.set(ix(2,REC),1);render(1,0);
    assert(plugin.getInt(ix(2,STATUS))==loop4::Overdub);
    plugin.set(ix(2,REC),1);render(10,0); // on-screen REC action ends overdub
    assert(!plugin.getBool(ix(2,REC)));
    assert(plugin.getInt(ix(2,STATUS))==loop4::Playing&&plugin.getInt(ix(3,STATUS))==loop4::Empty);
    plugin.set(SelectedLoop,3);plugin.set(RecAction+3,1);plugin.set(ix(3,REC),1);render(8,.25f);plugin.set(ix(3,REC),1);render(1,0);
    assert(plugin.getInt(ix(3,STATUS))==loop4::Playing);
    // Group membership is captured with the action, before the next audio block.
    plugin.set(Quantize,0);plugin.set(GroupFirst,1);plugin.set(GroupFirst+1,1);plugin.set(GroupFirst+2,1);plugin.set(GroupFirst+3,0);
    plugin.set(GroupStop,1);plugin.set(GroupFirst,0);render(4,0);
    for(int n=0;n<3;++n)assert(plugin.getInt(ix(n,STATUS))==loop4::Stopped);
    assert(plugin.getInt(ix(3,STATUS))==loop4::Playing);
    plugin.set(GroupFirst,1);plugin.set(GroupPlay,1);render(4,0);
    for(int n=0;n<4;++n)assert(plugin.getInt(ix(n,STATUS))==loop4::Playing);
    plugin.set(RestartLoops,1);render(1,0);assert(!plugin.getBool(RestartLoops));
    settle();auto saved=plugin.internalSaveState();
    // Duplicating a plugin state keeps independent backup identities.
    {MPCLooper other;other.prepare(8000,128);assert(other.internalLoadState(saved.data(),saved.size()));other.reset();other.process(ctx);
        std::vector<uint8_t> a,b;plugin.saveExtraState(a);other.saveExtraState(b);
        assert(std::memcmp(a.data()+12,b.data()+12,32)!=0);}

    plugin.set(ix(0,CLEAR),1);render(3,0);assert(plugin.getInt(ix(0,STATUS))==loop4::Empty);
    assert(plugin.internalLoadState(saved.data(),saved.size()));plugin.reset();waitState(0,loop4::Playing);
    assert(plugin.getInt(ix(0,STATUS))==loop4::Playing);
    settle();fileJob(ExportAll,"WAV ");
    unsigned wavs=0;
    for(auto& p:std::filesystem::recursive_directory_iterator(root/"Content/Exports"))if(p.path().extension()==".wav")++wavs;
    assert(wavs==4);
    fileJob(ix(1,EXPORT),"WAV ");
    wavs=0;for(auto& p:std::filesystem::recursive_directory_iterator(root/"Content/Exports"))if(p.path().extension()==".wav")++wavs;assert(wavs==5);
    settle();plugin.set(ix(0,LENGTH),3);
    fileJob(SaveSession,"Session collected");
    unsigned backups=0;for(auto& p:std::filesystem::recursive_directory_iterator(root/"Content/Sessions"))if(p.path().extension()==".loop4")++backups;assert(backups>=1);
    plugin.set(ix(0,CLEAR),1);render(3,0);fileJob(RestoreSession,"Backup queued");
    assert(plugin.getInt(ix(0,STATUS))==loop4::Playing);assert(plugin.getInt(ix(0,LENGTH))==3);
    // V1 backups stored length against the old five-choice scale (Free,1,2,4,8).
    for(auto& file:std::filesystem::recursive_directory_iterator(root/"Content/Sessions"))if(file.path().extension()==".loop4"){
        auto old=loop4::read(file.path());assert(std::memcmp(old.data(),"STLPSAV2",8)==0);old[7]='1';
        for(int n=0;n<4;++n){size_t at=12+size_t(ix(n,LENGTH))*4;float value=loop4::f32(old.data()+at);std::vector<uint8_t> encoded;loop4::putFloat(encoded,std::round(value*6)/4);std::copy(encoded.begin(),encoded.end(),old.begin()+at);}
        loop4::writeAtomic(file.path(),old);
    }
    plugin.set(ix(0,LENGTH),1);fileJob(RestoreSession,"Backup queued");
    assert(plugin.getInt(ix(0,LENGTH))==3);
    // Changing the deprecated selector cannot reload audio or disable 3/4.
    plugin.set(AllStop,1);render(4,0);plugin.set(LoopMode,0);render(2,0);
    assert(plugin.getInt(ix(2,STATUS))==loop4::Stopped&&plugin.getInt(ix(3,STATUS))==loop4::Stopped);
    plugin.set(SelectedLoop,1);plugin.set(NextLoop,1);assert(plugin.getInt(SelectedLoop)==2);render(1,0);
    // Real host transport: pause freezes the published position and keeps local intent.
    plugin.set(ix(0,REC),1);ctx.transport.valid=true;ctx.transport.playing=true;
    ctx.transport.tempoValid=true;ctx.transport.bpm=120;render(3,0);
    float position=plugin.get(ix(0,PROGRESS));ctx.transport.playing=false;render(20,0);
    assert(plugin.get(ix(0,PROGRESS))==position&&plugin.getInt(ix(0,STATUS))==loop4::Paused);
    ctx.transport.playing=true;render(3,0);assert(plugin.getInt(ix(0,STATUS))==loop4::Playing);
    assert(plugin.get(ix(0,PROGRESS))!=position);
    plugin.set(ix(0,STOP),1);render(3,0);position=plugin.get(ix(0,PROGRESS));render(3,0);
    assert(plugin.get(ix(0,PROGRESS))!=position&&plugin.getInt(ix(0,STATUS))==loop4::Stopped);
    ctx.transport.playing=false;position=plugin.get(ix(0,PROGRESS));render(20,0);assert(plugin.get(ix(0,PROGRESS))==position);
    // Running song jumps/wraps must not change loop phase. A paused cursor
    // move requests loop zero on resume, regardless of the chosen destination.
    ctx.transport.ppqValid=true;ctx.transport.ppqPos=8;ctx.transport.playing=true;
    plugin.set(ix(1,REC),1);render(1,0);position=plugin.get(ix(1,PROGRESS));
    ctx.transport.ppqPos=0;render(1,0);
    assert(std::fabs(plugin.get(ix(1,PROGRESS))-std::fmod(position+128.f/1536,1.f))<2.f/255);
    ctx.transport.ppqPos=.032;ctx.transport.playing=false;render(1,0);position=plugin.get(ix(1,PROGRESS));
    ctx.transport.ppqPos=5;render(2,0);assert(plugin.get(ix(1,PROGRESS))==position);
    ctx.transport.playing=true;render(1,0);
    assert(plugin.getInt(ix(1,STATUS))==loop4::Playing&&plugin.getInt(ix(0,STATUS))==loop4::Stopped);
    assert(std::fabs(plugin.get(ix(1,PROGRESS))-128.f/1536)<1.f/255+.001f);
    ctx.transport.ppqPos=5.032;ctx.transport.playing=false;render(2,0);position=plugin.get(ix(1,PROGRESS));
    ctx.transport.playing=true;render(1,0);
    assert(std::fabs(plugin.get(ix(1,PROGRESS))-position-128.f/1536)<2.f/255);
    // One REC press waits for the next bar; phase corrections at the downbeat
    // cannot finish/drop a capture, even when just one sample was recorded.
    {MPCLooper armed;armed.effect_=&effect;armed.host_=&host;armed.prepare(8000,128);
     auto t=ctx;t.numFrames=1;t.transport.valid=t.transport.playing=t.transport.ppqValid=t.transport.tempoValid=true;
     t.transport.bpm=120;t.transport.ppqPos=2.99;double at=t.transport.ppqPos;
     auto frame=[&](double ppq){t.transport.ppqPos=ppq;t.transport.samplePos=ppq*4000;l.fill(.2f);r.fill(-.2f);audioCallback=true;armed.process(t);audioCallback=false;};
     frame(at);at+=.00025;armed.set(Quantize,2);armed.set(RecAction,1);armed.set(ix(0,REC),1);
     for(int i=0;i<4039;++i){frame(at);at+=.00025;assert(armed.getInt(ix(0,STATUS))==loop4::Armed);}
     frame(4.001);assert(armed.getInt(ix(0,STATUS))==loop4::Recording);assert(!armed.getBool(ix(0,REC)));
     // Emulate a host timestamp correction larger than two audio blocks.
     at=4.01;for(int i=0;i<256;++i){frame(at);at+=.00025;assert(armed.getInt(ix(0,STATUS))==loop4::Recording);}
     armed.set(Quantize,0);armed.set(ix(0,REC),1);frame(at);assert(armed.getInt(ix(0,STATUS))==loop4::Playing);
     std::vector<uint8_t> state;for(int i=0;i<200;++i){frame(at);at+=.00025;armed.saveExtraState(state);if(loop4::u32(state.data()+48)>=257)break;state.clear();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
     assert(state.size()>60&&loop4::u32(state.data()+48)==257);
    }
    // Four-bar capture across TWO complete two-bar MPC sequence cycles.
    {MPCLooper cycle;cycle.effect_=&effect;cycle.host_=&host;cycle.prepare(8000,128);
     auto t=ctx;t.transport.valid=t.transport.playing=t.transport.ppqValid=t.transport.tempoValid=t.transport.cycling=true;
     t.transport.bpm=120;t.transport.cycleStartPpq=0;t.transport.cycleEndPpq=8;
     cycle.set(Quantize,2);cycle.set(RecAction,1);cycle.set(ix(0,LENGTH),3);cycle.set(ix(0,REC),1);
     auto block=[&](int b){t.transport.ppqPos=(b%250)*.032;t.transport.samplePos=(b%250)*128;l.fill(.2f);r.fill(-.2f);audioCallback=true;cycle.process(t);audioCallback=false;};
     for(int b=0;b<500;++b){block(b);assert(cycle.getInt(ix(0,STATUS))==(b==499?loop4::Playing:loop4::Recording));}
     std::vector<uint8_t> state;for(int i=0;i<200;++i){block(500+i);cycle.saveExtraState(state);if(loop4::u32(state.data()+48)==64000)break;state.clear();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
     assert(state.size()>60&&loop4::u32(state.data()+48)==64000);
    }
    assert(audioNotifications.load()==0&&notifications.load()>0);
    std::cout<<"PASS proxy MIDI Learn parameters, selected capture/level, no audio-thread host notifications; real plugin in-place input, independent loops, audio chunk reload, individual WAV stems, session backup and restore\n";
}
