#!/usr/bin/env node
// Renders the boot-splash images (wordmark + progress bar) from nova.config.json with the shell's own
// DM Sans font, using Playwright's Chromium (tests/node_modules). Output:
//   image/config/includes.chroot/usr/share/plymouth/themes/novaos/{logo.png,bar.png,track.png}
// `make iso` runs this automatically when the brand name or tagline changed; the default render is committed.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';

const ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const cfg=JSON.parse(fs.readFileSync(path.join(ROOT,'nova.config.json'),'utf8'));
const out=path.join(ROOT,'image/config/includes.chroot/usr/share/plymouth/themes/novaos');
const require=createRequire(path.join(ROOT,'tests/package.json'));
const {chromium}=require('playwright');
// Fonts are embedded as data: URLs (a setContent page may not read file:// URLs).
const font=f=>'data:font/woff2;base64,'+fs.readFileSync(path.join(ROOT,'ui/dist/fonts',f)).toString('base64');
if(!fs.existsSync(path.join(ROOT,'ui/dist/fonts')))throw new Error('build the UI first (make ui): the splash uses its fonts');
const esc=s=>s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const html=`<!doctype html><style>
@font-face{font-family:D;font-weight:700;src:url(${font('dm-sans-latin-700-normal.woff2')})}
@font-face{font-family:D;font-weight:400;src:url(${font('dm-sans-latin-400-normal.woff2')})}
html,body{margin:0;background:transparent}
#logo{display:inline-block;padding:8px 16px;text-align:center;color:#fff;font-family:D}
#logo b{display:block;font-size:76px;font-weight:700;letter-spacing:-.02em;line-height:1.05}
#logo span{display:block;margin-top:14px;font-size:22px;font-weight:400;color:#d6e3f1}
#bar,#track{width:260px;height:4px;border-radius:2px}
#bar{background:${cfg.accents.mint}} #track{background:#ffffff26;margin-top:8px}
</style><div id="logo"><b>${esc(cfg.name)}</b><span>${esc(cfg.tagline)}</span></div><div id="bar"></div><div id="track"></div>`;
const browser=await chromium.launch();
const page=await browser.newPage({deviceScaleFactor:1});
await page.setContent(html);
await page.evaluate(()=>document.fonts.ready);
fs.mkdirSync(out,{recursive:true});
for(const id of ['logo','bar','track'])await page.locator('#'+id).screenshot({path:path.join(out,id+'.png'),omitBackground:true});
await browser.close();
fs.writeFileSync(path.join(out,'.rendered-for'),`${cfg.name}\n${cfg.tagline}\n${cfg.accents.mint}\n`);
console.log('[splash] rendered',out);
