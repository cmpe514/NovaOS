const $=id=>document.getElementById(id),os=$('os');
const appIds=['notes','tasks','calculator','clock','weather','music','photos','browser','files','calendar','settings'];
const state={wifi:true,bluetooth:false,focus:false,quiet:false,alt:false,motion:false,clock24:false,accent:'mint',brightness:100,scale:100};
Object.assign(state,window.novaSystem?.initialState?.()||{}); // NovaOS: restore persisted settings and real device state
let current='home',overlay=null,toastTimer,backendRecords=[],dataReady=false,loadTimer,writeQueue=Promise.resolve(),lastWrite=0,audioContext,musicTimer;
const pending=new Map(),sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function message(text){const el=$('toast');el.textContent=text;el.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>el.hidden=true,5000)}
function closeOverlay(){if(overlay){$(overlay).hidden=true;overlay=null}}
function show(id){
  window.novaSystem?.closePanels?.(); // NovaOS: system panels (Wi-Fi, Bluetooth) never stay open behind an app
  if(id==='shade'||id==='drawer'){if(overlay===id){closeOverlay();return}closeOverlay();$(id).hidden=false;overlay=id;$(id).querySelector('[data-close-overlay]')?.focus();return}
  closeOverlay();
  if(!['home',...appIds].includes(id))return;
  $(current).hidden=true;$(id).hidden=false;current=id;
  if(id==='calendar')renderCalendar();
  window.scrollTo({top:0,behavior:'instant'});
}
document.querySelectorAll('[data-open]').forEach(b=>b.addEventListener('click',()=>show(b.dataset.open)));
document.querySelectorAll('[data-close-overlay]').forEach(b=>b.addEventListener('click',closeOverlay));
$('status-settings').addEventListener('click',()=>show('shade'));
$('home-button').addEventListener('click',()=>show('home'));
const drawer=$('drawer-grid');
document.querySelectorAll('#home .shortcut[data-open]').forEach(button=>{
  if(button.dataset.open==='drawer')return;
  const copy=button.cloneNode(true);
  // (an editor-only line that stripped template attributes from the copy was removed: they no longer exist)
  copy.addEventListener('click',()=>show(copy.dataset.open));
  drawer.appendChild(copy);
});
function tick(){
  const date=new Date(),options={hour:'2-digit',minute:'2-digit',hour12:!state.clock24};
  const time=date.toLocaleTimeString(undefined,options);
  $('status-time').textContent=time;$('home-time').textContent=time;
  $('clock-large').textContent=date.toLocaleTimeString(undefined,{...options,second:'2-digit'});
  $('home-date').textContent=date.toLocaleDateString(undefined,{weekday:'long',month:'long',day:'numeric'});
  $('clock-date').textContent=date.toLocaleDateString(undefined,{weekday:'long',year:'numeric',month:'long',day:'numeric'});
}
function sync(){
  os.classList.toggle('alt',state.alt);os.classList.toggle('focus',state.focus);os.classList.toggle('no-motion',state.motion);
  // Brightness is applied to the wallpaper, never to the OS container: fixed overlays and the dock keep their dimensions.
  const base=state.focus?.34:state.alt?.34:.48;
  // NovaOS: with a real backlight the slider drives the panel instead, so the wallpaper is not dimmed twice.
  $('os').querySelector('.wallpaper').style.opacity=window.novaSystem?.hardwareBrightness?.()?'1':String(Math.min(1,Math.max(.35,state.brightness/100)));
  document.documentElement.style.setProperty('--accent',{mint:'%%ACCENT_MINT%%',peach:'%%ACCENT_PEACH%%',blue:'%%ACCENT_BLUE%%'}[state.accent]);
  document.documentElement.style.setProperty('--scale',state.scale/100);
  document.querySelectorAll('[data-switch]').forEach(b=>b.setAttribute('aria-pressed',String(state[b.dataset.switch])));
  for(const key of ['alt','focus','quiet','motion','clock24','wifi','bluetooth'])$('setting-'+key).checked=state[key];
  $('setting-accent').value=state.accent;$('setting-brightness').value=state.brightness;$('brightness').value=state.brightness;$('setting-scale').value=state.scale;
  $('wifi-icon').style.opacity=state.wifi?'1':'.3';
  tick();
  window.novaSystem?.stateChanged?.(state); // NovaOS: persist settings, apply them to the device
}
document.querySelectorAll('[data-switch]').forEach(button=>button.addEventListener('click',()=>{const key=button.dataset.switch;state[key]=!state[key];sync()}));
for(const key of ['alt','focus','quiet','motion','clock24','wifi','bluetooth'])$('setting-'+key).addEventListener('change',e=>{state[key]=e.target.checked;sync()});
for(const [id,key] of [['brightness','brightness'],['setting-brightness','brightness'],['setting-scale','scale']])$(id).addEventListener('input',e=>{state[key]=Number(e.target.value);sync()});
$('setting-accent').addEventListener('change',e=>{state.accent=e.target.value;sync()});
sync();setInterval(tick,1000);

