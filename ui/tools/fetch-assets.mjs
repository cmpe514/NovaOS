#!/usr/bin/env node
// Downloads the original wallpaper and photos (Pexels) into ui/assets/fetched/ and checks their sha256
// against ui/assets/assets.json. Network failures are not fatal: the build then uses the CC0 fallback art.
//   node tools/fetch-assets.mjs            fetch + verify (an empty sha256 is filled in on first fetch: TOFU)
//   node tools/fetch-assets.mjs --strict   fail instead of warning on download errors
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const UI=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const manifestPath=path.join(UI,'assets/assets.json');
const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
const dir=path.join(UI,'assets/fetched');
const strict=process.argv.includes('--strict');
fs.mkdirSync(dir,{recursive:true});
let recorded=false,problems=0;

for(const [slot,a] of Object.entries(manifest)){
  if(slot.startsWith('_'))continue;
  const target=path.join(dir,a.file);
  if(fs.existsSync(target)&&a.sha256&&createHash('sha256').update(fs.readFileSync(target)).digest('hex')===a.sha256){console.log(`[assets] ${slot}: already present, checksum OK`);continue}
  try{
    console.log(`[assets] ${slot}: downloading ${a.url}`);
    const res=await fetch(a.url,{headers:{'User-Agent':'NovaOS-build (fetch-assets)'}});
    if(!res.ok)throw new Error(`HTTP ${res.status}`);
    const body=Buffer.from(await res.arrayBuffer());
    if(body.length<1024||!/^image\//.test(res.headers.get('content-type')||''))throw new Error(`unexpected response (${res.headers.get('content-type')}, ${body.length} bytes)`);
    const sum=createHash('sha256').update(body).digest('hex');
    if(a.sha256&&a.sha256!==sum)throw new Error(`checksum mismatch: expected ${a.sha256}, got ${sum}. The file changed upstream; inspect it and update assets.json deliberately.`);
    if(!a.sha256){a.sha256=sum;recorded=true;console.log(`[assets] ${slot}: recorded sha256 ${sum} (first fetch — review and commit ui/assets/assets.json)`)}
    fs.writeFileSync(target+'.part',body);fs.renameSync(target+'.part',target);
    console.log(`[assets] ${slot}: OK (${body.length} bytes)`);
  }catch(error){
    problems++;fs.rmSync(target,{force:true});
    console.warn(`[assets] ${slot}: ${error.message} — the build will use the fallback artwork (${a.fallback}).`);
  }
}
if(recorded)fs.writeFileSync(manifestPath,JSON.stringify(manifest,null,2)+'\n');
if(problems&&strict)process.exit(1);
