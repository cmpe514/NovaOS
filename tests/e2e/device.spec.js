// The UI served by novad (--dev, fake system backend): data and settings go through the real API, every
// system control talks to the (fake) device, and pages from other origins cannot use the API.
import fs from 'node:fs';
import path from 'node:path';
import {test,expect,openApp} from './helpers.js';
import {signIn,devState,status,NOVAD_STATE} from './device-helpers.js';

test.describe.configure({mode:'serial'}); // one novad instance, shared state

test('notes and settings are stored by novad and survive a reload',async({page})=>{
  await signIn(page);
  await openApp(page,'notes');
  const title='Device note '+Date.now();
  await page.fill('#note-title',title);
  await page.locator('#note-form [type=submit]').click();
  await expect(page.locator('#note-list > article').first()).toContainText(title);
  const stored=JSON.parse(fs.readFileSync(path.join(NOVAD_STATE,'data/records.json'),'utf8'));
  expect(stored.records.map(r=>r.title)).toContain(title);
  await openApp(page,'settings');
  await page.selectOption('#setting-accent','peach');
  await page.waitForTimeout(600);
  await page.reload();
  await page.locator('html[data-ready="true"]').waitFor();
  expect(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim())).toBe('#ffc4a6');
  await openApp(page,'notes');
  await expect(page.locator('#note-list')).toContainText(title);
  expect(JSON.parse(fs.readFileSync(path.join(NOVAD_STATE,'data/settings.json'),'utf8')).ui.accent).toBe('peach');
});

test('Files app writes real files into ~/Documents',async({page})=>{
  await signIn(page);
  await openApp(page,'files');
  await expect(page.locator('#files-note')).toHaveText('Text files in your Documents folder.');
  await page.fill('#file-name','todo.txt');
  await page.fill('#file-content','buy bread');
  await page.locator('#file-form [type=submit]').click();
  await expect(page.locator('#file-list')).toContainText('todo.txt');
  expect(fs.readFileSync(path.join(NOVAD_STATE,'home/Documents/todo.txt'),'utf8')).toBe('buy bread');
  await page.fill('#file-name','../escape.txt');
  await page.locator('#file-form [type=submit]').click();
  await expect(page.locator('#toast')).toContainText('dot');
  expect(fs.existsSync(path.join(NOVAD_STATE,'home/escape.txt'))).toBe(false);
  await page.locator('#file-list > article',{hasText:'todo.txt'}).getByRole('button',{name:'Delete file'}).click();
  await expect(page.locator('#file-list > article',{hasText:'todo.txt'})).toHaveCount(0);
  expect(fs.existsSync(path.join(NOVAD_STATE,'home/Documents/todo.txt'))).toBe(false);
});

test('status bar shows the real battery and network; Wi-Fi tile switches the radio',async({page})=>{
  await signIn(page);
  await expect(page.locator('#battery-icon')).toBeVisible();
  await expect(page.locator('#battery-icon')).toHaveAttribute('aria-label','Battery 76%');
  await expect(page.locator('#wifi-icon')).toHaveAttribute('aria-label','Wi-Fi connected to Nova Home');
  await expect(page.locator('#signal-icon')).toBeVisible();
  await page.locator('.bottom-dock [data-open="shade"]').click();
  await expect(page.locator('#quick-disclaimer')).toHaveText('Wi-Fi and Bluetooth tiles switch this device’s radios.');
  await page.locator('[data-switch="wifi"]').click();
  await expect.poll(async()=>(await status(page)).wifi.enabled).toBe(false);
  await expect(page.locator('#wifi-icon')).toHaveAttribute('aria-label','Wi-Fi off');
  await expect(page.locator('#signal-icon')).toBeHidden();
  await page.locator('[data-switch="wifi"]').click();
  await expect.poll(async()=>(await status(page)).wifi.enabled).toBe(true);
});

test('Quiet mutes the device, the volume slider and backlight reach the device',async({page})=>{
  await signIn(page);
  await page.locator('.bottom-dock [data-open="shade"]').click();
  await expect(page.locator('#volume-item')).toBeVisible();
  await page.locator('#volume').fill('30');
  await expect.poll(async()=>(await status(page)).audio.volume).toBe(30);
  await page.locator('[data-switch="quiet"]').click();
  await expect.poll(async()=>(await status(page)).audio.muted).toBe(true);
  await page.locator('[data-switch="quiet"]').click();
  await expect.poll(async()=>(await status(page)).audio.muted).toBe(false);
  await page.locator('#brightness').fill('110');
  await expect.poll(async()=>(await status(page)).backlight.percent).toBe(100);
  await expect(page.locator('.wallpaper')).toHaveCSS('opacity','1'); // the real backlight is used, no double dimming
});