let display='0',previous=null,operation=null,fresh=false;
function calculate(a,b,op){return op==='+'?a+b:op==='−'?a-b:op==='×'?a*b:op==='÷'?a/b:b}
$('calc-keys').addEventListener('click',event=>{
  const key=event.target.closest('[data-key]')?.dataset.key;if(!key)return;
  if(key==='C'){display='0';previous=null;operation=null;fresh=false}
  else if(key==='⌫'){display=display.length>1?display.slice(0,-1):'0'}
  else if(/^[0-9.]$/.test(key)){if(fresh||display==='Error'){display='0';fresh=false}if(key!=='.'||!display.includes('.'))display=display==='0'&&key!=='.'?key:display+key}
  else if(key==='='){if(operation!==null&&previous!==null){const result=calculate(previous,Number(display),operation);display=Number.isFinite(result)?String(Number(result.toPrecision(11))):'Error';previous=null;operation=null;fresh=true}}
  else{if(operation!==null&&!fresh&&previous!==null){const result=calculate(previous,Number(display),operation);display=Number.isFinite(result)?String(Number(result.toPrecision(11))):'Error'}previous=Number(display);operation=key;fresh=true}
  $('calc-display').textContent=display;
});
let displayedMonth=new Date();displayedMonth.setDate(1);
function renderCalendar(){
  const y=displayedMonth.getFullYear(),m=displayedMonth.getMonth(),today=new Date(),days=$('calendar-days');
  $('month-heading').textContent=displayedMonth.toLocaleDateString(undefined,{month:'long',year:'numeric'});
  days.replaceChildren();
  for(let i=0;i<new Date(y,m,1).getDay();i++)days.appendChild(document.createElement('span'));
  for(let d=1;d<=new Date(y,m+1,0).getDate();d++){
    const cell=document.createElement('span');cell.className='rounded-xl py-3 text-sm';cell.textContent=d;
    if(d===today.getDate()&&m===today.getMonth()&&y===today.getFullYear()){cell.style.background='var(--accent)';cell.style.color='#10222c';cell.classList.add('font-bold')}
    days.appendChild(cell);
  }
}
$('month-prev').addEventListener('click',()=>{displayedMonth.setMonth(displayedMonth.getMonth()-1);renderCalendar()});
$('month-next').addEventListener('click',()=>{displayedMonth.setMonth(displayedMonth.getMonth()+1);renderCalendar()});
$('month-today').addEventListener('click',()=>{displayedMonth=new Date();displayedMonth.setDate(1);renderCalendar()});

