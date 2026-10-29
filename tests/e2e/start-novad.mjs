// Starts `novad --dev` (fake system backend) for the device project, with throw-away data under
// tests/.novad/. Builds the binary first if needed. Usage: node e2e/start-novad.mjs <port>
import {spawn,execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const HERE=path.dirname(fileURLToPath(import.meta.url));
const REPO=path.resolve(HERE,'../..');
const port=process.argv[2]||'7199';
const state=path.join(REPO,'tests/.novad');
const bin=path.join(REPO,'daemon/bin/novad');
fs.rmSync(state,{recursive:true,force:true});
for(const d of ['data','home','run'])fs.mkdirSync(path.join(state,d),{recursive:true,mode:0o700});
if(!fs.existsSync(bin)||process.env.NOVA_REBUILD!=='0'){
  console.log('[novad] building',bin);
  execFileSync('go',['build','-o',bin,'./cmd/novad'],{cwd:path.join(REPO,'daemon'),stdio:'inherit',env:{...process.env,CGO_ENABLED:'0'}});
}
const child=spawn(bin,['serve','--dev','--fake-storage=blank','--listen',`127.0.0.1:${port}`,'--ui',path.join(REPO,'ui/dist'),
  '--data',path.join(state,'data'),'--home',path.join(state,'home'),'--runtime-dir',path.join(state,'run')],{stdio:'inherit'});
for(const sig of ['SIGINT','SIGTERM'])process.on(sig,()=>{child.kill('SIGTERM');process.exit(0)});
child.on('exit',code=>process.exit(code??1));
