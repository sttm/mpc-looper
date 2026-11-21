#pragma once
#include "mpc_plugin.h"
#include <algorithm>
#include <cmath>
// Host position identifies explicit starts, never directly sets loop phase.
// Ambiguous running jumps without cycle metadata remain non-destructive.
struct HostTimeline {
 struct Position {bool available=false,restart=false,anchor=false;double beat=0;};
 bool seen=false,playing=false,anchored=false,stoppedMoved=false;
 int source=0;double previous=0,previousStep=0,held=0,stopPosition=0;
 Position update(const mpc::Transport& t,unsigned frames,double rate){
  if(!t.valid){seen=anchored=stoppedMoved=false;source=0;return {};}
  if(!std::isfinite(rate)||rate<=0)return {};
  bool ppq=t.ppqValid&&std::isfinite(t.ppqPos),sample=std::isfinite(t.samplePos);
  int mode=ppq?1:sample?2:0;
  double bpm=t.tempoValid&&std::isfinite(t.bpm)?std::clamp(t.bpm,20.,400.):120;
  double raw=ppq?t.ppqPos:sample?t.samplePos*bpm/(60*rate):0;
  double step=frames*bpm/(60*rate),tolerance=std::max(1e-5,2*std::max(step,previousStep));
  bool available=mode!=0,anchor=available&&!anchored,restart=false;
  if(mode!=source){seen=false;stoppedMoved=false;}
  if(seen&&available){
   if(playing&&!t.playing){
    held=previous+previousStep;
    if(t.cycleEndPpq>t.cycleStartPpq&&previous<t.cycleEndPpq&&held>=t.cycleEndPpq)
        held=t.cycleStartPpq+std::fmod(held-t.cycleStartPpq,t.cycleEndPpq-t.cycleStartPpq);
    stopPosition=raw;stoppedMoved=false;
   }
   else if(!playing&&!t.playing){if(std::fabs(raw-stopPosition)>tolerance)stoppedMoved=true;}
   else if(!playing&&t.playing){
    // A late final stopped timestamp near the held audio position is not a seek.
    bool continuation=std::fabs(raw-held)<=tolerance;
    bool fromStart=std::fabs(raw)<=tolerance&&std::fabs(held)>2*tolerance;
    restart=!continuation&&(fromStart||stoppedMoved||std::fabs(raw-stopPosition)>tolerance);
    stoppedMoved=false;
   }else if(playing&&t.playing&&t.cycling&&std::isfinite(t.cycleStartPpq)&&std::isfinite(t.cycleEndPpq)&&t.cycleEndPpq>t.cycleStartPpq){
    double expected=previous+previousStep,length=t.cycleEndPpq-t.cycleStartPpq;
    bool crossing=expected>=t.cycleEndPpq-tolerance;
    double folded=t.cycleStartPpq+std::fmod(std::max(0.,expected-t.cycleStartPpq),length);
    double distance=std::fabs(raw-folded);
    bool natural=crossing&&std::min(distance,std::fabs(length-distance))<=tolerance;
    bool jumpToStart=std::fabs(raw-t.cycleStartPpq)<=tolerance&&previous>t.cycleStartPpq+2*tolerance;
    restart=jumpToStart&&!natural;
   }
  }
  if(!seen&&!t.playing){held=raw;stopPosition=raw;}
  seen=available;playing=t.playing;source=mode;previous=raw;previousStep=step;anchored=anchored||available;
  return {available,restart,anchor,raw};
 }
};