let tabSerial=0,activeTab, tabs=[];
function newTab(){const tab={id:++tabSerial,history:[],position:-1,draft:''};tabs.push(tab);activeTab=tab.id;renderTabs();return tab}
function active(){return tabs.find(t=>t.id===activeTab)}
function currentUrl(tab=active()){return tab?.history[tab.position]||''}
function normalizedAddress(raw){
  const text=raw.trim();
  if(!text)return null;
  if(/^https?:\/\//i.test(text)){try{const url=new URL(text);return url.protocol==='https:'||url.protocol==='http:'?url.href:null}catch{return null}}
  if(/^[^\s./]+\.[^\s]+/.test(text)){try{return new URL('https://'+text).href}catch{return null}}
  return '%%SEARCH_URL%%'+encodeURIComponent(text);
}
function renderTabs(){
  const list=$('browser-tab-list');
  for(const node of [...list.children])if(!tabs.some(t=>String(t.id)===node.dataset.tabId))node.remove();
  tabs.forEach((tab,index)=>{
    let node=[...list.children].find(el=>el.dataset.tabId===String(tab.id));
    if(!node){
      node=document.createElement('div');node.className='browser-tab';node.dataset.tabId=tab.id;node.setAttribute('role','tab');
      const name=document.createElement('button');name.type='button';name.className='browser-tab-name';name.addEventListener('click',()=>{activeTab=tab.id;renderTabs()});
      const close=document.createElement('button');close.type='button';close.className='browser-tab-close';close.setAttribute('aria-label','Close tab');close.textContent='×';
      close.addEventListener('click',()=>{const at=tabs.findIndex(t=>t.id===tab.id);tabs.splice(at,1);if(!tabs.length)newTab();else if(activeTab===tab.id)activeTab=tabs[Math.min(at,tabs.length-1)].id;renderTabs()});
      node.append(name,close);
    }
    node.setAttribute('aria-selected',String(tab.id===activeTab));
    const url=currentUrl(tab);
    let title='New tab';if(url){try{title=new URL(url).hostname.replace(/^www\./,'')}catch{title='Website'}}
    node.querySelector('.browser-tab-name').textContent=title;
    if(list.children[index]!==node)list.insertBefore(node,list.children[index]||null);
  });
  const tab=active();
  $('browser-address').value=currentUrl(tab)||tab.draft;
  $('browser-back').disabled=tab.position<=0;
  $('browser-forward').disabled=tab.position>=tab.history.length-1;
  $('browser-current').textContent=currentUrl(tab);
  $('browser-open').disabled=!currentUrl(tab);
}
$('browser-new-tab').addEventListener('click',()=>{newTab();$('browser-address').focus()});
$('browser-address').addEventListener('input',e=>{active().draft=e.target.value});
$('browser-form').addEventListener('submit',e=>{
  e.preventDefault();const tab=active(),url=normalizedAddress($('browser-address').value);
  if(!url)return message('Enter a valid website address or search.');
  tab.history=tab.history.slice(0,tab.position+1);tab.history.push(url);tab.position++;tab.draft='';
  renderTabs();window.novaSystem.openWebsite(url);
});
$('browser-back').addEventListener('click',()=>{const tab=active();if(tab.position>0){tab.position--;renderTabs()}});
$('browser-forward').addEventListener('click',()=>{const tab=active();if(tab.position<tab.history.length-1){tab.position++;renderTabs()}});
$('browser-open').addEventListener('click',()=>{const url=currentUrl();if(url)window.novaSystem.openWebsite(url)});
newTab();

let lastPhotoFocus;
document.querySelectorAll('.photo-button').forEach(button=>button.addEventListener('click',()=>{
  const img=button.querySelector('img');if(!img.src)return;lastPhotoFocus=button;
  $('photo-large').src=img.src;$('photo-large').alt=img.alt;$('photo-modal').hidden=false;$('photo-close').focus();
}));
function closePhoto(){$('photo-modal').hidden=true;$('photo-large').removeAttribute('src');lastPhotoFocus?.focus()}
$('photo-close').addEventListener('click',closePhoto);
$('photo-modal').addEventListener('click',e=>{if(e.target===$('photo-modal'))closePhoto()});
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(window.novaSystem?.closeTopDialog?.())return;if(!$('photo-modal').hidden)closePhoto();else closeOverlay()}});

