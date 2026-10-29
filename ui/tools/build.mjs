#!/usr/bin/env node
// Builds the offline NovaOS shell UI into ui/dist (or $NOVA_UI_OUT).
//   node tools/build.mjs                 build
//   node tools/build.mjs --update-lock   rewrite vendor.lock.json after a deliberate dependency bump
// Inputs: ui/src (markup/CSS/JS derived from ui/original/ui.html), nova.config.json (branding),
// node_modules (pinned in package-lock.json), ui/assets (fetched photos or generated fallbacks).
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const UI=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const REPO=path.resolve(UI,'..');
const OUT=path.resolve(process.env.NOVA_UI_OUT||path.join(UI,'dist'));
const UPDATE_LOCK=process.argv.includes('--update-lock');
const log=(...a)=>console.log('[ui]',...a);
const fail=msg=>{console.error('[ui] ERROR:',msg);process.exit(1)};
const sha256=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const rel=file=>path.relative(UI,file).split(path.sep).join('/');

const config=JSON.parse(fs.readFileSync(path.join(REPO,'nova.config.json'),'utf8'));
for(const key of ['name','tagline','version','search_url'])if(typeof config[key]!=='string'||!config[key])fail(`nova.config.json: "${key}" is required`);
for(const key of ['mint','peach','blue'])if(!/^#[0-9a-f]{6}$/i.test(config.accents?.[key]||''))fail(`nova.config.json: accents.${key} must be a #rrggbb colour`);

// ---------- vendored files: exact versions + checksums ----------
const FONT_FACES=[
  // [package, family, weights, subsets] — mirrors the original Google Fonts request:
  // DM+Sans:wght@400;500;600;700 & Space+Mono:wght@400;700 (normal style only)
  ['@fontsource/dm-sans','DM Sans',[400,500,600,700],['latin-ext','latin']],
  ['@fontsource/space-mono','Space Mono',[400,700],['vietnamese','latin-ext','latin']],
];
const vendorFiles=[path.join(UI,'node_modules/lucide/dist/umd/lucide.min.js')];
for(const [pkg,,weights,subsets] of FONT_FACES){
  const short=pkg.split('/')[1];
  for(const w of weights)for(const s of subsets)vendorFiles.push(path.join(UI,'node_modules',pkg,'files',`${short}-${s}-${w}-normal.woff2`));
  vendorFiles.push(path.join(UI,'node_modules',pkg,'LICENSE'));
}
const lockPath=path.join(UI,'vendor.lock.json');
const expectedVersions={'lucide':'0.577.0','tailwindcss':'3.4.17','@fontsource/dm-sans':'5.3.0','@fontsource/space-mono':'5.3.0'};
for(const [pkg,version] of Object.entries(expectedVersions)){
  const file=path.join(UI,'node_modules',pkg,'package.json');
  if(!fs.existsSync(file))fail(`${pkg} is not installed. Run: (cd ui && npm ci)`);
  const actual=JSON.parse(fs.readFileSync(file,'utf8')).version;
  if(actual!==version)fail(`${pkg} is ${actual}, expected ${version}. Run: (cd ui && npm ci)`);
}
if(UPDATE_LOCK){
  const files={};for(const f of vendorFiles)files[rel(f)]=sha256(f);
  fs.writeFileSync(lockPath,JSON.stringify({_comment:'sha256 of every vendored file copied into the build. Regenerate only after a deliberate version bump: node tools/build.mjs --update-lock',versions:expectedVersions,files},null,2)+'\n');
  log('vendor.lock.json updated');
}
const lock=JSON.parse(fs.readFileSync(lockPath,'utf8'));
for(const f of vendorFiles){
  const want=lock.files[rel(f)];
  if(!want)fail(`${rel(f)} is not in vendor.lock.json`);
  if(!fs.existsSync(f))fail(`${rel(f)} is missing. Run: (cd ui && npm ci)`);
  if(sha256(f)!==want)fail(`checksum mismatch for ${rel(f)}`);
}
log(`verified ${vendorFiles.length} vendored files against vendor.lock.json`);

// ---------- output tree ----------
fs.rmSync(OUT,{recursive:true,force:true});
for(const dir of ['css','js','fonts','vendor','assets','licenses'])fs.mkdirSync(path.join(OUT,dir),{recursive:true});

// ---------- images ----------
const escapeAttr=s=>s.replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;');
const slots=JSON.parse(fs.readFileSync(path.join(UI,'assets/assets.json'),'utf8'));
const assetAttrs={},credits=[];
for(const [slot,a] of Object.entries(slots)){
  if(slot.startsWith('_'))continue;
  const fetched=path.join(UI,'assets/fetched',a.file);
  let use=null;
  if(fs.existsSync(fetched)){
    const got=sha256(fetched);
    if(a.sha256&&a.sha256!==got)log(`WARNING ${a.file}: checksum ${got} does not match assets.json; using the fallback`);
    else use={src:fetched,name:a.file,alt:a.alt};
  }
  if(!use)use={src:path.join(UI,'assets/fallback',a.fallback),name:a.fallback,alt:a.fallbackAlt};
  fs.copyFileSync(use.src,path.join(OUT,'assets',use.name));
  assetAttrs[slot]=`src="assets/${use.name}" alt="${escapeAttr(use.alt)}"`;
  credits.push({slot,file:use.name,fallback:use.name===a.fallback,page:a.page});
  log(`image ${slot}: ${use.name===a.fallback?'fallback artwork':'fetched photo'} (${use.name})`);
}

// ---------- templated sources ----------
const tokens={
  NAME:config.name,TAGLINE:config.tagline,VERSION:config.version,SEARCH_URL:config.search_url,
  ACCENT_MINT:config.accents.mint,ACCENT_PEACH:config.accents.peach,ACCENT_BLUE:config.accents.blue,
};
function render(text,file){
  const out=text.replace(/%%ASSET:([a-z0-9]+)%%/g,(_,slot)=>assetAttrs[slot]??fail(`${file}: unknown image slot ${slot}`))
    .replace(/%%([A-Z_]+)%%/g,(m,key)=>key in tokens?tokens[key]:fail(`${file}: unknown token ${m}`));
  if(/%%[A-Z_:a-z0-9]+%%/.test(out))fail(`${file}: unresolved token`);
  return out;
}
const SRC=path.join(UI,'src');
for(const file of ['index.html','css/shell.css',...fs.readdirSync(path.join(SRC,'js')).filter(f=>f.endsWith('.js')).map(f=>'js/'+f)]){
  fs.writeFileSync(path.join(OUT,file),render(fs.readFileSync(path.join(SRC,file),'utf8'),file));
}

// ---------- fonts ----------
const faces=[];
for(const [pkg,family,weights,subsets] of FONT_FACES){
  const short=pkg.split('/')[1];
  const ranges=JSON.parse(fs.readFileSync(path.join(UI,'node_modules',pkg,'unicode.json'),'utf8'));
  for(const s of subsets){
    if(!ranges[s])fail(`${pkg}: no unicode range for subset ${s}`);
    for(const w of weights){
      const name=`${short}-${s}-${w}-normal.woff2`;
      fs.copyFileSync(path.join(UI,'node_modules',pkg,'files',name),path.join(OUT,'fonts',name));
      faces.push(`/* ${family} ${w} ${s} — ${pkg}@${expectedVersions[pkg]} */\n@font-face{font-family:'${family}';font-style:normal;font-weight:${w};font-display:swap;src:url(../fonts/${name}) format('woff2');unicode-range:${ranges[s]}}`);
    }
  }
  fs.copyFileSync(path.join(UI,'node_modules',pkg,'LICENSE'),path.join(OUT,'licenses',`OFL-${short}.txt`));
}
fs.writeFileSync(path.join(OUT,'css/fonts.css'),faces.join('\n')+'\n');

// ---------- icons ----------
fs.copyFileSync(path.join(UI,'node_modules/lucide/dist/umd/lucide.min.js'),path.join(OUT,'vendor/lucide.min.js'));
fs.copyFileSync(path.join(UI,'node_modules/lucide/LICENSE'),path.join(OUT,'licenses/lucide-ISC.txt'));
fs.copyFileSync(path.join(UI,'node_modules/tailwindcss/LICENSE'),path.join(OUT,'licenses/tailwindcss-MIT.txt'));

// ---------- Tailwind (compiled with the CLI instead of the play CDN) ----------
const input=path.join(OUT,'css/.tailwind.input.css');
fs.writeFileSync(input,'@tailwind base;\n@tailwind components;\n@tailwind utilities;\n');
execFileSync(process.execPath,[path.join(UI,'node_modules/tailwindcss/lib/cli.js'),'-c',path.join(UI,'tailwind.config.cjs'),'-i',input,'-o',path.join(OUT,'css/tailwind.css'),'--minify'],{stdio:['ignore','inherit','inherit'],env:{...process.env,NOVA_UI_OUT:OUT,BROWSERSLIST_IGNORE_OLD_DATA:'1'}});
fs.rmSync(input);

// ---------- credits + manifest ----------
fs.writeFileSync(path.join(OUT,'licenses/IMAGES.txt'),credits.map(c=>c.fallback
  ?`${c.slot}: ${c.file} — original ${config.name} artwork, CC0 1.0.`
  :`${c.slot}: ${c.file} — photo from Pexels (${c.page}), used under the Pexels License (https://www.pexels.com/license/).`).join('\n')+'\n');
const manifest={name:config.name,version:config.version,files:{}};
(function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  const full=path.join(dir,entry.name);
  if(entry.isDirectory())walk(full);else manifest.files[path.relative(OUT,full).split(path.sep).join('/')]=sha256(full);
}})(OUT);
fs.writeFileSync(path.join(OUT,'build-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
log(`built ${Object.keys(manifest.files).length} files into ${OUT}`);
