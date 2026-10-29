// Device-project helpers: sign the browser context in exactly like the session launcher does (one-time
// code from the runtime directory, exchanged at /auth for the session cookie).
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {expect} from '@playwright/test';

const RUN=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../.novad/run');

export async function signIn(page){
  for(let attempt=0;attempt<20;attempt++){
    const code=fs.readFileSync(path.join(RUN,'login-code'),'utf8').trim();
    const res=await page.goto('/auth?code='+code);
    if(res&&res.ok()&&new URL(page.url()).pathname==='/')break;
    await page.waitForTimeout(100); // another worker used the code first; a new one has been written
  }
  await page.locator('html[data-ready="true"]').waitFor();
  await expect(page.locator('html')).toHaveAttribute('data-mode','device');
}
export async function devState(page){return page.evaluate(()=>fetch('/api/dev/state',{headers:{'X-Nova-Request':'1'}}).then(r=>r.json()))}
export async function status(page){return page.evaluate(()=>fetch('/api/system/status',{headers:{'X-Nova-Request':'1'}}).then(r=>r.json()))}
export const NOVAD_STATE=path.resolve(RUN,'..');
