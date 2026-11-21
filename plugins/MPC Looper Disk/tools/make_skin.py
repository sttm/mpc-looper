#!/usr/bin/env python3
from pathlib import Path
from looper_artwork import wordmark
import sys,shutil,math
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]; SDK=ROOT.parent.parent/'vendor/mpc-plugin-builder-sdk'
sys.path.insert(0,str(SDK/'tools'));import gen_ui as ui
O=ui.obj;S=ROOT/'skins'
if S.exists():shutil.rmtree(S)
S.mkdir()
BG='#0a0d12';CARD='#181e26';BORDER='#303b47';WHITE='#eef2f6';MUTED='#a1aebb';GREEN='#8fe07a';RED='#ed6e72';AMBER='#ffbf64'
fontPath=ROOT.parent.parent/'assets/fonts/BarlowSemiCondensed-SemiBold.ttf'
fontMedium=ROOT.parent.parent/'assets/fonts/BarlowSemiCondensed-Medium.ttf'
# Generated originals are kept in assets; this is offline sprite packing only.
MATERIALS=ROOT/'assets/physical-v1'
panel_material=Image.open(MATERIALS/'panel.png').convert('RGB')
button_material=Image.open(MATERIALS/'button.png').convert('RGB')
pad_atlas=Image.open(MATERIALS/'pad-states.png').convert('RGB')
pad_tiles=[pad_atlas.crop((x*pad_atlas.width//2,y*pad_atlas.height//2,(x+1)*pad_atlas.width//2,(y+1)*pad_atlas.height//2)) for y in range(2) for x in range(2)]
thumb_material=Image.open(MATERIALS/'fader-thumb.png').convert('RGBA')
thumb_bounds=thumb_material.getchannel('A').point(lambda a:255 if a>24 else 0).getbbox()
thumb_material=thumb_material.crop(thumb_bounds)
card_material=panel_material.resize((295,450),Image.Resampling.LANCZOS)
def material_card(im,draw,x,y,w=295,h=450):
 mask=Image.new('L',(w,h));ImageDraw.Draw(mask).rounded_rectangle((0,0,w-1,h-1),radius=14,fill=255)
 im.paste(card_material.resize((w,h),Image.Resampling.LANCZOS),(x,y),mask)
 draw.rounded_rectangle((x,y,x+w-1,y+h-1),radius=14,outline=BORDER,width=1)

def font(size):
 return ImageFont.truetype(str(fontPath if size>=20 else fontMedium),size)
def text(draw,xy,label,size=20,fill=WHITE,anchor=None):draw.text(xy,label,font=font(size),fill=fill,anchor=anchor)
def panel(title):
 im=panel_material.resize((1280,1280),Image.Resampling.LANCZOS).crop((0,0,1280,628));d=ImageDraw.Draw(im);mark=wordmark(WHITE,'#000000');im.paste(Image.new('RGB',mark.size,WHITE),(20,9),mark.convert('L'));d=ImageDraw.Draw(im);text(d,(1248,30),title,16,MUTED,'rm');d.line((20,63,1260,63),fill=BORDER);return im,d
assets={}
def button(name,label,w,h,accent=False,danger=False,background=BG):
 for on in (False,True):
  im=button_material.resize((w,h),Image.Resampling.LANCZOS);d=ImageDraw.Draw(im)
  color=RED if danger else GREEN
  if on or accent:d.rounded_rectangle((3,3,w-4,h-4),radius=6,outline=color,width=2)
  text(d,(w/2,h/2),label,23 if w>130 else 20,color if on or accent or danger else WHITE,'mm')
  file=f'{name}_{"on" if on else "off"}.png';im.save(S/file);assets[file]=im
 return name+'_on.png',name+'_off.png'
def entry(name,typ,data,bounds,param=None,conds=None,focus='No'):
 return ui.entry(name,typ,data,bounds,focus,'Hide' if conds else 'Show','Always',remap=[] if param is None else [('Data',f'Parameter {param}')],invalidating=conds)
def image(file,bounds,param=None,frames=None):
 data=O(('version',2),('imageType','FilmStrip' if frames else 'Regular'),('colour','00000000'),('image',file))
 if frames:data+=O(('numFrames',frames),('handleName','Data'),('animated',False))
 return entry(file,'Image',data,bounds,param)
def label(name,p,bounds,size=24,color='ffeef2f6'):
 data=ui.label_data('Value',color,size,'left verticallyCentred')
 return entry(name,'Label',data,bounds,p)
def btn(name,label_,bounds,p,value=1,total=2,accent=False,danger=False):
 x,y,w,h=map(int,bounds.split());on,off=button(name,label_,w,h,accent,danger,CARD if name.startswith(('rec','stop','dub','undo','clear','select')) else BG)
 return entry(name,'Button',ui.button_data(on,off,value,total),bounds,p)
def idx(n,f):return 11+n*10+f
# Same native focus/Q-Link action wrapper as the working AirLink faders.
focus=entry('focus','Focus',ui.focus_data('ff8fe07a'),'0 0 255 44')
for key,value in focus:
 if key=='bounds':
  for i,(k,v) in enumerate(value):
   if k=='whenVisible':value[i]=(k,'WhenFocussed')
level_control=ui.component_definition(ui.control_actions('looper level editor'),[
 focus,entry('rail','Knob',ui.knob_filmstrip_data('level.png',127,'Horizontal'),'0 0 255 44')
],'00000000')
definitions=[('looper level',level_control),('looper level editor',ui.overlay_component('KnobOverlay',ui.knob_overlay_data(False),'ff0a0d12'))]
# Whole-frame progress, 256 fixed states. No runtime shape rendering.
progress=Image.new('RGB',(255,12*256),CARD);d=ImageDraw.Draw(progress)
for frame in range(256):
 y=frame*12;d.rectangle((0,y+2,254,y+9),fill=BORDER)
 if frame:d.rectangle((0,y+2,round(254*frame/255),y+9),fill=GREEN)
progress.save(S/'progress.png')
state=Image.new('RGB',(145,36*12),CARD);d=ImageDraw.Draw(state)
for n,title in enumerate(('EMPTY','ARMED','RECORDING','PLAYING','STOPPED','OVERDUB','FINISHING','BUSY','TOO LONG','PAUSED','RAM LIMIT','DISK ERROR')):
 state.paste(card_material.crop((20,55,165,91)),(0,n*36))
 text(d,(0,n*36+18),title,19,RED if n in (1,2) else AMBER if n in (5,6) else GREEN if n==3 else MUTED,'lm')
state.save(S/'states.png')
slider=Image.new('RGB',(255,44*127),CARD);d=ImageDraw.Draw(slider)
for f in range(127):
 y=f*44;x=8+round(f/126*238);d.line((8,y+22,246,y+22),fill=BORDER,width=5);d.line((8,y+22,x,y+22),fill=GREEN,width=5);d.ellipse((x-6,y+16,x+6,y+28),fill=WHITE)
slider.save(S/'level.png')
# Static pedal state images: no real-time arcs, glow effects or drawing code.
pedal=Image.new('RGB',(150,150*12),CARD)
# State-to-cell mapping: green, red, amber, unlit. No colour computation on MPC.
state_cells=[3,1,1,0,3,2,2,1,1,3,1,1]
for n,cell in enumerate(state_cells):
 base=card_material.crop((73,290,223,440));mask=Image.new('L',(300,300));ImageDraw.Draw(mask).ellipse((4,4,296,296),fill=255)
 base.paste(pad_tiles[cell].resize((150,150),Image.Resampling.LANCZOS),(0,0),mask.resize((150,150),Image.Resampling.LANCZOS));pedal.paste(base,(0,n*150))
pedal.save(S/'pedal.png')
for on in (False,True):
 im=Image.new('RGBA',(300,300),(0,0,0,0));q=ImageDraw.Draw(im);c=WHITE
 mask=Image.new('L',(300,300));ImageDraw.Draw(mask).ellipse((60,60,240,240),fill=255)
 im.paste(pad_tiles[0].resize((300,300),Image.Resampling.LANCZOS),(0,0),mask)
 q.polygon(((79,128),(79,172),(123,150)),fill=c);q.line((143,174,157,126),fill=c,width=4);q.ellipse((177,128,221,172),fill=c)
 im=im.resize((150,150),Image.Resampling.LANCZOS);file='pedal_'+('on' if on else 'off')+'.png';im.save(S/file);assets[file]=im
for n in range(4):
 for on in (False,True):
  im=card_material.crop((35,198,125,288)).resize((180,180),Image.Resampling.LANCZOS)
  mask=Image.new('L',(148,148));ImageDraw.Draw(mask).ellipse((0,0,147,147),fill=255)
  im.paste(pad_tiles[3].resize((148,148),Image.Resampling.LANCZOS),(16,16),mask);q=ImageDraw.Draw(im)
  if on:q.ellipse((32,32,148,148),outline=WHITE,width=4)
  q.rectangle((74,74,106,106),fill=WHITE) # same 16px icon at 2x rendering
  im=im.resize((90,90),Image.Resampling.LANCZOS);file=f'stop{n}_'+('on' if on else 'off')+'.png';im.save(S/file);assets[file]=im
FADER_FRAMES=96 # 14400px filmstrip stays below the catalog's 16384px limit
fader=Image.new('RGB',(60,150*FADER_FRAMES),CARD)
for n in range(FADER_FRAMES):
 im=card_material.crop((200,115,260,265)).resize((120,300),Image.Resampling.LANCZOS);q=ImageDraw.Draw(im)
 q.rounded_rectangle((40,12,80,288),radius=12,fill='#090c10',outline=BORDER,width=2)
 y=276-round(n/(FADER_FRAMES-1)*252);q.rectangle((57,y,63,276),fill=GREEN)
 for tick in range(9):
  ty=24+tick*31;q.line((18,ty,30,ty),fill=MUTED,width=2);q.line((90,ty,102,ty),fill=MUTED,width=2)
 cap=thumb_material.resize((84,28),Image.Resampling.LANCZOS)
 im.paste(cap,(18,y-14),cap)
 fader.paste(im.resize((60,150),Image.Resampling.LANCZOS),(0,n*150))
fader.save(S/'fader.png')
fader_focus=entry('focus','Focus',ui.focus_data('ff8fe07a'),'0 0 60 150')
for key,value in fader_focus:
 if key=='bounds':
  for i,(k,v) in enumerate(value):
   if k=='whenVisible':value[i]=(k,'WhenFocussed')
definitions.append(('looper fader',ui.component_definition(ui.control_actions('looper level editor'),[
 fader_focus,entry('fader','Knob',ui.knob_filmstrip_data('fader.png',FADER_FRAMES,'Vertical'),'0 0 60 150')
],'00000000')))
main,d=panel('')
children=[image('main.png','0 0 1280 628')]
text(d,(470,27),'BPM',15,MUTED);children.append(label('tempo',9,'390 11 85 37',25))
for n,title in enumerate(('OFF','BEAT','BAR')):children.append(btn('quant'+str(n),title,f'{570+n*85} 13 75 37',2,n,3))
children.append(label('disk state',64,'840 14 160 31',19))
children.append(btn('save','COLLECT SESSION','1015 11 245 43',7))
for n in range(4):
 start=len(children)
 x=20+n*315;material_card(main,d,x,85)
 # Data chooses the internal slot; Bank names a parameter in that slot's
 # hardware row. Native Mouse Down -> Q-Link selects its host bank.
 selection=btn('select'+str(n),'LOOP '+str(n+1),'0 0 255 36',None,n,4)
 actions=[ui.action('Mouse Down','Q-Link','Bank','')]
 definitions.append(('loop selector '+str(n),ui.component_definition(actions,[selection],'00000000')))
 selector=entry('loop selector '+str(n),'loop selector '+str(n),O(('version',1),('handleName','Data')),f'{x+20} 97 255 36',51)
 for key,value in selector:
  if key=='handle remapping':
   for k,v in value:
    if k=='map':v.append(O(('key','Bank'),('value',f'Parameter {90+n}')))
 children.append(selector)
 children.append(image('states.png',f'{x+20} 140 145 36',idx(n,8),12))
 children.append(label('length '+str(n),idx(n,7),f'{x+175} 144 100 30',19))
 children.append(image('progress.png',f'{x+20} 176 255 12',idx(n,9),256))
 children.append(btn('clear'+str(n),'CLEAR',f'{x+20} 200 125 38',idx(n,4),danger=True))
 children.append(btn('undo'+str(n),'UNDO',f'{x+20} 244 125 38',idx(n,3)))
 children.append(entry('stop'+str(n),'Button',ui.button_data(f'stop{n}_on.png',f'stop{n}_off.png',1,2),f'{x+35} 283 90 90',idx(n,2)))
 children.append(entry('level '+str(n),'looper fader',O(('version',1),('handleName','Data')),f'{x+200} 200 60 150',90+n,focus='Yes'))
 children.append(label('gain '+str(n),idx(n,6),f'{x+172} 350 112 25',20))
 children.append(image('pedal.png',f'{x+73} 375 150 150',idx(n,8),12))
 children.append(entry('rec'+str(n),'Button',ui.button_data('pedal_on.png','pedal_off.png',1,2),f'{x+73} 375 150 150',idx(n,0)))
children.append(btn('allplay','ALL PLAY','20 551 235 50',4,accent=True))
children.append(btn('allstop','ALL STOP','270 551 235 50',5))
children.append(btn('sync','SYNC BPM','535 551 145 50',56,total=1))
children.append(btn('warp','WARP','690 551 145 50',57,total=1))
children.append(btn('exportall','EXPORT ALL WAV','865 551 395 50',6))
main.save(S/'main.png');mainchildren=children
settings,d=panel('SETUP / MAXIMUM 32 BARS PER LOOP')
children=[image('setup.png','0 0 1280 628')]
text(d,(30,88),'NEXT RECORDING LENGTH',21)
text(d,(650,88),'4 INDEPENDENT LOOPS',20,MUTED)
text(d,(1100,99),'REC ACTION',18,MUTED,'mm')
for n in range(4):
 y=132+n*73;text(d,(30,y+14),'LOOP '+str(n+1),22)
 for choice,title in enumerate(('FREE','1 BAR','2 BARS','4 BARS','8 BARS','16 BARS','32 BARS')):
  children.append(btn(f'len{n}_{choice}',title,f'{190+choice*100} {y} 90 53',idx(n,7),choice,7))
 children.append(btn('rec_dub'+str(n),'REC > DUB',f'920 {y} 145 53',60+n,0,2))
 children.append(btn('rec_play'+str(n),'REC > PLAY',f'1080 {y} 165 53',60+n,1,2))
text(d,(30,446),'LIVE INPUT MONITOR',20)
children.append(btn('monitoroff','OFF','380 438 180 48',1,0,2));children.append(btn('monitoron','ON','580 438 180 48',1,1,2))
text(d,(30,508),'FADER MAX',20)
children.append(btn('fadermax0','0 dB','380 497 180 48',89,0,2))
children.append(btn('fadermax6','+6 dB','580 497 180 48',89,1,2))
text(d,(30,561),'Tempo mode:',19,MUTED);children.append(label('tempo processing',59,'220 550 460 40',25))
settings.save(S/'setup.png');setupchildren=children
files,d=panel('FILES / DISK SESSION / KEEP CONTENT WITH PROJECT')
children=[image('files.png','0 0 1280 628')]
text(d,(30,90),'EXPORT ONE LOOP',22)
children.append(btn('original','ORIGINAL','650 77 240 45',58,0,2));children.append(btn('currenttempo','CURRENT TEMPO','910 77 335 45',58,1,2))
for n in range(4):children.append(btn('export'+str(n),'LOOP '+str(n+1)+' WAV',f'{30+n*315} 138 275 65',idx(n,5)))
children.append(btn('file_exportall','EXPORT ALL AS SEPARATE WAV','30 236 600 66',6,accent=True))
children.append(btn('file_save','COLLECT / SAVE','30 339 285 58',7))
children.append(btn('file_restore','LOAD BACKUP','345 339 285 58',8))
text(d,(670,244),'WAV: stereo PCM24, one loop cycle.',21,MUTED)
text(d,(670,280),'Post-looper MPC effects are not exported.',18,MUTED)
text(d,(670,345),'Collect copies source files; keep them with the project.',18,MUTED)
text(d,(30,433),'STATUS / LAST EXPORT',16,MUTED);children.append(label('files',10,'30 459 1220 42',23))
text(d,(30,538),'READ MISSES',15,MUTED);children.append(label('disk misses',65,'140 527 130 35',21))
text(d,(300,538),'I/O ERRORS',15,MUTED);children.append(label('disk errors',66,'410 527 130 35',21))
children.append(label('cache lock',67,'600 527 220 35',21));children.append(label('audio cache',68,'850 527 210 35',21))
text(d,(30,580),'Files are inside this plugin folder: Content/Exports and Content/Sessions.',19,MUTED)
files.save(S/'files.png');filechildren=children
control,d=panel('UNIVERSAL MIDI LEARN / SELECTED LOOP')
children=[image('control.png','0 0 1280 628')]
text(d,(30,92),'SELECT LOOP',22)
for n in range(4):children.append(btn('control_select'+str(n),'LOOP '+str(n+1),f'{30+n*315} 138 275 58',51,n,4))
children.append(btn('record_selected','RECORD SELECTED','30 250 580 72',52,accent=True))
children.append(btn('next_loop','NEXT LOOP','665 250 580 72',54))
text(d,(30,365),'SELECTED LEVEL',22)
for n in range(4):children.append(entry('selected level '+str(n),'looper level',O(('version',1),('handleName','Data')),'30 411 255 44',90+n,conds=[f'IndexedEnabling/{n}/4/Parameter 51'],focus='Yes'))
children.append(label('selected gain',53,'330 411 200 44',28))
text(d,(30,493),'MIDI Learn: button -> Record Selected; fader -> Selected Level.',22,MUTED)
text(d,(30,535),'External selection button -> Next Loop. Turning a loop level also selects that loop.',19,MUTED)
text(d,(30,578),'Turn a bank level/length to select its loop. Bank-button-only selection is not confirmed.',18,MUTED)
control.save(S/'control.png');controlchildren=children
group_panel,d=panel('GROUP / SHARED QUANTIZED START AND STOP')
children=[image('group.png','0 0 1280 628')]
text(d,(40,100),'SELECT ANY TWO, THREE OR FOUR LOOPS',25)
for n in range(4):children.append(btn('group_loop'+str(n),'LOOP '+str(n+1),f'{40+n*310} 155 280 75',69+n,total=1))
children.append(btn('group_play','GROUP PLAY','40 280 580 100',73,accent=True))
children.append(btn('group_stop','GROUP STOP','660 280 580 100',74))
children.append(btn('restart_loops','RESTART LOOPS','40 430 360 65',75))
text(d,(430,456),'Restart phase only; locally stopped loops stay stopped.',23,MUTED)
text(d,(40,550),'Play uses the LOOPS grid. Stop uses the latest selected stop boundary.',20,MUTED)
group_panel.save(S/'group.png');groupchildren=children
playback,d=panel('PLAYBACK / LOOP SYNC / STOP TIMING')
children=[image('playback.png','0 0 1280 628')]
for x,title in ((40,'LOOP'),(190,'LOOP SYNC'),(430,'PLAY MODE'),(765,'STOP MODE')):text(d,(x,100),title,20,MUTED)
for n in range(4):
 y=145+n*78;text(d,(40,y+30),str(n+1),30,WHITE,'lm')
 children.append(btn('sync_off'+str(n),'OFF',f'150 {y} 90 58',76+n,0,2))
 children.append(btn('sync_on'+str(n),'ON',f'250 {y} 90 58',76+n,1,2))
 children.append(btn('multi'+str(n),'MULTI',f'390 {y} 120 58',80+n,0,2))
 children.append(btn('single'+str(n),'SINGLE',f'520 {y} 120 58',80+n,1,2))
 for mode,title in enumerate(('IMMEDIATE','BEAT','MEASURE')):
  children.append(btn('stop_mode'+str(n)+'_'+str(mode),title,f'{690+mode*185} {y} 170 58',85+n,mode,3))
text(d,(40,483),'SYNC RESUME',20,MUTED)
children.append(btn('resume_phase','PHASE','230 465 250 58',84,0,2))
children.append(btn('resume_measure','MEASURE','500 465 250 58',84,1,2))
text(d,(40,555),'PHASE: current position now. MEASURE: phrase zero at next bar.',20,MUTED)
text(d,(40,589),'SYNC OFF: immediate phrase zero. SINGLE tracks exclude other SINGLE tracks.',18,MUTED)
playback.save(S/'playback.png');playbackchildren=children
pages=[('LOOPS',mainchildren),('SETUP',setupchildren),('FILES',filechildren),('CONTROL',controlchildren),('GROUP',groupchildren),('PLAYBACK',playbackchildren)]
tabs=[]
for n,(name,items) in enumerate(pages):
 bounds=[f'{20+b*315} 85 295 450' for b in range(4)] if n==0 else [f'20 {122+b*73} 1240 65' for b in range(4)] if n==1 else ['20 80 1240 410'] if n==3 else []
 tabs.append(O(('version',1),('tabName',name),('fnKeyIndex',n),('fnKeySubIndex',0),('componentDefinition',ui.component_definition([],items,'ff0a0d12')),('qlinkBoundsData',bounds)))
tui=O(('pageData',O(('version',1),('componentDefinitions',O(('version',2),('importFiles',[]),('localComponentDefinitions',[O(('key',k),('value',v)) for k,v in definitions]))),('replacementStrings',[]),('info',O(('version',1),('type','CompleteDescription'))),('tabs',tabs))))
(S/'TUI.json').write_text(ui.dump_json(tui)+'\n')
maps=[]
for tab in range(6):
 values={q:p for n in range(4) for q,p in ((1+n*4,90+n),(2+n*4,idx(n,7)),(3+n*4,60+n),(4+n*4,-1))} if tab<2 else {}
 if tab==3:values={1:51,2:53}
 q=O(*[(f'Q-Link {i}',values.get(i,-1)) for i in range(1,17)])
 maps.append(O(('Tab',tab+1),('SubTab',1),('Bank Direction','Row'),('Q-Links',q)))
ql=O(('version',4),('info',O(('version',1),('type','CompleteDescription'))),('Screen Mode Q-Links',O(('version',4),('map',maps))),('Program Mode Q-Links',O(*[(f'Q-Link {i}',{q:p for n in range(4) for q,p in ((1+n*4,90+n),(2+n*4,idx(n,7)),(3+n*4,60+n),(4+n*4,-1))}.get(i,-1)) for i in range(1,17)])))
(S/'Q-Links.json').write_text(ui.dump_json(ql)+'\n')
# Offline visual preview; states are illustrative, not hardware measurements.
preview=main.copy()
for n in range(4):
 x=20+n*315;frame=(3,5,4,0)[n];preview.paste(state.crop((0,frame*36,145,(frame+1)*36)),(x+20,140))
 pd=ImageDraw.Draw(preview);text(pd,(x+175,147),'4 bars',19);text(pd,(x+172,350),'0.0 dB',20)
 for fname,px,py in [(f'select{n}_{"on" if n==1 else "off"}.png',x+20,97),(f'clear{n}_off.png',x+20,200),(f'undo{n}_off.png',x+20,244),(f'stop{n}_off.png',x+35,283)]:preview.paste(assets[fname],(px,py))
 preview.paste(fader.crop((0,(FADER_FRAMES-1)*150,60,FADER_FRAMES*150)),(x+200,200))
 preview.paste(pedal.crop((0,frame*150,150,(frame+1)*150)),(x+73,375));preview.paste(assets['pedal_off.png'],(x+73,375),assets['pedal_off.png'])
 p=(192,104,0,0)[n];preview.paste(progress.crop((0,p*12,255,(p+1)*12)),(x+20,176))
for fname,x,y in [('allplay_off.png',20,551),('allstop_off.png',270,551),('sync_off.png',535,551),('warp_off.png',690,551),('exportall_off.png',865,551),('save_off.png',1015,11)]:preview.paste(assets[fname],(x,y))
for n in range(3):preview.paste(assets[f'quant{n}_{"on" if n==2 else "off"}.png'],(570+n*85,13))
preview4=preview.copy()
for im in (preview,preview4):pd=ImageDraw.Draw(im);text(pd,(390,16),'120.0',25);text(pd,(850,19),'READY',19)
(ROOT/'build').mkdir(exist_ok=True);preview.save(ROOT/'build/UI-preview.png');preview4.save(ROOT/'build/UI-preview-4.png')
print('Generated physical-material four-loop UI:',S)
