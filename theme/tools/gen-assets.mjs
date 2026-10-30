#!/usr/bin/env node
// Build-time only: rasterises the NovaOS theme (fonts, icons, wallpaper) from the same sources the web UI
// uses (DM Sans / Space Mono woff2, Lucide icon nodes, ui/assets/fallback/wallpaper.svg) into blobs that the
// native shell and recovery link in. Chromium is used here as a rasteriser on the build host; nothing of it
// ships in NovaOS. Output (committed, so a normal build does not need Node or Chromium):
//   theme/generated/{assets.h, font_*.bin, icons.bin, wallpaper.bin}
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath, pathToFileURL} from 'node:url';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const OUT=path.join(ROOT,'theme/generated');
const tokens=JSON.parse(fs.readFileSync(path.join(ROOT,'theme/tokens.json'),'utf8'));
const {chromium}=createRequire(path.join(ROOT,'tests/package.json'))('playwright');
const fontFile=f=>'data:font/woff2;base64,'+fs.readFileSync(path.join(ROOT,'ui/dist/fonts',f)).toString('base64');
const ASCII=Array.from({length:95},(_,i)=>String.fromCharCode(32+i)).join('');
// name, family, weight, px, charset
const FONTS=[
  ['status','M',700,12,ASCII],['label','D',600,12,ASCII],['small','D',500,12,ASCII],['body','D',400,15,ASCII],
  ['date','D',500,16,ASCII],['h2','D',600,22,ASCII],['title','D',700,28,ASCII],['big','D',400,40,ASCII],
  ['hero','D',400,132,'0123456789: APM'],
];
const ICON_PX=27;
async function iconSvg(name){
  const alias={'globe-2':'earth'};
  const mod=await import(pathToFileURL(path.join(ROOT,'ui/node_modules/lucide/dist/esm/icons',(alias[name]||name)+'.js')));
  const kids=mod.default.map(([tag,attrs])=>`<${tag} ${Object.entries(attrs).map(([k,v])=>`${k}="${v}"`).join(' ')}/>`).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${kids}</svg>`;
}
const icons=[];for(const a of tokens.apps)icons.push(await iconSvg(a.icon));
for(const n of ['wifi','battery-medium','signal','sliders-horizontal','arrow-left','x','delete'])icons.push(await iconSvg(n));
const wallpaper='data:image/svg+xml;base64,'+fs.readFileSync(path.join(ROOT,'ui/assets/fallback/wallpaper.svg')).toString('base64');

const browser=await chromium.launch();
const page=await browser.newPage();
await page.setContent(`<!doctype html><style>
@font-face{font-family:D;font-weight:400;src:url(${fontFile('dm-sans-latin-400-normal.woff2')})}
@font-face{font-family:D;font-weight:500;src:url(${fontFile('dm-sans-latin-500-normal.woff2')})}
@font-face{font-family:D;font-weight:600;src:url(${fontFile('dm-sans-latin-600-normal.woff2')})}
@font-face{font-family:D;font-weight:700;src:url(${fontFile('dm-sans-latin-700-normal.woff2')})}
@font-face{font-family:M;font-weight:700;src:url(${fontFile('space-mono-latin-700-normal.woff2')})}
</style><body>`);
const res=await page.evaluate(async({FONTS,icons,ICON_PX,wallpaper})=>{
  for(const [, fam, w] of FONTS)await document.fonts.load(`${w} 20px ${fam}`);
  const fonts=[];
  for(const [name,fam,w,px,chars] of FONTS){
    const c=document.createElement('canvas'),x=c.getContext('2d');x.font=`${w} ${px}px ${fam}`;
    const m=x.measureText('Hg|'),asc=Math.ceil(m.fontBoundingBoxAscent),desc=Math.ceil(m.fontBoundingBoxDescent);
    const pad=Math.ceil(px/8)+1,h=asc+desc,glyphs=[];let total=0;
    for(const ch of chars){const adv=x.measureText(ch).width;const cw=Math.ceil(adv)+2*pad;glyphs.push({ch,adv:Math.round(adv*16),x:total,w:cw});total+=cw;}
    c.width=total;c.height=h;x.font=`${w} ${px}px ${fam}`;x.fillStyle='#fff';x.textBaseline='alphabetic';
    for(const g of glyphs)x.fillText(g.ch,g.x+pad,asc);
    const d=x.getImageData(0,0,total,h).data,alpha=new Array(total*h);for(let i=0;i<total*h;i++)alpha[i]=d[i*4+3];
    fonts.push({name,px,asc,h,pad,total,glyphs,alpha});
  }
  const ic=[];
  for(const svg of icons){
    const img=new Image();img.src='data:image/svg+xml;base64,'+btoa(svg);await img.decode();
    const c=document.createElement('canvas');c.width=c.height=ICON_PX;const x=c.getContext('2d');x.drawImage(img,0,0,ICON_PX,ICON_PX);
    const d=x.getImageData(0,0,ICON_PX,ICON_PX).data;const a=[];for(let i=0;i<ICON_PX*ICON_PX;i++)a.push(d[i*4+3]);ic.push(a);
  }
  // Wallpaper: same filter and overlay gradient as the reference's .wallpaper + body background.
  const W=320,H=200,img=new Image();img.src=wallpaper;await img.decode();
  const c=document.createElement('canvas');c.width=W;c.height=H;const x=c.getContext('2d');
  x.filter='saturate(.9) brightness(.48)';const s=Math.max(W/img.naturalWidth,H/img.naturalHeight);
  x.drawImage(img,(W-img.naturalWidth*s)/2,(H-img.naturalHeight*s)/2,img.naturalWidth*s,img.naturalHeight*s);x.filter='none';
  const r=155*Math.PI/180,dx=Math.sin(r),dy=-Math.cos(r),L=Math.abs(W*dx)+Math.abs(H*dy);
  const g=x.createLinearGradient(W/2-dx*L/2,H/2-dy*L/2,W/2+dx*L/2,H/2+dy*L/2);
  g.addColorStop(0,'#061024aa');g.addColorStop(.48,'#06102400');g.addColorStop(1,'#07162bee');x.fillStyle=g;x.fillRect(0,0,W,H);
  const d=x.getImageData(0,0,W,H).data,rgb=[];for(let i=0;i<W*H;i++)rgb.push(d[i*4],d[i*4+1],d[i*4+2]);
  return {fonts,ic,wp:{W,H,rgb}};
},{FONTS,icons,ICON_PX,wallpaper});
await browser.close();

fs.mkdirSync(OUT,{recursive:true});
let h=`/* Generated by theme/tools/gen-assets.mjs from theme/tokens.json. Do not edit. */\n#pragma once\n#include <stdint.h>\n`;
h+=`typedef struct { uint16_t x, w, adv16; } nova_glyph;\ntypedef struct { const char *chars; int px, asc, h, pad, stride; const nova_glyph *g; const uint8_t *alpha; } nova_font;\n`;
for(const f of res.fonts){
  fs.writeFileSync(path.join(OUT,`font_${f.name}.bin`),Buffer.from(f.alpha));
  h+=`static const nova_glyph nova_glyphs_${f.name}[] = {${f.glyphs.map(g=>`{${g.x},${g.w},${g.adv}}`).join(',')}};\n`;
  h+=`#define NOVA_FONT_${f.name.toUpperCase()} { ${JSON.stringify(f.glyphs.map(g=>g.ch).join(''))}, ${f.px}, ${f.asc}, ${f.h}, ${f.pad}, ${f.total}, nova_glyphs_${f.name}, nova_blob_font_${f.name} }\n`;
}
fs.writeFileSync(path.join(OUT,'icons.bin'),Buffer.from(res.ic.flat()));
fs.writeFileSync(path.join(OUT,'wallpaper.bin'),Buffer.from(res.wp.rgb));
h+=`#define NOVA_ICON_PX ${ICON_PX}\n#define NOVA_WALL_W ${res.wp.W}\n#define NOVA_WALL_H ${res.wp.H}\n`;
h+=`enum { ${tokens.apps.map(a=>'ICON_'+a.id.toUpperCase()).join(', ')}, ICON_WIFI, ICON_BATTERY, ICON_SIGNAL, ICON_SLIDERS, ICON_BACK, ICON_CLOSE, ICON_DELETE };\n`;
const hex=s=>'0x'+s.slice(1,7);
h+=`typedef struct { const char *id, *label; uint32_t bg, fg; } nova_app;\nstatic const nova_app nova_apps[] = {\n${tokens.apps.map(a=>`  {"${a.id}", "${a.label}", ${hex(a.bg)}, ${hex(a.fg)}},`).join('\n')}\n};\n#define NOVA_APP_COUNT ${tokens.apps.length}\n`;
h+=`#define NOVA_ACCENT ${hex(tokens.colors.accent_mint)}\n`;
// Blob symbols come from theme/generated/blobs.S (.incbin).
h+=res.fonts.map(f=>`extern const uint8_t nova_blob_font_${f.name}[];\n`).join('')+`extern const uint8_t nova_blob_icons[];\nextern const uint8_t nova_blob_wallpaper[];\n`;
fs.writeFileSync(path.join(OUT,'assets.h'),h);
let s='/* Generated by theme/tools/gen-assets.mjs. */\n.section .rodata\n';
for(const f of res.fonts)s+=`.global nova_blob_font_${f.name}\nnova_blob_font_${f.name}: .incbin "font_${f.name}.bin"\n`;
s+='.global nova_blob_icons\nnova_blob_icons: .incbin "icons.bin"\n.global nova_blob_wallpaper\nnova_blob_wallpaper: .incbin "wallpaper.bin"\n';
fs.writeFileSync(path.join(OUT,'blobs.S'),s);
console.log('wrote',OUT);
