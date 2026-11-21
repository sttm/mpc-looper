#include "LoopFiles.h"
#include "HostTimeline.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
thread_local bool audio=false;
[[gnu::noinline]] void* operator new(std::size_t n){if(audio)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
struct Rig {
 loop4::Engine e{8000,std::string(32,'a'),4,4};std::array<float,4> levels{{1,1,1,1}};double beat=0;float l=0,r=0;
 Rig(){e.recordAction(0,0);e.recordAction(1,0);}
 void tap(unsigned command=loop4::Rec,int bars=0,int quant=0){audio=true;e.command(0,command,beat,120,4,quant,bars);audio=false;}
 void run(unsigned n,float v=0,bool clock=true){e.clockRunning(clock);audio=true;for(unsigned i=0;i<n;++i){e.frame(v,v,l,r,beat,levels,false);if(clock)beat+=2./8000;assert(std::isfinite(l)&&std::isfinite(r));}audio=false;}
};
int main(){
 Rig h;h.tap();h.run(1000,.1f);assert(h.e.view(0).state==loop4::Recording);
 h.tap();h.run(100,.2f);assert(h.e.view(0).state==loop4::Overdub);assert(h.e.slots[0].dubWritten==100);
 h.tap();assert(h.e.view(0).state==loop4::Playing);h.run(900,.9f);
 {auto p=h.e.pin(0);assert(p.buffer->length==1000);for(int i=0;i<1000;++i)assert(std::fabs(p.buffer->l[i]-(i<100?.3f:.1f))<1e-5);}
 assert(!h.e.slots[0].dubbing);h.run(300);double phase=h.e.slots[0].pos;
 h.tap();assert(h.e.view(0).state==loop4::Overdub);assert(h.e.slots[0].pos==phase);h.run(50,.2f);
 h.tap();assert(h.e.view(0).state==loop4::Playing);h.run(950,.9f);
 assert(h.e.slots[0].pos==phase);assert(!h.e.slots[0].dubbing);
 {auto p=h.e.pin(0);for(int i=300;i<350;++i)assert(std::fabs(p.buffer->l[i]-.3f)<1e-5);assert(std::fabs(p.buffer->l[500]-.1f)<1e-5);}
 // STOP silences just this loop; its phase continues with the shared clock.
 h.tap(loop4::Stop);assert(h.e.view(0).state==loop4::Stopped);phase=h.e.slots[0].pos;h.run(200);
 assert(h.e.slots[0].pos==std::fmod(phase+200,1000));assert(h.l==0&&h.r==0);
 h.tap();assert(h.e.view(0).state==loop4::Playing);assert(!h.e.slots[0].dubbing&&h.e.slots[0].pos==std::fmod(phase+200,1000));
 h.run(200);phase=h.e.slots[0].pos;double beat=h.beat;
 // Global STOP freezes phase and armed/recording/edit state, preserving local intent.
 h.run(800,0,false);assert(h.e.slots[0].pos==phase&&h.beat==beat&&h.l==0&&h.r==0);
 assert(h.e.view(0).state==loop4::Paused);h.run(100);assert(h.e.slots[0].pos==phase+100);
 h.tap();h.run(50,.1f);assert(h.e.slots[0].dubbing);phase=h.e.slots[0].pos;unsigned written=h.e.slots[0].dubWritten;
 h.run(800,.8f,false);assert(h.e.slots[0].pos==phase&&h.e.slots[0].dubWritten==written);
 h.run(20,.1f);assert(h.e.slots[0].dubWritten==written+20);
 // Local STOP in an overdub stops adding immediately and fades now, then completes its immutable tail.
 h.tap(loop4::Stop);assert(h.e.view(0).state==loop4::Stopped);h.run(100);assert(h.l==0&&h.r==0);h.run(1000);assert(!h.e.slots[0].dubbing);
 h.tap();assert(h.e.view(0).state==loop4::Playing);
 // Alternate REC -> PLAY -> DUB sequence, no restart on fourth tap.
 Rig alternate;alternate.e.recordAction(0,1);alternate.tap();alternate.run(512,.1f);alternate.tap();alternate.run(1);
 assert(alternate.e.view(0).state==loop4::Playing);alternate.tap();assert(alternate.e.view(0).state==loop4::Overdub);
 alternate.run(25,.2f);alternate.tap();assert(alternate.e.view(0).state==loop4::Playing);
 // Fixed capture hands the NEXT sample to the first overdub, not the last recorded sample twice.
 Rig fixed;fixed.tap(loop4::Rec,1);fixed.run(16000,.1f);assert(fixed.e.slots[0].dubArmed);
 fixed.run(1,.2f);assert(fixed.e.view(0).state==loop4::Overdub&&fixed.e.slots[0].dubWritten==1);
 // Quantized manual finish hands over on the actual record boundary.
 Rig grid;grid.tap();grid.run(1000,.1f);grid.tap(loop4::Rec,0,1);grid.run(3000,.1f);
 assert(grid.e.view(0).state==loop4::Recording);grid.run(1,.2f);assert(grid.e.view(0).state==loop4::Overdub);
 {auto p=grid.e.pin(0);assert(p.buffer->length==4000);}
 // Clearing must still work while the global transport is paused.
 h.run(200,0,false);h.tap(loop4::Clear);assert(!h.e.pin(0)&&h.e.view(0).state==loop4::Empty);
 h.tap(loop4::Undo);assert(h.e.pin(0));
 // A requested restart uses loop zero, independent of song position/tempo.
 Rig transport;std::array<float,1000> source{};source.fill(.15f);
 transport.e.import(0,source.data(),source.data(),1000,120);transport.e.import(1,source.data(),source.data(),750,120);
 transport.e.command(1,loop4::Stop,0,120,4,0,0);transport.run(100);
 audio=true;transport.e.restart();audio=false;
 assert(transport.e.slots[0].pos==0&&transport.e.slots[1].pos==0&&!transport.e.slots[1].playing);
 transport.run(128);assert(transport.e.slots[0].pos==128&&transport.e.slots[1].pos==128&&!transport.e.slots[1].playing);
 transport.e.playback(150,true,false);audio=true;transport.e.restart();audio=false;assert(transport.e.slots[0].pos==0);
 transport.tap();transport.run(20,.2f);assert(transport.e.slots[0].dubbing);
 unsigned dubWritten=transport.e.slots[0].dubWritten;audio=true;transport.e.restart();audio=false;
 assert(transport.e.slots[0].dubbing&&transport.e.slots[0].dubWritten==dubWritten);
 transport.run(20,.2f);assert(transport.e.slots[0].dubWritten==dubWritten+20);
 {auto p=transport.e.pin(0);assert(p&&p.buffer->l[100]==.15f);}
 Rig capture;capture.tap();capture.run(256,.2f);audio=true;capture.e.restart();audio=false;
 assert(capture.e.slots[0].recording&&capture.e.slots[0].written==256);
 capture.run(128,.2f);assert(capture.e.slots[0].written==384);
 // Running jumps and sequence wraps never request a restart.
 HostTimeline timeline;mpc::Transport t{};t.valid=t.ppqValid=t.tempoValid=t.playing=true;t.bpm=120;
 auto initial=timeline.update(t,128,8000);assert(initial.anchor&&!initial.restart);
 for(double pos:{.032,4.,0.,16.,2.,0.}){t.ppqPos=pos;assert(!timeline.update(t,128,8000).restart);}
 // Stop alone remains a pause, even if the host returns its cursor to zero.
 t.ppqPos=4;t.playing=false;assert(!timeline.update(t,128,8000).restart);
 t.playing=true;assert(!timeline.update(t,128,8000).restart);
 t.playing=false;assert(!timeline.update(t,128,8000).restart);
 t.ppqPos=7;assert(!timeline.update(t,128,8000).restart);
 t.ppqPos=9;assert(!timeline.update(t,128,8000).restart);
 t.playing=true;assert(timeline.update(t,128,8000).restart);assert(!timeline.update(t,128,8000).restart);
 // Cursor move reported only on resume and samplePos fallback also restart.
 t.playing=false;timeline.update(t,128,8000);t.playing=true;t.ppqPos=1;assert(timeline.update(t,128,8000).restart);
 t.ppqValid=false;t.samplePos=800;assert(!timeline.update(t,128,8000).restart);
 t.playing=false;timeline.update(t,128,8000);t.samplePos=0;timeline.update(t,128,8000);
 t.playing=true;assert(timeline.update(t,128,8000).restart);
 // Reported inversion: cursor returns to zero ON Stop, then Play Start.
 HostTimeline startAtZero;mpc::Transport host{};host.valid=host.ppqValid=host.tempoValid=host.playing=true;host.bpm=120;
 host.ppqPos=3.9;startAtZero.update(host,128,8000);
 host.playing=false;host.ppqPos=0;assert(!startAtZero.update(host,128,8000).restart);
 host.playing=true;assert(startAtZero.update(host,128,8000).restart);
 // A late final timestamp within the held block is ordinary Play, not restart.
 HostTimeline late;host.ppqPos=3;late.update(host,128,8000);host.playing=false;late.update(host,128,8000);
 host.ppqPos=3.032;late.update(host,128,8000);host.playing=true;assert(!late.update(host,128,8000).restart);
 // A short natural cycle is distinct from a manual return early in the cycle.
 HostTimeline cycle;host.cycling=true;host.cycleStartPpq=0;host.cycleEndPpq=8;
 host.ppqPos=7.968;cycle.update(host,128,8000);host.ppqPos=0;assert(!cycle.update(host,128,8000).restart);
 host.ppqPos=3;cycle.update(host,128,8000);host.ppqPos=0;assert(cycle.update(host,128,8000).restart);
 // Three launches and stops commit on exactly one musical sample. No audio allocation.
 Rig group;for(int n=0;n<4;++n)group.e.import(n,source.data(),source.data(),1000,120);
 group.run(200);for(int n=0;n<3;++n)group.e.command(n,loop4::Stop,group.beat,120,4,0,0);
 double gridBeat=.975;group.e.queueTransport(7,true,group.beat,gridBeat,4,1);
 group.run(100);for(int n=0;n<3;++n)assert(!group.e.slots[n].playing);
 group.run(1);for(int n=0;n<4;++n)assert(group.e.slots[n].playing);
 // Command mask is independent: the fourth loop is not stopped with the triple.
 group.e.queueTransport(7,false,group.beat,.975,4,1);group.run(100);
 for(int n=0;n<4;++n)assert(group.e.slots[n].playing);
 group.run(1);for(int n=0;n<3;++n)assert(!group.e.slots[n].playing);assert(group.e.slots[3].playing);
 // Independent touches use the same external grid, even when local beat is offset.
 group.e.launchMode(true);
 for(int n=0;n<3;++n)group.e.command(n,loop4::Rec,group.beat,120,4,1,0,3.975);
 group.run(100);for(int n=0;n<3;++n)assert(!group.e.slots[n].playing);
 group.run(1);for(int n=0;n<3;++n)assert(group.e.slots[n].playing);
 // Restart during armed REC uses the monotonic clock, not zero / raw wrapped PPQ.
 Rig armed;armed.beat=50;armed.tap(loop4::Rec,1,2);assert(armed.e.slots[0].armRecord);
 armed.e.restart(armed.beat);armed.run(1,.1f);assert(armed.e.slots[0].recording&&armed.e.slots[0].written==1);
 // A four-bar take spans two short two-bar host cycles, without reset/truncation.
 loop4::Engine longTake{8000,std::string(32,'b'),10,4};longTake.recordAction(0,1);
 longTake.command(0,loop4::Rec,0,120,4,2,4,0);HostTimeline captureTimeline;
 host.playing=host.cycling=true;host.cycleStartPpq=0;host.cycleEndPpq=8;
 double continuous=0;std::array<float,4> unity{{1,1,1,1}};float ll=0,rr=0;
 audio=true;
 for(unsigned i=0;i<64000;++i){host.ppqPos=std::fmod(continuous,8.);auto p=captureTimeline.update(host,1,8000);assert(!p.restart);
     longTake.frame(.1f,.1f,ll,rr,continuous,unity,false);continuous+=.00025;}
 audio=false;auto completed=longTake.pin(0);assert(completed&&completed.buffer->length==64000);
 // Transfer live input between two partial overdubs, then return before closure.
 Rig handoff;for(int n=0;n<2;++n)handoff.e.import(n,source.data(),source.data(),1000,120);
 handoff.e.command(0,loop4::Rec,0,120,4,0,0);handoff.run(50,.2f);
 handoff.e.command(1,loop4::Rec,handoff.beat,120,4,0,0);
 assert(handoff.e.view(0).state==loop4::Playing&&handoff.e.view(1).state==loop4::Overdub);
 handoff.run(50,.3f);handoff.e.command(0,loop4::Rec,handoff.beat,120,4,0,0);
 assert(handoff.e.slots[1].finishDub&&!handoff.e.slots[0].finishDub);
 handoff.run(50,.4f);handoff.e.command(0,loop4::Rec,handoff.beat,120,4,0,0);handoff.run(1000);
 {auto first=handoff.e.pin(0),second=handoff.e.pin(1);assert(first&&second);
  assert(std::fabs(first.buffer->l[25]-.35f)<1e-5&&std::fabs(first.buffer->l[75]-.15f)<1e-5&&std::fabs(first.buffer->l[125]-.55f)<1e-5);
  assert(std::fabs(second.buffer->l[75]-.45f)<1e-5&&std::fabs(second.buffer->l[125]-.15f)<1e-5);}
 // PHASE ignores launch quantization and resumes current position immediately.
 Rig modes;for(int n=0;n<4;++n)modes.e.import(n,source.data(),source.data(),1000,120);
 modes.run(123);modes.e.command(0,loop4::Stop,modes.beat,120,4,2,0);assert(!modes.e.slots[0].playing); // default IMMEDIATE
 modes.run(77);double held=modes.e.slots[0].pos;
 modes.e.command(0,loop4::Rec,modes.beat,120,4,2,0,3.9);assert(modes.e.slots[0].playing&&modes.e.slots[0].pos==held);
 // Free launch is immediate phrase zero; stop timing remains independent.
 modes.e.command(0,loop4::Stop,modes.beat,120,4,0,0);modes.e.loopOptions(0,false,false,0);
 modes.e.command(0,loop4::Rec,modes.beat,120,4,2,0,3.9);assert(modes.e.slots[0].playing&&modes.e.slots[0].pos==0);
 // MEASURE starts phrase zero on the bar, while another loop keeps playing.
 modes.e.command(0,loop4::Stop,modes.beat,120,4,0,0);modes.e.loopOptions(0,true,false,0);modes.e.launchMode(true);
 modes.e.command(0,loop4::Rec,modes.beat,120,4,0,0,3.975);modes.run(100);assert(!modes.e.slots[0].playing);
 modes.run(1);assert(modes.e.slots[0].playing&&modes.e.slots[0].pos==1);
 // SINGLE switching is deferred until the new launch, never while queued.
 modes.e.loopOptions(0,true,true,0);modes.e.loopOptions(1,true,true,0);
 modes.e.command(1,loop4::Stop,modes.beat,120,4,0,0);
 modes.e.command(1,loop4::Rec,modes.beat,120,4,0,0,3.975);modes.run(100);assert(modes.e.slots[0].playing&&!modes.e.slots[1].playing);
 modes.run(1);assert(!modes.e.slots[0].playing&&modes.e.slots[1].playing&&modes.e.slots[2].playing&&modes.e.slots[3].playing);
 // Beat/measure stops commit on the exact sample, regardless of REC Quantize.
 for(int timing:{1,2}){
  modes.e.loopOptions(2,true,false,timing);double gridAt=timing==1?.975:3.975;
  modes.e.command(2,loop4::Stop,modes.beat,120,4,0,0,gridAt);modes.run(100);assert(modes.e.slots[2].playing);
  modes.run(1);assert(!modes.e.slots[2].playing);modes.e.loopOptions(2,true,false,0);modes.e.launchMode(false);
  modes.e.command(2,loop4::Rec,modes.beat,120,4,0,0);assert(modes.e.slots[2].playing);}
 // Immediate group stop still applies while global clock is paused.
 modes.run(10,0,false);modes.e.queueTransport(15,false,modes.beat,0,4,0);modes.run(1,0,false);
 for(int n=0;n<4;++n)assert(!modes.e.slots[n].playing);
 // A seek during an unfinished immutable dub is rejected, not allowed to corrupt its tail.
 Rig closing;closing.e.import(0,source.data(),source.data(),1000,120);closing.tap();closing.run(50,.2f);closing.tap(loop4::Stop);
 closing.e.loopOptions(0,false,false,0);closing.tap();assert(!closing.e.slots[0].playing&&closing.e.view(0).state==loop4::Busy);
 closing.run(1000);closing.tap();assert(closing.e.slots[0].playing&&closing.e.slots[0].pos==0);
 {auto p=closing.e.pin(0);assert(p&&std::fabs(p.buffer->l[25]-.35f)<1e-5&&std::fabs(p.buffer->l[75]-.15f)<1e-5);}
 // Timed Stop applies to overdubbing too; input continues only until the boundary.
 Rig timedDub;timedDub.e.import(0,source.data(),source.data(),1000,120);timedDub.e.loopOptions(0,true,false,1);
 timedDub.tap();timedDub.run(50,.2f);timedDub.e.command(0,loop4::Stop,timedDub.beat,120,4,0,0,.975);
 timedDub.run(100,.3f);assert(timedDub.e.slots[0].playing&&!timedDub.e.slots[0].finishDub);
 timedDub.run(1,.8f);assert(!timedDub.e.slots[0].playing&&timedDub.e.slots[0].finishDub);
 timedDub.run(1000,.8f);{auto p=timedDub.e.pin(0);assert(p&&std::fabs(p.buffer->l[75]-.45f)<1e-5&&std::fabs(p.buffer->l[175]-.15f)<1e-5);}
 // Recording Stop at the next bar retains the exact take length.
 Rig timedRec;timedRec.e.loopOptions(0,true,false,2);timedRec.tap();timedRec.run(200,.1f);
 timedRec.e.command(0,loop4::Stop,timedRec.beat,120,4,0,0,3.975);timedRec.run(100,.2f);assert(timedRec.e.slots[0].recording);
 timedRec.run(1,.9f);{auto p=timedRec.e.pin(0);assert(p&&p.buffer->length==300&&!timedRec.e.slots[0].playing);}
 std::cout<<"PASS REC>DUB>PLAY, REC>PLAY>DUB, immediate partial overdub, phase continuity, STOP/play, global clock freeze/resume, independent loop clock, stopped-move restart, single live-input writer, PHASE/free/MEASURE, SINGLE/MULTI, three stop timings; no audio allocations\n";
}
