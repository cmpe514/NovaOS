// NovaOS: connects the shell UI to the real device through novad.
// Rule followed throughout: every toggle or indicator either controls/reflects the real system, or it is
// hidden, or it is explicitly labelled as a preview. Nothing pretends.
(function(){
  'use strict';
  const $=id=>document.getElementById(id);
  const BRAND=document.title.split(' — ')[0];
  const UI_KEYS=['focus','quiet','alt','motion','clock24','accent','brightness','scale'];
  const PREVIEW_KEYS=[...UI_KEYS,'wifi','bluetooth'];
  const SETTINGS_KEY='nova.settings.v1';
  let mode='preview',settings={},status=null,last=null,attached=false;
  let saveTimer,backlightTimer,volumeTimer,pollTimer,weatherTimer,volumeDragging=false;
  let dialogOpener=null,joinTarget=null,storageChoice=null;

  // ---------- helpers ----------
  const req=(method,path,body)=>novaApi.request(method,path,body);
  function message(text){if(window.novaApp)window.novaApp.message(text);else console.warn(text)}
  function text(id,value){const el=$(id);if(el&&el.textContent!==value)el.textContent=value}
  function show(id,visible){const el=$(id);if(el)el.hidden=!visible}
  function el(tag,className,content){const node=document.createElement(tag);if(className)node.className=className;if(content!==undefined)node.textContent=content;return node}
  function button(label,className,onClick){const b=el('button',className||'small-action',label);b.type='button';b.addEventListener('click',onClick);return b}
  function bytes(n){if(!Number.isFinite(n))return '';const u=['B','KB','MB','GB','TB'];let i=0;while(n>=1000&&i<u.length-1){n/=1000;i++}return (i?n.toFixed(n<10?1:0):n)+' '+u[i]}
  function sanitize(input){
    const out={};
    for(const key of ['focus','quiet','alt','motion','clock24','wifi','bluetooth'])if(typeof input?.[key]==='boolean')out[key]=input[key];
    if(['mint','peach','blue'].includes(input?.accent))out.accent=input.accent;
    if(Number.isFinite(input?.brightness))out.brightness=Math.round(Math.min(110,Math.max(45,input.brightness)));
    if(Number.isFinite(input?.scale))out.scale=Math.round(Math.min(120,Math.max(90,input.scale)));
    return out;
  }
  function pick(state,keys){const out={};for(const key of keys)out[key]=state[key];return out}
  // The original slider runs 45..110 (it dims the wallpaper). A real backlight gets 5..100 %.
  const backlightPercent=value=>Math.round(Math.min(100,Math.max(5,(value-45)/65*95+5)));
  function setIcon(id,name,label){
    const old=$(id);if(!old)return;
    if(old.getAttribute('data-lucide')===name&&old.getAttribute('aria-label')===label)return;
    const holder=document.createElement('div'),icon=document.createElement('i');
    icon.id=id;icon.setAttribute('data-lucide',name);icon.setAttribute('role','img');icon.setAttribute('aria-label',label);
    icon.hidden=old.hidden;if(old.style.cssText)icon.setAttribute('style',old.style.cssText);
    holder.appendChild(icon);lucide.createIcons({root:holder});
    old.replaceWith(holder.firstElementChild);
  }

  // ---------- settings ----------
  async function loadSettings(){
    if(mode==='device'){try{settings=sanitize((await req('GET','/api/settings')).settings)}catch{settings={}}}
    else if(mode==='preview'){try{settings=sanitize(JSON.parse(localStorage.getItem(SETTINGS_KEY)||'{}'))}catch{settings={}}}
  }
  function scheduleSave(state){
    clearTimeout(saveTimer);
    saveTimer=setTimeout(async()=>{
      if(mode==='device'){try{await req('PUT','/api/settings',{settings:pick(state,UI_KEYS)})}catch(error){message('Settings could not be saved: '+error.message)}}
      else if(mode==='preview'){try{localStorage.setItem(SETTINGS_KEY,JSON.stringify(pick(state,PREVIEW_KEYS)))}catch{}}
    },300);
  }

  // ---------- device actions ----------
  async function act(path,body,failure){
    try{return await req('POST',path,body)}
    catch(error){message(failure+': '+error.message);await refreshStatus();return null}
  }
  function applyToDevice(state,changed){
    if(mode!=='device'||!status)return;
    if(changed.has('wifi')&&status.wifi)act('/api/wifi',{enabled:state.wifi},'Wi-Fi could not be switched');
    if(changed.has('bluetooth')&&status.bluetooth)act('/api/bluetooth',{enabled:state.bluetooth},'Bluetooth could not be switched');
    if(changed.has('quiet')&&status.audio)act('/api/audio',{muted:state.quiet},'Sound could not be changed');
    if(changed.has('brightness')&&status.backlight){
      clearTimeout(backlightTimer);
      backlightTimer=setTimeout(()=>act('/api/backlight',{percent:backlightPercent(state.brightness)},'Brightness could not be changed'),120);
    }
  }
  // Puts device-reported values into the app state without echoing them back to the device.
  function applyDeviceState(patch){
    const app=window.novaApp;if(!app)return;
    let changed=false;
    for(const [key,value] of Object.entries(patch))if(app.state[key]!==value){app.state[key]=value;changed=true}
    if(changed){last={...last,...patch};app.sync()}
  }

  // ---------- status ----------
  async function refreshStatus(){
    if(mode!=='device')return;
    try{status=await req('GET','/api/system/status')}catch{return}
    if(attached)render();
  }
  function renderIndicators(){
    const net=status.network||{},wifi=status.wifi;
    if(net.state==='connected'&&net.kind==='ethernet')setIcon('wifi-icon','ethernet-port','Wired network connected');
    else if(net.state==='connected'&&net.kind==='wifi')setIcon('wifi-icon','wifi','Wi-Fi connected to '+(net.ssid||'a network'));
    else if(wifi&&wifi.enabled)setIcon('wifi-icon','wifi',net.state==='connecting'?'Wi-Fi connecting':'Wi-Fi on, not connected');
    else setIcon('wifi-icon','wifi-off',wifi?'Wi-Fi off':'Offline');
    const wifiIcon=$('wifi-icon');
    wifiIcon.style.opacity=net.state==='connected'?'1':'.3';
    const strength=net.kind==='wifi'&&net.state==='connected'&&Number.isFinite(net.signal)?net.signal:null;
    if(strength===null)show('signal-icon',false);
    else{
      const level=strength>=75?'signal':strength>=50?'signal-high':strength>=25?'signal-medium':'signal-low';
      setIcon('signal-icon',level,'Wi-Fi signal '+strength+'%');show('signal-icon',true);
    }
    const battery=status.battery;
    if(!battery)show('battery-icon',false);
    else{
      const p=battery.percent,name=battery.charging?'battery-charging':p>=80?'battery-full':p>=40?'battery-medium':p>=15?'battery-low':'battery-warning';
      setIcon('battery-icon',name,'Battery '+p+'%'+(battery.charging?', charging':''));show('battery-icon',true);
    }
  }
  function renderStorage(){
    const storage=status.storage||{};
    if(storage.persistent){
      text('storage-title','Saving to '+(storage.label||'NOVA-DATA')+(storage.device?' ('+storage.device+')':''));
      text('storage-note',(Number.isFinite(storage.freeBytes)?bytes(storage.freeBytes)+' free of '+bytes(storage.totalBytes)+'. ':'')+'Notes, tasks, files and settings survive restarts.');
    }else{
      text('storage-title','Changes won’t be saved');
      text('storage-note','No NOVA-DATA disk was found, so notes, tasks, files and settings are kept in memory and are lost when this device shuts down.'+(storage.canSetup?' Set up an empty disk to keep them.':''));
    }
    show('storage-setup-open',!storage.persistent&&storage.canSetup);
    const list=$('usb-list');list.replaceChildren();
    for(const drive of status.usb||[]){
      const row=el('div','setting-row');
      const info=el('div');info.append(el('p','font-semibold',drive.label||drive.name),el('p','',(drive.sizeBytes?bytes(drive.sizeBytes)+' · ':'')+(drive.mounted?'Connected — text files appear in the Files app':'USB drive, not opened')));
      const action=drive.mounted
        ?button('Eject','small-action shrink-0',async()=>{if(await act('/api/usb/unmount',{id:drive.id},'The drive could not be ejected'))message('You can now remove '+(drive.label||'the drive')+'.');refreshStatus()})
        :button('Open','small-action shrink-0',async()=>{await act('/api/usb/mount',{id:drive.id},'The drive could not be opened');refreshStatus()});
      row.append(info,action);list.appendChild(row);
    }
    renderUsbFiles();
  }
  function render(){
    if(!status)return;
    renderIndicators();
    const wifi=status.wifi,bluetooth=status.bluetooth;
    document.querySelector('[data-switch="wifi"]').hidden=!wifi;show('wifi-row',Boolean(wifi));
    document.querySelector('[data-switch="bluetooth"]').hidden=!bluetooth;show('bluetooth-row',Boolean(bluetooth));
    const radios=[wifi&&'Wi-Fi',bluetooth&&'Bluetooth'].filter(Boolean);
    text('quick-disclaimer',radios.length?radios.join(' and ')+(radios.length>1?' tiles switch':' tile switches')+' this device’s radio'+(radios.length>1?'s':'')+'.':'This device has no Wi-Fi or Bluetooth radio.');
    const patch={};
    if(wifi)patch.wifi=Boolean(wifi.enabled);
    if(bluetooth)patch.bluetooth=Boolean(bluetooth.enabled);
    applyDeviceState(patch);
    show('volume-item',Boolean(status.audio));
    if(status.audio&&!volumeDragging)$('volume').value=String(status.audio.volume);
    text('brightness-note',status.backlight?'Adjust the screen backlight.':'Dims the wallpaper; this display has no adjustable backlight.');
    const time=status.time||{};
    text('time-sync-note',time.ntpSynced?'Set automatically from network time.':time.ntpEnabled?'Waiting for network time. The clock may be off until this device is online.':'Network time is off; the clock uses the hardware clock.');
    const select=$('setting-timezone');if(time.timezone&&select.options.length&&select.value!==time.timezone&&document.activeElement!==select)select.value=time.timezone;
    renderStorage();
    window.novaApp?.sync();
  }

  // ---------- dialogs and panels ----------
  function openDialog(id,focusId){dialogOpener=document.activeElement;show(id,true);$(focusId)?.focus()}
  function closeDialog(id){if($(id).hidden)return false;show(id,false);dialogOpener?.focus?.();dialogOpener=null;return true}
  function closePanels(){let closed=false;for(const id of ['network-panel','bluetooth-panel'])if(!$(id).hidden){$(id).hidden=true;closed=true}return closed}
  function closeTopDialog(){return closeDialog('storage-dialog')||closeDialog('power-dialog')||closePanels()}
  function openPanel(id){window.novaApp?.show('settings');closePanels();dialogOpener=document.activeElement;show(id,true);$(id).querySelector('[data-close-panel]')?.focus()}

  // ---------- power ----------
  function setupPower(){
    show('power-button',mode==='device');
    $('power-button').addEventListener('click',()=>{
      const persistent=status?.storage?.persistent;
      text('power-note',persistent?'Your notes, tasks, files and settings are saved before the device turns off.':'Changes from this session are not saved: there is no NOVA-DATA disk.');
      openDialog('power-dialog','power-cancel');
    });
    $('power-cancel').addEventListener('click',()=>closeDialog('power-dialog'));
    $('power-dialog').addEventListener('click',e=>{if(e.target===$('power-dialog'))closeDialog('power-dialog')});
    for(const [id,action,label] of [['power-off','poweroff','Shutting down…'],['power-restart','reboot','Restarting…']]){
      $(id).addEventListener('click',async()=>{
        $('power-off').disabled=$('power-restart').disabled=true;
        const result=await act('/api/power',{action},'The device could not '+(action==='poweroff'?'shut down':'restart'));
        $('power-off').disabled=$('power-restart').disabled=false;
        if(!result)return;
        closeDialog('power-dialog');
        message(result.dryRun?'Development mode: '+(action==='poweroff'?'shutdown':'restart')+' was simulated.':label);
      });
    }
  }

  // ---------- Wi-Fi networks ----------
  async function loadNetworks(){
    text('network-note','Scanning…');show('network-join',false);
    const list=$('network-list');list.replaceChildren();
    let networks;
    try{networks=(await req('GET','/api/wifi/networks')).networks}catch(error){text('network-note','Could not scan: '+error.message);return}
    text('network-note',networks.length?'Choose a network to join.':'No networks found. Make sure Wi-Fi is on.');
    for(const n of networks){
      const row=el('div','item flex justify-between items-center gap-3');
      const info=el('div','min-w-0');
      info.append(el('p','font-semibold break-words',n.ssid),el('p','text-sm text-white/60',[n.connected?'Connected':null,n.security?'Secured':'Open',n.signal+'%'].filter(Boolean).join(' · ')));
      row.append(info,n.connected
        ?button('Disconnect','small-action shrink-0',async()=>{await act('/api/wifi/disconnect',{},'Could not disconnect');loadNetworks();refreshStatus()})
        :button('Connect','small-action shrink-0',()=>join(n)));
      list.appendChild(row);
    }
  }
  async function join(network){
    if(network.security&&!network.known){
      joinTarget=network;text('network-join-name','Join “'+network.ssid+'”');$('network-password').value='';show('network-join',true);$('network-password').focus();return;
    }
    text('network-note','Connecting to '+network.ssid+'…');
    if(await act('/api/wifi/connect',{ssid:network.ssid},'Could not connect'))message('Connected to '+network.ssid+'.');
    loadNetworks();refreshStatus();
  }
  function setupNetworks(){
    $('wifi-networks-open').addEventListener('click',()=>{openPanel('network-panel');loadNetworks()});
    $('network-join-cancel').addEventListener('click',()=>{show('network-join',false);joinTarget=null});
    $('network-join').addEventListener('submit',async e=>{
      e.preventDefault();if(!joinTarget)return;
      const ssid=joinTarget.ssid,password=$('network-password').value;
      show('network-join',false);text('network-note','Connecting to '+ssid+'…');
      if(await act('/api/wifi/connect',{ssid,password},'Could not connect'))message('Connected to '+ssid+'.');
      $('network-password').value='';joinTarget=null;loadNetworks();refreshStatus();
    });
  }

  // ---------- Bluetooth ----------
  async function loadBluetooth(){
    text('bluetooth-panel-note','Searching for nearby devices…');
    const list=$('bluetooth-list');list.replaceChildren();
    let devices;
    try{devices=(await req('GET','/api/bluetooth/devices')).devices}catch(error){text('bluetooth-panel-note','Could not search: '+error.message);return}
    text('bluetooth-panel-note',devices.length?'Make sure the device you want is in pairing mode.':'No devices found. Put your device in pairing mode and search again.');
    for(const d of devices){
      const row=el('div','item flex justify-between items-center gap-3');
      const info=el('div','min-w-0');
      info.append(el('p','font-semibold break-words',d.name||d.address),el('p','text-sm text-white/60',d.connected?'Connected':d.paired?'Paired':'Available'));
      const actions=el('div','flex gap-2 shrink-0');
      const run=(path,failure)=>async()=>{await act(path,{address:d.address},failure);loadBluetooth()};
      if(!d.paired)actions.append(button('Pair','small-action',run('/api/bluetooth/pair','Pairing failed')));
      else{
        actions.append(d.connected?button('Disconnect','small-action',run('/api/bluetooth/disconnect','Could not disconnect')):button('Connect','small-action',run('/api/bluetooth/connect','Could not connect')));
        actions.append(button('Forget','small-action',run('/api/bluetooth/remove','Could not forget the device')));
      }
      row.append(info,actions);list.appendChild(row);
    }
    const again=button('Search again','small-action mt-2',loadBluetooth);list.appendChild(again);
  }
  function setupBluetooth(){$('bluetooth-devices-open').addEventListener('click',()=>{openPanel('bluetooth-panel');loadBluetooth()})}

  // ---------- storage setup (blank disks only, typed confirmation) ----------
  function setupStorage(){
    $('storage-setup-open').addEventListener('click',async()=>{
      storageChoice=null;$('storage-confirm').value='';$('storage-format').disabled=true;
      const list=$('storage-candidates');list.replaceChildren(el('p','','Looking for empty disks…'));
      openDialog('storage-dialog','storage-cancel');
      let disks;
      try{disks=(await req('GET','/api/storage/candidates')).disks}catch(error){list.replaceChildren(el('p','','Could not list disks: '+error.message));return}
      list.replaceChildren();
      if(!disks.length){list.appendChild(el('p','','No empty disk found. Attach a blank disk (nothing on it at all) and try again.'));return}
      for(const disk of disks){
        const label=el('label','item flex items-center gap-3');
        const radio=el('input');radio.type='radio';radio.name='storage-disk';radio.value=disk.name;
        radio.addEventListener('change',()=>{storageChoice=disk;validate()});
        label.append(radio,el('span','',disk.name+' · '+bytes(disk.sizeBytes)+(disk.model?' · '+disk.model:'')));
        list.appendChild(label);
      }
    });
    const validate=()=>{$('storage-format').disabled=!(storageChoice&&$('storage-confirm').value.trim()===storageChoice.name)};
    $('storage-confirm').addEventListener('input',validate);
    $('storage-cancel').addEventListener('click',()=>closeDialog('storage-dialog'));
    $('storage-format').addEventListener('click',async()=>{
      if(!storageChoice)return;
      $('storage-format').disabled=true;text('storage-candidates','');$('storage-candidates').appendChild(el('p','','Setting up '+storageChoice.name+'… this can take a minute.'));
      const result=await act('/api/storage/setup',{device:storageChoice.name,confirm:$('storage-confirm').value.trim()},'Storage setup failed');
      closeDialog('storage-dialog');
      if(result)message('Storage is ready. Your changes are now saved to '+storageChoice.name+'.');
      refreshStatus();
    });
  }

  // ---------- time zone ----------
  async function setupTimezones(){
    const select=$('setting-timezone');
    try{
      const data=await req('GET','/api/timezones');
      select.replaceChildren(...data.timezones.map(zone=>{const o=el('option','',zone.replace(/_/g,' '));o.value=zone;return o}));
      select.value=data.current;
    }catch{select.replaceChildren(el('option','','Unavailable'));select.disabled=true}
    select.addEventListener('change',async()=>{if(await act('/api/timezone',{timezone:select.value},'The time zone could not be changed'))message('Time zone set to '+select.value.replace(/_/g,' ')+'.');refreshStatus()});
  }

  // ---------- weather (Open-Meteo through novad, with a clearly labelled sample fallback) ----------
  async function refreshWeather(){
    if(mode!=='device')return;
    let w;
    try{w=await req('GET','/api/weather')}catch{return}
    if(w.live){
      text('weather-place',w.place+' · live');
      text('weather-temperature',Math.round(w.temperature)+'°');
      text('weather-condition',w.condition);
      text('weather-note','Weather data by Open-Meteo.com · updated '+new Date(w.updated).toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit',hour12:!window.novaApp?.state.clock24})+'.');
    }else{
      text('weather-place','Sample weather · Dubai');text('weather-temperature','29°');text('weather-condition','Partly cloudy');
      text('weather-note','Sample conditions, not a live forecast. '+(w.reason||'A live forecast appears when this device is online.'));
    }
    if(w.placeName&&document.activeElement!==$('setting-weather-place'))$('setting-weather-place').value=w.placeName;
  }
  function setupWeather(){
    $('weather-place-form').addEventListener('submit',async e=>{
      e.preventDefault();const query=$('setting-weather-place').value.trim();if(!query)return;
      const result=await act('/api/weather/place',{query},'Could not set the location');
      if(result){message('Forecast location set to '+result.place+'.');refreshWeather()}
    });
    refreshWeather();weatherTimer=setInterval(refreshWeather,15*60*1000);
  }

  // ---------- photos from ~/Pictures ----------
  async function loadPhotos(){
    let photos;
    try{photos=(await req('GET','/api/photos')).photos}catch{return}
    const grid=document.querySelector('#photos .grid');
    grid.querySelectorAll('[data-user-photo]').forEach(node=>node.remove());
    for(const photo of photos){
      const b=el('button','photo-button rounded-2xl overflow-hidden');b.type='button';b.dataset.userPhoto='';b.setAttribute('aria-label','View '+photo.name);
      const img=el('img','photo-thumb w-full');img.loading='lazy';img.src=photo.url;img.alt=photo.name;
      b.appendChild(img);b.addEventListener('click',()=>window.novaApp.viewPhoto(b));grid.appendChild(b);
    }
  }

  // ---------- text files on USB drives ----------
  async function renderUsbFiles(){
    let section=$('usb-files');
    const mounted=(status?.usb||[]).filter(d=>d.mounted);
    if(!mounted.length){section?.remove();return}
    if(!section){section=el('div','mt-8');section.id='usb-files';$('file-list').after(section)}
    const items=[];
    for(const drive of mounted){
      let files=[];try{files=(await req('GET','/api/usb/files?id='+encodeURIComponent(drive.id))).files}catch{}
      for(const f of files){
        const row=el('div','item flex justify-between items-center gap-3');
        row.append(el('p','break-words min-w-0',(drive.label||drive.name)+' / '+f.path),button('Import','small-action shrink-0',async()=>{
          if(await act('/api/usb/import',{id:drive.id,path:f.path},'Could not import the file')){message(f.path+' was copied to Documents.');window.dataSdk.reload?.()}
        }));
        items.push(row);
      }
    }
    section.replaceChildren(el('h3','font-semibold mb-3','On USB drives'),...(items.length?items:[el('p','text-white/60','No text files found on the connected drives.')]));
  }

  // ---------- preview / unauthorised labelling ----------
  function labelPreview(){
    text('quick-disclaimer','Preview: the Wi-Fi and Bluetooth tiles only change icons here; they do not switch real radios.');
    $('wifi-icon').setAttribute('aria-label','Wi-Fi indicator (preview only)');
    const kept=window.dataSdk.browserStorageWorks();
    text('settings-about',BRAND+' '+(document.documentElement.dataset.version||'')+' · running as a preview in a web browser, so system controls are unavailable. '+(kept?'Notes, tasks, files and settings are kept in this browser’s storage.':'This browser blocks storage, so changes won’t be saved.'));
    text('weather-note','Sample conditions, not a live forecast. Live weather needs the '+BRAND+' device.');
  }
  function labelDevice(){
    const s=novaApi.session||{};
    text('wifi-label','Wi-Fi');text('wifi-note','Turn the Wi-Fi radio on or off.');show('wifi-networks-open',true);
    text('bluetooth-label','Bluetooth');text('bluetooth-note','Turn the Bluetooth radio on or off.');show('bluetooth-devices-open',true);
    text('quiet-note','Mute all sound on this device and stop the built-in melody from starting.');
    if(s.features?.documents)text('files-note','Text files in your Documents folder.');
    text('settings-about',BRAND+' '+(s.version||'')+(s.base?' · '+s.base:'')+(s.dev?' · development mode: power actions are simulated':''));
    show('system-settings',true);
  }

  window.novaSystem={
    async load(){
      await novaApi.ready;mode=novaApi.mode;
      await loadSettings();
      if(mode==='device')await refreshStatus();
    },
    initialState(){
      const state={...settings};
      if(mode==='device'&&status){
        if(status.wifi)state.wifi=Boolean(status.wifi.enabled);
        if(status.bluetooth)state.bluetooth=Boolean(status.bluetooth.enabled);
      }
      return state;
    },
    stateChanged(state){
      if(!last){last={...state};if(mode==='device')applyToDevice(state,new Set(['quiet','brightness']));return}
      const changed=new Set(PREVIEW_KEYS.filter(key=>state[key]!==last[key]));
      last={...state};
      if(!changed.size)return;
      applyToDevice(state,changed);
      if([...changed].some(key=>mode==='device'?UI_KEYS.includes(key):true))scheduleSave(state);
    },
    hardwareBrightness:()=>mode==='device'&&Boolean(status?.backlight),
    storageName:()=>mode==='preview'?'browser storage':BRAND+' storage',
    openWebsite(url){
      if(mode==='device'){act('/api/browser/open',{url},'The website could not be opened')}
      else window.open(url,'_blank','noopener,noreferrer');
    },
    closeTopDialog,closePanels,
    attach(){
      attached=true;
      document.documentElement.dataset.mode=mode;
      document.querySelectorAll('[data-close-panel]').forEach(b=>b.addEventListener('click',closePanels));
      setupPower();
      if(mode==='device'){
        labelDevice();setupNetworks();setupBluetooth();setupStorage();setupTimezones();setupWeather();loadPhotos();render();
        $('volume').addEventListener('input',e=>{
          volumeDragging=true;clearTimeout(volumeTimer);
          volumeTimer=setTimeout(async()=>{await act('/api/audio',{volume:Number(e.target.value)},'Volume could not be changed');volumeDragging=false},120);
        });
        $('music-play').addEventListener('click',()=>{if(status&&!status.audio)message('No sound device was found on this computer.')});
        pollTimer=setInterval(()=>{if(!document.hidden)refreshStatus()},5000);
        document.addEventListener('visibilitychange',()=>{if(!document.hidden){refreshStatus();refreshWeather();loadPhotos()}});
      }else if(mode==='preview')labelPreview();
      else text('settings-about','This window is not signed in to '+BRAND+', so nothing can be loaded or saved here.');
    },
  };
})();