let activeNotes=[];
function stopMusic(){clearInterval(musicTimer);musicTimer=null;activeNotes.forEach(n=>{try{n.stop()}catch{}});activeNotes=[];$('music-play').hidden=false;$('music-stop').hidden=true}
function playNote(){
  if(!audioContext||state.quiet)return;
  const frequencies=[261.63,329.63,392,523.25,392,329.63],osc=audioContext.createOscillator(),gain=audioContext.createGain();
  osc.frequency.value=frequencies[Math.floor(Date.now()/480)%frequencies.length];
  gain.gain.setValueAtTime(.001,audioContext.currentTime);
  gain.gain.linearRampToValueAtTime(.075,audioContext.currentTime+.08);
  gain.gain.exponentialRampToValueAtTime(.001,audioContext.currentTime+.42);
  osc.connect(gain);gain.connect(audioContext.destination);osc.start();osc.stop(audioContext.currentTime+.45);
  activeNotes.push(osc);osc.onended=()=>{activeNotes=activeNotes.filter(n=>n!==osc)};
}
$('music-play').addEventListener('click',async()=>{
  if(state.quiet)return message('Turn off Quiet mode to play the melody.');
  try{audioContext=audioContext||new (window.AudioContext||window.webkitAudioContext)();await audioContext.resume();playNote();musicTimer=setInterval(playNote,480);$('music-play').hidden=true;$('music-stop').hidden=false}
  catch{message('Audio is unavailable in this browser.')}
});
$('music-stop').addEventListener('click',stopMusic);

const handler={onDataChanged(data){clearTimeout(loadTimer);dataReady=true;backendRecords=data;renderRecords()}};
async function initialize(){
  clearTimeout(loadTimer);dataReady=false;
  for(const type of ['note','task','file']){const el=$(type+'-loading');el.hidden=false;el.textContent='Loading saved '+type+'s…'}
  loadTimer=setTimeout(()=>{if(!dataReady)loadError('Could not load saved items.')},8000);
  try{const result=await window.dataSdk.init(handler);if(!result.isOk){clearTimeout(loadTimer);loadError('Could not reach '+window.novaSystem.storageName()+'.')}}
  catch{clearTimeout(loadTimer);loadError('Could not reach '+window.novaSystem.storageName()+'.')}
}
function loadError(text){
  for(const type of ['note','task','file']){
    const el=$(type+'-loading');el.replaceChildren(document.createTextNode(text+' '));
    const retry=document.createElement('button');retry.type='button';retry.className='small-action';retry.textContent='Retry';retry.addEventListener('click',initialize);el.appendChild(retry);
  }
  message(text);
}
function recordById(id){return backendRecords.find(r=>String(r.__backendId)===id)}
function renderList(type){
  const list=$(type+'-list'),records=backendRecords.filter(r=>r.app_type===type).sort((a,b)=>String(b.created_at).localeCompare(String(a.created_at)));
  $(type+'-loading').hidden=dataReady;$(type+'-empty').hidden=!dataReady||records.length>0;
  const ids=new Set(records.map(r=>String(r.__backendId)));
  for(const row of [...list.children])if(!ids.has(row.dataset.recordId))row.remove();
  records.forEach((r,index)=>{
    const id=String(r.__backendId);let row=[...list.children].find(child=>child.dataset.recordId===id);
    if(!row){
      row=$(type+'-template').content.firstElementChild.cloneNode(true);row.dataset.recordId=id;
      const del=row.querySelector('button');del.addEventListener('click',()=>removeRecord(id,del,row));
      if(type==='task')row.querySelector('.task-checkbox').addEventListener('change',e=>toggleTask(id,e.currentTarget.checked));
    }
    if(list.children[index]!==row)list.insertBefore(row,list.children[index]||null);
    const title=row.querySelector('[data-role="saved-'+type+'-title"]');
    if(title.textContent!==r.title)title.textContent=r.title;
    if(type==='task'){
      const checked=pending.has(id)?pending.get(id).completed:Boolean(r.completed);
      row.querySelector('.task-checkbox').checked=checked;title.classList.toggle('line-through',checked);title.classList.toggle('opacity-50',checked);
    }else{
      const content=row.querySelector('[data-role="saved-'+type+'-content"]');
      if(content.textContent!==(r.content||''))content.textContent=r.content||'';
    }
  });
}
// ---------------------------------------------------------------------------------------------
// RECONSTRUCTED: ui/original/ui.html is truncated at this point (mid-statement, line 543). Everything
// below rebuilds the missing behaviour from the variables and calls the surviving code already relies on
// (renderRecords, removeRecord, toggleTask, writeQueue, lastWrite, pending, sleep, the three forms).
// The 999-item cap the hosted SDK imposed is gone; novad enforces its own, much larger, limit.
// ---------------------------------------------------------------------------------------------
function renderRecords(){for(const type of ['note','task','file'])renderList(type)}
const writeSpacing=0; // the hosted SDK needed writes spaced out; the local backend does not, but the queue keeps them ordered
function queueWrite(job){
  const run=writeQueue.then(async()=>{const wait=lastWrite+writeSpacing-Date.now();if(wait>0)await sleep(wait);lastWrite=Date.now();return job()});
  writeQueue=run.catch(()=>{});return run;
}
const labels={note:'Note',task:'Task',file:'File'};
async function createRecord(type,fields,form){
  if(!dataReady)return message('Your saved items are still loading.');
  const button=form.querySelector('[type="submit"]');button.disabled=true;
  try{
    const result=await queueWrite(()=>window.dataSdk.create({app_type:type,title:fields.title,content:fields.content||'',completed:false,created_at:new Date().toISOString()}));
    if(result?.isOk){form.reset();message(labels[type]+' saved.')}
    else message(result?.error||'Could not save. Your text is still in the form; try again.');
  }catch{message('Could not save. Your text is still in the form; try again.')}
  finally{button.disabled=false}
}
async function removeRecord(id,button,row){
  const record=recordById(id);if(!record)return;
  button.disabled=true;row.classList.add('opacity-50');
  try{
    const result=await queueWrite(()=>window.dataSdk.delete(record));
    if(!result?.isOk){button.disabled=false;row.classList.remove('opacity-50');message(result?.error||'Could not delete. Try again.')}
  }catch{button.disabled=false;row.classList.remove('opacity-50');message('Could not delete. Try again.')}
}
async function toggleTask(id,completed){
  const record=recordById(id);if(!record)return;
  pending.set(id,{completed});renderList('task'); // optimistic: strike through immediately
  try{
    const result=await queueWrite(()=>window.dataSdk.update({...record,completed}));
    if(!result?.isOk)message(result?.error||'Could not update the task.');
  }catch{message('Could not update the task.')}
  finally{pending.delete(id);renderList('task')}
}
$('note-form').addEventListener('submit',e=>{e.preventDefault();createRecord('note',{title:$('note-title').value.trim(),content:$('note-content').value},e.currentTarget)});
$('task-form').addEventListener('submit',e=>{e.preventDefault();createRecord('task',{title:$('task-title').value.trim()},e.currentTarget)});
$('file-form').addEventListener('submit',e=>{e.preventDefault();createRecord('file',{title:$('file-name').value.trim(),content:$('file-content').value},e.currentTarget)});

