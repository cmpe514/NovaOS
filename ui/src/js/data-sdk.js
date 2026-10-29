// NovaOS: local implementation of the hosted `window.dataSdk` the original app was written against.
// The contract is kept exactly, so the app code does not change:
//   init(handler)            -> {isOk}; then handler.onDataChanged(allRecords) after every change
//   create/update/delete(r)  -> {isOk}   (plus an `error` string when isOk is false)
//   record = {__backendId, app_type: 'note'|'task'|'file', title, content, completed, created_at}
// On the appliance records live in novad (atomic JSON on the NOVA-DATA disk, or RAM when there is none).
// In a plain browser preview they live in localStorage, or in memory if storage is blocked.
(function(){
  'use strict';
  const TYPES=['note','task','file'];
  let handler=null,records=[],backend=null;

  function newId(){
    if(window.crypto?.randomUUID)return crypto.randomUUID();
    return Date.now().toString(36)+'-'+Math.random().toString(36).slice(2,12);
  }
  function clean(record){
    if(!record||!TYPES.includes(record.app_type))throw new Error('Unknown record type.');
    if(typeof record.title!=='string'||!record.title.trim())throw new Error('A title is required.');
    return {
      __backendId:record.__backendId===undefined?undefined:String(record.__backendId),
      app_type:record.app_type,
      title:record.title,
      content:typeof record.content==='string'?record.content:'',
      completed:Boolean(record.completed),
      created_at:typeof record.created_at==='string'&&record.created_at?record.created_at:new Date().toISOString(),
    };
  }

  const deviceBackend={
    kind:'device',
    async list(){return (await novaApi.request('GET','/api/records')).records},
    async create(record){await novaApi.request('POST','/api/records',record)},
    async update(record){await novaApi.request('PUT','/api/records/'+encodeURIComponent(record.__backendId),record)},
    async remove(record){await novaApi.request('DELETE','/api/records/'+encodeURIComponent(record.__backendId))},
  };

  const STORAGE_KEY='nova.records.v1';
  let memory=[],storageWorks=true;
  function readStore(){
    try{const raw=localStorage.getItem(STORAGE_KEY);return raw?JSON.parse(raw):[]}
    catch{storageWorks=false;return memory}
  }
  function writeStore(list){
    memory=list;
    try{localStorage.setItem(STORAGE_KEY,JSON.stringify(list))}
    catch(error){storageWorks=false;throw new Error('This browser refused to store the change ('+error.name+').')}
  }
  const browserBackend={
    kind:'browser',
    persistent:()=>storageWorks,
    async list(){return readStore()},
    async create(record){const list=readStore();list.push({...record,__backendId:newId()});writeStore(list)},
    async update(record){
      const list=readStore(),at=list.findIndex(r=>String(r.__backendId)===record.__backendId);
      if(at<0)throw new Error('That item no longer exists.');
      list[at]={...record,created_at:list[at].created_at};writeStore(list);
    },
    async remove(record){
      const list=readStore(),next=list.filter(r=>String(r.__backendId)!==record.__backendId);
      if(next.length===list.length)throw new Error('That item no longer exists.');
      writeStore(next);
    },
  };

  async function refresh(){
    records=(await backend.list()).map(r=>({...r}));
    if(handler)try{handler.onDataChanged(records.map(r=>({...r})))}catch(error){console.error(error)}
  }
  async function mutate(action,record,needsId){
    try{
      if(!backend)throw new Error('Storage is not ready yet.');
      const value=clean(record);
      if(needsId&&!value.__backendId)throw new Error('Missing record id.');
      if(!needsId)delete value.__backendId;
      await backend[action](value);
      await refresh();
      return {isOk:true};
    }catch(error){
      // Re-sync so the UI never shows a change that did not happen.
      try{await refresh()}catch{}
      return {isOk:false,error:error.message};
    }
  }

  window.dataSdk={
    async init(nextHandler){
      handler=nextHandler;
      await novaApi.ready;
      if(novaApi.mode==='unauthorised')return {isOk:false,error:'This window is not signed in to '+document.title.split(' — ')[0]+'.'};
      backend=novaApi.mode==='device'?deviceBackend:browserBackend;
      try{await refresh();return {isOk:true}}
      catch(error){return {isOk:false,error:error.message}}
    },
    create:record=>mutate('create',record,false),
    update:record=>mutate('update',record,true),
    delete:record=>mutate('remove',record,true),
    // Not part of the original contract: lets Settings tell the truth about where data goes.
    reload:async()=>{try{await refresh()}catch{}},
    backendKind:()=>backend?.kind||null,
    browserStorageWorks:()=>storageWorks,
  };
})();
