// NovaOS: start-up order. Settings and device state are loaded before the app script runs, so the very
// first render already shows the saved wallpaper mood, accent, text scale and the real radio states.
(function(){
  'use strict';
  const wallpaper=document.querySelector('.wallpaper');
  const noWallpaper=()=>document.getElementById('os').classList.add('no-wallpaper');
  if(wallpaper){wallpaper.addEventListener('error',noWallpaper);if(wallpaper.complete&&!wallpaper.naturalWidth&&wallpaper.getAttribute('src'))noWallpaper()}
  window.novaSystem.load().catch(error=>console.error('NovaOS: system state unavailable',error)).finally(()=>{
    const script=document.createElement('script');
    script.src='js/app.js';
    script.onload=()=>{window.novaSystem.attach();document.documentElement.dataset.ready='true'};
    document.body.appendChild(script);
  });
})();
