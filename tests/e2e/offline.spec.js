// Section 3 of the brief: the build is fully offline and free of Canva platform leftovers.
import fs from 'node:fs';
import path from 'node:path';
import {test,expect,openShell,openApp,UI_DIST} from './helpers.js';

const files=[];(function walk(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const f=path.join(dir,e.name);if(e.isDirectory())walk(f);else files.push(f)}})(UI_DIST);
const textFiles=files.filter(f=>/\.(html|css|js|json)$/.test(f)&&!f.endsWith('lucide.min.js'));

test('visiting every app makes zero requests to non-localhost hosts',async({page,external})=>{
  await openShell(page);
  for(const id of ['notes','tasks','calculator','clock','weather','music','photos','browser','files','calendar','settings'])await openApp(page,id);
  await page.locator('.bottom-dock [data-open="drawer"]').click();
  await page.locator('.bottom-dock [data-open="shade"]').click();
  expect(external).toEqual([]); // also asserted for every test by the fixture
});

test('no Canva leftovers, CDN references or remote URLs in the build',()=>{
  for(const file of textFiles){
    const text=fs.readFileSync(file,'utf8'),name=path.relative(UI_DIST,file);
    expect(text,name).not.toMatch(/canva(?!s)/i); // 'canvas' (CSS preflight) is fine
    expect(text,name).not.toMatch(/_sdk\//);
    expect(text,name).not.toMatch(/data-template-id/);
    expect(text,name).not.toMatch(/__codeletBootstrap/);
    expect(text,name).not.toMatch(/--vh/);
    expect(text,name).not.toMatch(/fonts\.googleapis|fonts\.gstatic|cdn\.jsdelivr|cdn\.tailwindcss|unpkg\.com|images\.pexels/);
    expect(text,name).not.toMatch(/%%[A-Z_]+%%/);
  }
});

test('every vendored file is listed with a checksum in the build manifest',()=>{
  const manifest=JSON.parse(fs.readFileSync(path.join(UI_DIST,'build-manifest.json'),'utf8'));
  for(const f of files){const rel=path.relative(UI_DIST,f).split(path.sep).join('/');if(rel!=='build-manifest.json')expect(manifest.files[rel],rel).toMatch(/^[0-9a-f]{64}$/)}
  for(const lic of ['licenses/OFL-dm-sans.txt','licenses/OFL-space-mono.txt','licenses/lucide-ISC.txt','licenses/tailwindcss-MIT.txt'])expect(manifest.files[lic],lic).toBeTruthy();
});

test('fonts are the vendored DM Sans and Space Mono',async({page})=>{
  await openShell(page);
  await page.evaluate(()=>document.fonts.ready);
  const loaded=await page.evaluate(()=>[...document.fonts].filter(f=>f.status==='loaded').map(f=>f.family.replace(/"/g,'')+' '+f.weight));
  for(const want of ['DM Sans 400','DM Sans 600','DM Sans 700','Space Mono 700'])expect(loaded).toContain(want);
});

test('JS-added Tailwind classes are compiled',()=>{
  const css=fs.readFileSync(path.join(UI_DIST,'css/tailwind.css'),'utf8');
  for(const cls of ['line-through','opacity-50','rounded-xl','py-3','text-sm','font-bold','sm\\:grid-cols-3','text-white\\/60'])expect(css,cls).toContain('.'+cls);
});

test('the three Delete buttons have visible text and accessible names',()=>{
  const html=fs.readFileSync(path.join(UI_DIST,'index.html'),'utf8');
  for(const type of ['note','task','file'])expect(html).toContain(`aria-label="Delete ${type}">Delete</button>`);
  expect(html).not.toMatch(/<button(?![^>]*aria-label)[^>]*>\s*<\/button>/); // no unnamed button left relying on injected text
});
