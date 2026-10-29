#!/usr/bin/env node
// Serves a built UI directory on 127.0.0.1 for previews and the Playwright suite (no novad: "preview" mode).
//   node tools/serve-static.mjs [dir] [port]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const UI=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const ROOT=path.resolve(process.argv[2]||path.join(UI,'dist'));
const PORT=Number(process.argv[3]||process.env.PORT||8080);
const TYPES={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.svg':'image/svg+xml','.jpg':'image/jpeg','.jpeg':'image/jpeg','.png':'image/png','.woff2':'font/woff2','.txt':'text/plain; charset=utf-8'};

http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost');
  if(url.pathname.startsWith('/api/')){res.writeHead(404,{'Content-Type':'application/json'});res.end('{"error":"no daemon in preview mode"}');return}
  const file=path.resolve(ROOT,'.'+decodeURIComponent(url.pathname==='/'?'/index.html':url.pathname));
  if(!file.startsWith(ROOT+path.sep)){res.writeHead(403);res.end();return}
  fs.readFile(file,(err,data)=>{
    if(err){res.writeHead(404);res.end('not found');return}
    res.writeHead(200,{'Content-Type':TYPES[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store'});
    res.end(data);
  });
}).listen(PORT,'127.0.0.1',()=>console.log(`[serve] ${ROOT} on http://127.0.0.1:${PORT}/`));