test('power menu asks for confirmation and calls the device',async({page})=>{
  await signIn(page);
  await page.locator('.bottom-dock [data-open="shade"]').click();
  await page.locator('#power-button').click();
  await expect(page.locator('#power-dialog')).toBeVisible();
  await expect(page.locator('#power-cancel')).toBeFocused();
  await expect(page.locator('#power-note')).toContainText('not saved'); // no NOVA-DATA disk yet
  await page.keyboard.press('Escape');
  await expect(page.locator('#power-dialog')).toBeHidden();
  expect((await devState(page)).power).toEqual([]);
  await page.locator('#power-button').click();
  await page.locator('#power-restart').click();
  await expect(page.locator('#toast')).toContainText('simulated');
  expect((await devState(page)).power).toEqual(['reboot']);
});

test('Browser app opens real websites through the system browser',async({page})=>{
  await signIn(page);
  await openApp(page,'browser');
  await page.fill('#browser-address','example.org');
  await page.locator('#browser-form [type=submit]').click();
  await expect.poll(async()=>(await devState(page)).opened).toContain('https://example.org/');
  expect(await page.evaluate(()=>window.__opened.length)).toBe(0); // never window.open on the device
});

test('storage: clear notice without a disk, typed confirmation to set one up',async({page})=>{
  await signIn(page);
  await openApp(page,'settings');
  await expect(page.locator('#storage-title')).toHaveText('Changes won’t be saved');
  await page.locator('#storage-setup-open').click();
  await expect(page.locator('#storage-dialog')).toBeVisible();
  await page.locator('input[name="storage-disk"]').check();
  await expect(page.locator('#storage-format')).toBeDisabled();
  await page.fill('#storage-confirm','vd');
  await expect(page.locator('#storage-format')).toBeDisabled();
  await page.fill('#storage-confirm','vdb');
  await expect(page.locator('#storage-format')).toBeEnabled();
  await page.locator('#storage-format').click();
  await expect(page.locator('#toast')).toContainText('Storage is ready');
  expect((await devState(page)).setup).toEqual(['vdb']);
  await expect(page.locator('#storage-title')).toContainText('Saving to NOVA-DATA');
});

test('a page without the session cookie cannot read or change anything',async({browser,baseURL})=>{
  const context=await browser.newContext({baseURL});
  const page=await context.newPage();
  await page.goto('/');
  await page.locator('html[data-ready="true"]').waitFor();
  await expect(page.locator('html')).toHaveAttribute('data-mode','unauthorised');
  await expect(page.locator('#settings-about')).toContainText('not signed in');
  const res=await page.evaluate(()=>fetch('/api/records',{headers:{'X-Nova-Request':'1'}}).then(r=>r.status));
  expect(res).toBe(401);
  await context.close();
});

test('another origin cannot call the API, even in the signed-in browser profile',async({page,baseURL})=>{
  await signIn(page);
  const before=(await devState(page)).power.length;
  // Serve an "attacker" page on a different origin inside the same browser context (same cookie jar).
  await page.route('http://attacker.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><title>attacker</title>'}));
  await page.goto('http://attacker.test/');
  const results=await page.evaluate(async api=>{
    const out={};
    try{const r=await fetch(api+'/api/records',{credentials:'include',headers:{'X-Nova-Request':'1'}});out.read='status '+r.status}catch(e){out.read='blocked'}
    try{await fetch(api+'/api/power',{method:'POST',mode:'no-cors',credentials:'include',headers:{'Content-Type':'text/plain'},body:'{"action":"poweroff"}'});out.blind='sent'}catch(e){out.blind='blocked'}
    try{const r=await fetch(api+'/api/power',{method:'POST',credentials:'include',headers:{'Content-Type':'application/json','X-Nova-Request':'1'},body:'{"action":"poweroff"}'});out.cors='status '+r.status}catch(e){out.cors='blocked'}
    return out;
  },baseURL);
  expect(results.read).toBe('blocked');  // CORS: the response is never readable
  expect(results.cors).toBe('blocked');  // the preflight fails, the POST is never sent
  await page.goto(baseURL+'/');
  await page.locator('html[data-ready="true"]').waitFor();
  expect((await devState(page)).power.length).toBe(before); // the blind no-cors POST was rejected by novad
});
