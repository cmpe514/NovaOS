// Visual parity: the offline build vs. a reference render of the untouched original markup + CSS, at the
// three viewport sizes from the brief. Both pages render in the same browser in the same run, so the
// "baselines" always come from the original. Only the status-bar icons are masked: the build shows real
// device indicators (none in a preview) where the original showed fixed illustrations.
// NOVA_REFERENCE_CDN=1 renders the reference with the real Tailwind play CDN / Google Fonts / jsDelivr
// (internet required); by default the reference gets the same vendored files, which isolates markup/CSS changes.
import fs from 'node:fs';
import path from 'node:path';
import {PNG} from 'pngjs';
import pixelmatch from 'pixelmatch';
import {test,expect,openShell,openApp,FIXED_TIME,UI_DIST} from './helpers.js';
import {buildReference} from '../reference/build-reference.mjs';

const REF=buildReference();
const VIEWPORTS=[[1440,900],[820,1180],[390,844]];
const SCREENS=['home','drawer','shade','notes','calculator','calendar','settings','photos'];
const MAX_DIFF_RATIO=0.002; // 0.2% of pixels: allows sub-pixel text antialiasing only
const viaCdn=process.env.NOVA_REFERENCE_CDN==='1';
const ORIGINAL_COPY={
  'quick-disclaimer':'Connectivity indicators are illustrative; these controls do not change your device settings.',
  'wifi-note':'Change the simulated status icon.',
  'bluetooth-note':'Change the quick-settings tile state.',
  'settings-about':'NovaOS is an interactive browser concept, not a device operating system.',
};
const PEXELS={'6985132':'wallpaper','30052971':'photo1','30052973':'photo2','40131643':'photo3'};

async function routeReference(page,baseURL){
  const asset=slot=>fs.readdirSync(path.join(UI_DIST,'assets')).find(f=>f.startsWith(slot+'.'));
  await page.route('**/__reference/**',route=>route.fulfill({path:path.join(REF,path.basename(new URL(route.request().url()).pathname)||'index.html')}));
  if(viaCdn)return;
  await page.route('https://fonts.googleapis.com/**',route=>route.fulfill({contentType:'text/css',body:fs.readFileSync(path.join(UI_DIST,'css/fonts.css'),'utf8').replaceAll('../fonts/',baseURL+'/fonts/')}));
  await page.route('https://cdn.jsdelivr.net/npm/lucide@0.577.0/**',route=>route.fulfill({path:path.join(UI_DIST,'vendor/lucide.min.js'),contentType:'text/javascript'}));
  await page.route('https://cdn.tailwindcss.com/**',route=>route.fulfill({contentType:'text/javascript',body:`(()=>{const l=document.createElement('link');l.rel='stylesheet';l.href='${baseURL}/css/tailwind.css';document.head.appendChild(l)})();`}));
  await page.route('https://images.pexels.com/**',route=>{const id=new URL(route.request().url()).pathname.split('/')[2];return route.fulfill({path:path.join(UI_DIST,'assets',asset(PEXELS[id]))})});
}
async function prepare(page){
  await page.addStyleTag({content:'.status-icons{visibility:hidden!important} *{caret-color:transparent!important}'});
  await page.evaluate(()=>document.fonts.ready);
  await page.waitForFunction(()=>[...document.images].every(i=>!i.checkVisibility()||(i.complete&&i.naturalWidth>0)));
}
async function go(page,screen){
  if(screen==='home')return;
  if(screen==='drawer'||screen==='shade'){await page.locator(`.bottom-dock [data-open="${screen}"]`).click();return}
  await page.locator(`#home .shortcut[data-open="${screen}"]`).click();
  await page.waitForTimeout(50);
}

for(const [width,height] of VIEWPORTS){
  test(`matches the original at ${width}x${height}`,async({browser,baseURL},testInfo)=>{
    const shots={};
    for(const which of ['reference','build']){
      const context=await browser.newContext({viewport:{width,height},locale:'en-US',timezoneId:'UTC',reducedMotion:'reduce'});
      const page=await context.newPage();
      if(which==='reference'){
        await routeReference(page,baseURL);
        await page.clock.setFixedTime(FIXED_TIME);
        await page.goto('/__reference/index.html');
        await page.locator('#home-time:not(:empty)').waitFor();
      }else{
        await page.route(url=>!/^http:\/\/127\.0\.0\.1/.test(url.href),route=>route.abort());
        await openShell(page);
        // Copy the brief requires to change (untrue disclaimers) is checked by parity.spec.js; put the
        // original strings back here so everything else can be compared pixel for pixel.
        await page.evaluate(original=>{for(const [id,text] of Object.entries(original))document.getElementById(id).textContent=text},ORIGINAL_COPY);
      }
      // The original shows a "Loading saved …" line until its SDK answers; both pages have data loaded by now.
      shots[which]={};
      for(const screen of SCREENS){
        await page.locator('#home-button').click();
        await page.evaluate(()=>{document.querySelectorAll('.overlay').forEach(o=>o.hidden=true)});
        await go(page,screen);
        await prepare(page);
        shots[which][screen]=PNG.sync.read(await page.screenshot({fullPage:false}));
      }
      await context.close();
    }
    const failures=[];
    for(const screen of SCREENS){
      const a=shots.reference[screen],b=shots.build[screen];
      const diff=new PNG({width:a.width,height:a.height});
      const count=pixelmatch(a.data,b.data,diff.data,a.width,a.height,{threshold:0.1});
      const ratio=count/(a.width*a.height);
      if(ratio>MAX_DIFF_RATIO){
        failures.push(`${screen}: ${(ratio*100).toFixed(3)}% of pixels differ`);
        for(const [name,png] of [['reference',a],['build',b],['diff',diff]])await testInfo.attach(`${screen}-${name}.png`,{body:PNG.sync.write(png),contentType:'image/png'});
      }
    }
    expect(failures,failures.join('\n')).toEqual([]);
  });
}
