// Playwright suite for the NovaOS shell UI.
//   preview: the built UI from a plain static server (no novad) — parity, offline, visual tests
//   device:  the built UI served by novad --dev with a fake system backend — API-backed behaviour
// Browsers: `npx playwright install chromium` once (CI and `make test` do this for you).
import {defineConfig} from '@playwright/test';

export const PREVIEW_PORT=Number(process.env.NOVA_PREVIEW_PORT||8090);
export const DEVICE_PORT=Number(process.env.NOVA_DEVICE_PORT||7199);
const projects=(process.env.NOVA_PROJECTS||'preview,device').split(',');

export default defineConfig({
  testDir:'./e2e',
  timeout:30_000,
  expect:{timeout:5_000},
  fullyParallel:true,
  workers:process.env.CI?2:undefined,
  reporter:[['list'],['html',{open:'never',outputFolder:'report'}]],
  outputDir:'results',
  use:{locale:'en-US',timezoneId:'UTC',trace:'retain-on-failure',screenshot:'only-on-failure'},
  projects:[
    {name:'preview',testIgnore:/device\.spec\.js/,use:{baseURL:`http://127.0.0.1:${PREVIEW_PORT}`}},
    {name:'device',testMatch:/device\.spec\.js/,use:{baseURL:`http://127.0.0.1:${DEVICE_PORT}`}},
  ].filter(p=>projects.includes(p.name)),
  webServer:[
    projects.includes('preview')&&{command:`node ../ui/tools/serve-static.mjs ../ui/dist ${PREVIEW_PORT}`,url:`http://127.0.0.1:${PREVIEW_PORT}/index.html`,reuseExistingServer:!process.env.CI,stdout:'pipe'},
    projects.includes('device')&&{command:`node e2e/start-novad.mjs ${DEVICE_PORT}`,url:`http://127.0.0.1:${DEVICE_PORT}/healthz`,reuseExistingServer:false,stdout:'pipe',stderr:'pipe',timeout:120_000},
  ].filter(Boolean),
});
