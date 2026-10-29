// NovaOS: thin client for novad, the local daemon that serves this page.
// Every request carries the X-Nova-Request header, which forces a CORS preflight for any cross-origin
// caller; novad answers preflights with no CORS headers, so pages from other origins cannot call the API.
(function(){
  'use strict';
  const api={
    // 'device'       novad is serving this page and we hold a session (normal case on the appliance)
    // 'unauthorised' novad is serving this page but this browser profile has no session cookie
    // 'preview'      no novad (static hosting, file://): data stays in this browser
    mode:'preview',
    session:null,
  };
  async function request(method,path,body){
    const headers={'X-Nova-Request':'1'};
    if(body!==undefined)headers['Content-Type']='application/json';
    const res=await fetch(path,{method,headers,credentials:'same-origin',cache:'no-store',body:body===undefined?undefined:JSON.stringify(body)});
    let data=null;
    try{data=await res.json()}catch{}
    if(!res.ok){const error=new Error(data?.error||('Request failed ('+res.status+')'));error.status=res.status;throw error}
    return data;
  }
  api.request=request;
  api.ready=(async()=>{
    if(location.protocol==='file:')return api;
    try{api.session=await request('GET','/api/session');api.mode='device'}
    catch(error){api.mode=error.status===401?'unauthorised':'preview'}
    return api;
  })();
  window.novaApi=api;
})();
