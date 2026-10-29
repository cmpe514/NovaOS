// Builds a runnable "reference" from the untouched ui/original/ui.html, for the visual parity tests.
// The uploaded original is truncated mid-script (line 543), so it cannot run as-is. We complete it with
// the same reconstructed tail the product uses (ui/src/js/app.js, the RECONSTRUCTED block only), swap the
// Canva SDK <script> tags for an in-memory stub, and leave every other byte of markup and CSS alone.
// The remote URLs it references (Google Fonts, jsDelivr, the Tailwind play CDN, Pexels) are answered by
// Playwright routes in e2e/helpers.js — from local vendored copies, or from the real CDN when
// NOVA_REFERENCE_CDN=1 (needs internet; gives baselines rendered exactly as the original was).
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE=path.dirname(fileURLToPath(import.meta.url));
const REPO=path.resolve(HERE,'../..');
export const REFERENCE_DIR=path.join(HERE,'.build');

export function buildReference(){
  let html=fs.readFileSync(path.join(REPO,'ui/original/ui.html'),'utf8');
  const app=fs.readFileSync(path.join(REPO,'ui/src/js/app.js'),'utf8');
  const start=app.indexOf("if(content.textContent!==(r.content||''))content.")+"if(content.textContent!==(r.content||''))content.".length;
  const end=app.indexOf('// NovaOS additions: physical keyboard');
  if(start<60||end<0)throw new Error('reconstructed block not found in ui/src/js/app.js');
  const tail=app.slice(start,app.lastIndexOf('\n// ----',end));
  if(!html.trimEnd().endsWith('content.'))throw new Error('ui/original/ui.html no longer ends where expected');
  html=html.trimEnd()+tail+'\nlucide.createIcons();\ninitialize();\n</script>\n</body>\n</html>\n';
  // Canva platform scripts (with SRI hashes) -> one local stub.
  html=html.replace(/<script>window\["__codeletBootstrap__"\][\s\S]*?<\/script>/,'');
  html=html.replace(/<script src="\/_sdk\/[^"]*"[^>]*><\/script>/g,'');
  html=html.replace('</head>','<script src="/__reference/stub-sdk.js"></script>\n </head>');
  if(/_sdk\//.test(html))throw new Error('Canva SDK tag left in reference');
  fs.mkdirSync(REFERENCE_DIR,{recursive:true});
  fs.writeFileSync(path.join(REFERENCE_DIR,'index.html'),html);
  fs.writeFileSync(path.join(REFERENCE_DIR,'stub-sdk.js'),`// In-memory stand-in for Canva's hosted data SDK (reference rendering only).
window.dataSdk=(()=>{let h=null,rows=[],n=0;const emit=()=>h&&h.onDataChanged(rows.map(r=>({...r})));
return{async init(x){h=x;emit();return{isOk:true}},
async create(r){rows.push({...r,__backendId:'r'+(++n)});emit();return{isOk:true}},
async update(r){rows=rows.map(x=>x.__backendId===r.__backendId?{...r}:x);emit();return{isOk:true}},
async delete(r){rows=rows.filter(x=>x.__backendId!==r.__backendId);emit();return{isOk:true}}}})();\n`);
  return REFERENCE_DIR;
}
if(process.argv[1]===fileURLToPath(import.meta.url))console.log('reference written to',buildReference());
