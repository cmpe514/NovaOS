// Shared fixtures: every page records (and blocks) requests to any host other than 127.0.0.1, so a test
// fails if the UI tries to reach the internet; window.open and WebAudio are instrumented.
import {test as base,expect} from '@playwright/test';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE=path.dirname(fileURLToPath(import.meta.url));
export const UI_DIST=path.resolve(HERE,'../../ui/dist');
export const FIXED_TIME=new Date('2026-03-14T09:41:00Z'); // a Saturday; fixed so screenshots and the calendar are stable

const isLocal=url=>/^(http:\/\/(127\.0\.0\.1|localhost)(:\d+)?\/|data:|blob:|about:)/.test(url);

export const test=base.extend({
  external:async({},use)=>{await use([])},
  page:async({page,external},use)=>{
    await page.route(url=>!isLocal(url.href),route=>{external.push(route.request().url());return route.abort('blockedbyclient')});
    await page.addInitScript(()=>{
      window.__opened=[];window.open=(...args)=>{window.__opened.push(args);return null};
      window.__oscillators=0;
      const Ctx=window.AudioContext||window.webkitAudioContext;
      if(Ctx){const original=Ctx.prototype.createOscillator;Ctx.prototype.createOscillator=function(){window.__oscillators++;return original.call(this)}}
    });
    await use(page);
    expect(external,'the UI must not contact any host except 127.0.0.1').toEqual([]);
  },
});
export {expect};

export async function openShell(page,{time=FIXED_TIME}={}){
  if(time)await page.clock.setFixedTime(time);
  await page.goto('/');
  await page.locator('html[data-ready="true"]').waitFor();
  await expect(page.locator('#note-loading')).toBeHidden();
}
export async function openApp(page,id){
  await page.locator('#home-button').click();
  await page.locator(`#home .shortcut[data-open="${id}"]`).click();
  await expect(page.locator('#'+id)).toBeVisible();
}
export const readDist=file=>fs.readFileSync(path.join(UI_DIST,file),'utf8');