// ---------------------------------------------------------------------------------------------
// NovaOS additions: physical keyboard support (spec 3e). Not part of the original file.
// ---------------------------------------------------------------------------------------------
function typing(target){return target instanceof HTMLElement&&(target.isContentEditable||/^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName)&&!['checkbox','range','button','submit'].includes(target.type))}
const calcKeyMap={'+':'+','-':'−','*':'×','x':'×','X':'×','/':'÷','Enter':'=','=':'=','Backspace':'⌫','Delete':'C','c':'C','C':'C',',':'.','.':'.'};
document.addEventListener('keydown',e=>{
  if(e.defaultPrevented||e.ctrlKey||e.altKey||e.metaKey)return;
  if((e.key==='Home'||e.key==='BrowserHome')&&!typing(e.target)){e.preventDefault();window.novaSystem?.closeTopDialog?.();if(!$('photo-modal').hidden)closePhoto();show('home');return}
  if(current!=='calculator'||overlay||typing(e.target))return;
  if(e.key==='Enter'&&e.target.closest?.('#calc-keys'))return; // a focused keypad button activates itself
  const key=/^[0-9]$/.test(e.key)?e.key:calcKeyMap[e.key];if(!key)return;
  const button=$('calc-keys').querySelector('[data-key="'+key+'"]');if(!button)return;
  e.preventDefault();button.click();
});
function viewPhoto(button){const img=button.querySelector('img');if(!img.src)return;lastPhotoFocus=button;$('photo-large').src=img.src;$('photo-large').alt=img.alt;$('photo-modal').hidden=false;$('photo-close').focus()}
window.novaApp={state,sync,show,message,viewPhoto,current:()=>current}; // narrow hook for system.js

lucide.createIcons();
initialize();
