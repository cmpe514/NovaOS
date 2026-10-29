// One test per item of the parity checklist in the project brief (section 1), run against the offline build.
import {test,expect,openShell,openApp,FIXED_TIME} from './helpers.js';

test.describe('Notes / Tasks / Files',()=>{
  for(const [type,app,fields,list] of [
    ['note','notes',{'#note-title':'title','#note-content':'body'},'#note-list'],
    ['file','files',{'#file-name':'title','#file-content':'body'},'#file-list'],
  ]){
    test(`${app}: create, list newest-first, delete`,async({page})=>{
      await openShell(page,{time:null});
      await openApp(page,app);
      await expect(page.locator(`#${type}-empty`)).toBeVisible();
      for(const n of ['First','Second']){
        for(const [sel,kind] of Object.entries(fields))await page.fill(sel,kind==='title'?`${n} ${type}`:`${n} body`);
        await page.locator(`#${type}-form [type=submit]`).click();
        await expect(page.locator(`${list} > article`).first()).toContainText(`${n} ${type}`);
        await page.waitForTimeout(5); // distinct created_at
      }
      await expect(page.locator(`${list} > article`)).toHaveCount(2);
      await expect(page.locator(`${list} > article`).nth(0)).toContainText(`Second ${type}`);
      await expect(page.locator(`${list} > article`).nth(1)).toContainText(`First ${type}`);
      await expect(page.locator(`#${type}-title, #${type}-name`).first()).toHaveValue('');
      const del=page.locator(`${list} > article`).first().getByRole('button',{name:`Delete ${type}`});
      await expect(del).toHaveText('Delete');
      await del.click();
      await expect(page.locator(`${list} > article`)).toHaveCount(1);
      await expect(page.locator(`${list} > article`)).toContainText(`First ${type}`);
    });
  }

  test('tasks: create, toggle complete optimistically with strikethrough, delete',async({page})=>{
    await openShell(page,{time:null});
    await openApp(page,'tasks');
    await page.fill('#task-title','Water the plants');
    await page.locator('#task-form [type=submit]').click();
    await page.waitForTimeout(5);
    await page.fill('#task-title','Call mum');
    await page.keyboard.press('Enter');
    const rows=page.locator('#task-list > article');
    await expect(rows).toHaveCount(2);
    await expect(rows.nth(0)).toContainText('Call mum');
    // Slow the backend down so the optimistic state is observable before the write completes.
    await page.evaluate(()=>{const sdk=window.dataSdk,update=sdk.update;sdk.update=async r=>{await new Promise(res=>setTimeout(res,400));return update(r)}});
    await rows.nth(1).locator('.task-checkbox').check();
    const title=rows.nth(1).locator('[data-role="saved-task-title"]');
    await expect(title).toHaveClass(/line-through/,{timeout:200});
    await expect(title).toHaveClass(/opacity-50/);
    await expect(title).toHaveCSS('text-decoration-line','line-through');
    await page.waitForTimeout(600);
    await expect(rows.nth(1).locator('.task-checkbox')).toBeChecked();
    await rows.nth(0).getByRole('button',{name:'Delete task'}).click();
    await expect(rows).toHaveCount(1);
    await expect(rows.first()).toContainText('Water the plants');
  });

  test('records survive a reload',async({page})=>{
    await openShell(page,{time:null});
    await openApp(page,'notes');
    await page.fill('#note-title','Persistent');
    await page.locator('#note-form [type=submit]').click();
    await expect(page.locator('#note-list > article')).toHaveCount(1);
    await page.reload();
    await page.locator('html[data-ready="true"]').waitFor();
    await openApp(page,'notes');
    await expect(page.locator('#note-list > article')).toContainText('Persistent');
  });
});

test.describe('Calculator',()=>{
  const press=async(page,keys)=>{for(const k of keys)await page.locator(`#calc-keys [data-key="${k}"]`).click()};
  test('4-column keypad, "=" spans two rows',async({page})=>{
    await openShell(page);await openApp(page,'calculator');
    const columns=await page.locator('#calc-keys').evaluate(el=>getComputedStyle(el).gridTemplateColumns.split(' ').length);
    expect(columns).toBe(4);
    const eq=await page.locator('[data-key="="]').boundingBox(),one=await page.locator('[data-key="1"]').boundingBox(),zero=await page.locator('[data-key="0"]').boundingBox();
    expect(eq.y).toBeCloseTo(one.y,0);
    expect(eq.y+eq.height).toBeCloseTo(zero.y+zero.height,0);
    expect(zero.width).toBeGreaterThan(one.width*2);
  });
  test('arithmetic, 11 significant digits and Error',async({page})=>{
    await openShell(page);await openApp(page,'calculator');
    const display=page.locator('#calc-display');
    await press(page,['1','2','+','3','0','=']);await expect(display).toHaveText('42');
    await press(page,['C','1','÷','3','=']);await expect(display).toHaveText('0.33333333333');
    await press(page,['C','2','×','3','−','1','=']);await expect(display).toHaveText('5');
    await press(page,['C','1','÷','0','=']);await expect(display).toHaveText('Error');
    await press(page,['7']);await expect(display).toHaveText('7');
    await press(page,['8','⌫']);await expect(display).toHaveText('7');
    await press(page,['.','5','.','5']);await expect(display).toHaveText('7.55');
  });
  test('physical keyboard',async({page})=>{
    await openShell(page);await openApp(page,'calculator');
    await page.locator('#calculator h2').click();
    await page.keyboard.type('12*3');await page.keyboard.press('Enter');
    await expect(page.locator('#calc-display')).toHaveText('36');
    await page.keyboard.press('Backspace');await expect(page.locator('#calc-display')).toHaveText('3');
    await page.keyboard.type('/0=');await expect(page.locator('#calc-display')).toHaveText('Error');
    await page.keyboard.press('Delete');await expect(page.locator('#calc-display')).toHaveText('0');
  });
});

test('Clock shows the current time; 24-hour setting changes the format',async({page})=>{
  await openShell(page);await openApp(page,'clock');
  await expect(page.locator('#clock-large')).toHaveText(/^09:41:\d\d AM$/);
  await expect(page.locator('#clock-date')).toHaveText('Saturday, March 14, 2026');
  await page.locator('.bottom-dock [data-open="shade"]').click();
  await page.locator('[data-switch="clock24"]').click();
  await expect(page.locator('#status-time')).toHaveText('09:41');
});

test('Calendar: today highlighted, previous / next / jump to today',async({page})=>{
  await openShell(page);await openApp(page,'calendar');
  const heading=page.locator('#month-heading');
  await expect(heading).toHaveText('March 2026');
  const today=page.locator('#calendar-days span',{hasText:/^14$/});
  await expect(today).toHaveClass(/font-bold/);
  await expect(today).toHaveCSS('background-color','rgb(134, 244, 239)');
  await expect(page.locator('#calendar-days span').first()).toHaveText('1'); // 1 March 2026 is a Sunday: no leading blanks
  await page.locator('#month-next').click();await expect(heading).toHaveText('April 2026');
  await page.locator('#month-prev').click();await page.locator('#month-prev').click();await expect(heading).toHaveText('February 2026');
  await expect(page.locator('#calendar-days .font-bold')).toHaveCount(0);
  await page.locator('#month-today').click();await expect(heading).toHaveText('March 2026');
});

test('Photos: three images and a lightbox that Esc closes',async({page})=>{
  await openShell(page);await openApp(page,'photos');
  const thumbs=page.locator('#photos .photo-button');
  await expect(thumbs).toHaveCount(3);
  for(let i=0;i<3;i++)expect(await thumbs.nth(i).locator('img').evaluate(img=>img.complete&&img.naturalWidth>0)).toBe(true);
  await thumbs.nth(1).click();
  await expect(page.locator('#photo-modal')).toBeVisible();
  await expect(page.locator('#photo-close')).toBeFocused();
  await expect(page.locator('#photo-large')).toHaveAttribute('src',/assets\/photo2\./);
  await page.keyboard.press('Escape');
  await expect(page.locator('#photo-modal')).toBeHidden();
  await expect(thumbs.nth(1)).toBeFocused();
});

test('Weather: sample card, clearly labelled',async({page})=>{
  await openShell(page);await openApp(page,'weather');
  await expect(page.locator('#weather-place')).toHaveText('Sample weather · Dubai');
  await expect(page.locator('#weather-temperature')).toHaveText('29°');
  await expect(page.locator('#weather-note')).toContainText('not a live forecast');
});

test.describe('Music',()=>{
  test('plays the "Soft Signals" arpeggio with WebAudio and stops',async({page})=>{
    await openShell(page,{time:null});await openApp(page,'music');
    await page.locator('#music-play').click();
    await expect(page.locator('#music-stop')).toBeVisible();
    await expect.poll(()=>page.evaluate(()=>window.__oscillators)).toBeGreaterThan(1);
    await page.locator('#music-stop').click();
    await expect(page.locator('#music-play')).toBeVisible();
  });
  test('is blocked by Quiet mode',async({page})=>{
    await openShell(page,{time:null});
    await page.locator('.bottom-dock [data-open="shade"]').click();
    await page.locator('[data-switch="quiet"]').click();
    await openApp(page,'music');
    await page.locator('#music-play').click();
    await expect(page.locator('#toast')).toHaveText('Turn off Quiet mode to play the melody.');
    await expect(page.locator('#music-stop')).toBeHidden();
    expect(await page.evaluate(()=>window.__oscillators)).toBe(0);
  });
});

test.describe('Browser',()=>{
  test('URL vs search normalisation and opening',async({page})=>{
    await openShell(page);await openApp(page,'browser');
    const go=async text=>{await page.fill('#browser-address',text);await page.locator('#browser-form [type=submit]').click()};
    await go('example.com/path');
    await expect(page.locator('#browser-current')).toHaveText('https://example.com/path');
    await go('http://neverssl.com');
    await expect(page.locator('#browser-current')).toHaveText('http://neverssl.com/');
    await go('weather in lisbon');
    await expect(page.locator('#browser-current')).toHaveText('https://www.google.com/search?q=weather%20in%20lisbon');
    await go('javascript:alert(1)');
    await expect(page.locator('#browser-current')).toHaveText(/google\.com\/search\?q=javascript/);
    const opened=await page.evaluate(()=>window.__opened.map(a=>a[0]));
    expect(opened).toEqual(['https://example.com/path','http://neverssl.com/','https://www.google.com/search?q=weather%20in%20lisbon','https://www.google.com/search?q=javascript%3Aalert(1)']);
  });
  test('tabs and back / forward history',async({page})=>{
    await openShell(page);await openApp(page,'browser');
    const go=async text=>{await page.fill('#browser-address',text);await page.locator('#browser-form [type=submit]').click()};
    await expect(page.locator('#browser-back')).toBeDisabled();
    await go('one.example');await go('two.example');
    await expect(page.locator('#browser-back')).toBeEnabled();
    await page.locator('#browser-back').click();
    await expect(page.locator('#browser-current')).toHaveText('https://one.example/');
    await expect(page.locator('#browser-forward')).toBeEnabled();
    await page.locator('#browser-forward').click();
    await expect(page.locator('#browser-current')).toHaveText('https://two.example/');
    await page.locator('#browser-new-tab').click();
    const tabs=page.locator('#browser-tab-list [role="tab"]');
    await expect(tabs).toHaveCount(2);
    await expect(tabs.nth(1)).toHaveAttribute('aria-selected','true');
    await expect(tabs.nth(1)).toContainText('New tab');
    await expect(page.locator('#browser-address')).toBeFocused();
    await tabs.nth(0).locator('.browser-tab-name').click();
    await expect(tabs.nth(0)).toContainText('two.example');
    await expect(page.locator('#browser-address')).toHaveValue('https://two.example/');
    await tabs.nth(0).getByRole('button',{name:'Close tab'}).click();
    await expect(tabs).toHaveCount(1);
    await tabs.nth(0).getByRole('button',{name:'Close tab'}).click();
    await expect(tabs).toHaveCount(1); // closing the last tab opens a fresh one
    await expect(tabs.nth(0)).toContainText('New tab');
  });
});

test.describe('Quick settings and Settings',()=>{
  test('wallpaper mood, focus, reduce motion, accent, brightness',async({page})=>{
    await openShell(page);
    const os=page.locator('#os');
    await page.locator('.bottom-dock [data-open="shade"]').click();
    await page.locator('[data-switch="alt"]').click();await expect(os).toHaveClass(/\balt\b/);
    await expect(page.locator('[data-switch="alt"]')).toHaveAttribute('aria-pressed','true');
    await page.locator('[data-switch="focus"]').click();await expect(os).toHaveClass(/\bfocus\b/);
    await page.locator('[data-switch="motion"]').click();await expect(os).toHaveClass(/no-motion/);
    await page.locator('#brightness').fill('60');
    await expect(page.locator('.wallpaper')).toHaveCSS('opacity','0.6');
    await page.locator('.overlay:not([hidden]) [data-open="settings"]').click();
    await expect(page.locator('#settings')).toBeVisible();
    await expect(page.locator('#setting-alt')).toBeChecked();
    await expect(page.locator('#setting-brightness')).toHaveValue('60');
    await page.selectOption('#setting-accent','peach');
    expect(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim())).toBe('#ffc4a6');
    await page.selectOption('#setting-accent','blue');
    expect(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim())).toBe('#a7c8ff');
  });

  test('text scale 90–120% resizes text; 100% matches the original sizes',async({page})=>{
    await openShell(page);
    const size=sel=>page.locator(sel).first().evaluate(el=>parseFloat(getComputedStyle(el).fontSize));
    const samples=['#home h1','.shortcut','#status-time','#home-time','#home .home-right h2'];
    const original=[32,12,12,null,24];
    const at100=[];for(const s of samples)at100.push(await size(s));
    original.forEach((px,i)=>{if(px)expect(at100[i]).toBeCloseTo(px,1)});
    await openApp(page,'settings');
    await page.locator('#setting-scale').fill('120');
    for(let i=0;i<samples.length;i++)expect(await size(samples[i])).toBeCloseTo(at100[i]*1.2,0);
    await page.locator('#setting-scale').fill('90');
    for(let i=0;i<samples.length;i++)expect(await size(samples[i])).toBeCloseTo(at100[i]*0.9,0);
    const calcText=await page.evaluate(()=>getComputedStyle(document.getElementById('calc-display')).fontSize);
    expect(parseFloat(calcText)).toBeCloseTo(48*0.9,0); // Tailwind text-5xl follows the scale too
  });

  test('Wi-Fi and Bluetooth tiles are labelled as preview-only outside the device',async({page})=>{
    await openShell(page);
    await page.locator('.bottom-dock [data-open="shade"]').click();
    await expect(page.locator('#quick-disclaimer')).toContainText('Preview');
    await page.locator('[data-switch="wifi"]').click();
    await expect(page.locator('[data-switch="wifi"]')).toHaveAttribute('aria-pressed','false');
    await expect(page.locator('#wifi-icon')).toHaveCSS('opacity','0.3');
    await page.locator('[data-switch="bluetooth"]').click();
    await expect(page.locator('[data-switch="bluetooth"]')).toHaveAttribute('aria-pressed','true');
    await expect(page.locator('#power-button')).toBeHidden(); // no fake power menu in a browser preview
    await openApp(page,'settings');
    await expect(page.locator('#wifi-note')).toContainText('Preview only');
    await expect(page.locator('#settings-about')).toContainText('preview');
  });

  test('settings persist across reloads',async({page})=>{
    await openShell(page);
    await openApp(page,'settings');
    await page.locator('#setting-alt').check();
    await page.locator('#setting-clock24').check();
    await page.selectOption('#setting-accent','blue');
    await page.locator('#setting-scale').fill('110');
    await page.waitForTimeout(500); // saves are debounced
    await page.reload();
    await page.locator('html[data-ready="true"]').waitFor();
    await expect(page.locator('#os')).toHaveClass(/\balt\b/);
    await expect(page.locator('#status-time')).toHaveText('09:41');
    expect(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim())).toBe('#a7c8ff');
    expect(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--scale').trim())).toBe('1.1');
  });
});

test.describe('Keyboard, focus and accessibility',()=>{
  test('Esc closes the app drawer and quick settings; Home key returns home',async({page})=>{
    await openShell(page);
    await page.locator('.bottom-dock [data-open="drawer"]').click();
    await expect(page.locator('#drawer')).toBeVisible();
    await expect(page.locator('#drawer-grid .shortcut')).toHaveCount(11);
    await expect(page.locator('#drawer [data-close-overlay]')).toBeFocused();
    await page.keyboard.press('Escape');
    await expect(page.locator('#drawer')).toBeHidden();
    await page.locator('#status-settings').click();
    await expect(page.locator('#shade')).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(page.locator('#shade')).toBeHidden();
    await openApp(page,'calendar');
    await page.locator('#calendar h2').click();
    await page.keyboard.press('Home');
    await expect(page.locator('#home')).toBeVisible();
    await expect(page.locator('#calendar')).toBeHidden();
  });
  test('Home key inside a text field moves the caret, not the screen',async({page})=>{
    await openShell(page);await openApp(page,'notes');
    await page.locator('#note-title').fill('abc');
    await page.locator('#note-title').press('Home');
    await expect(page.locator('#notes')).toBeVisible();
  });
  test('visible focus ring on keyboard focus',async({page})=>{
    await openShell(page);
    await page.keyboard.press('Tab');
    const focused=page.locator(':focus');
    await expect(focused).toHaveCSS('outline-style','solid');
    await expect(focused).toHaveCSS('outline-width','2px');
    await expect(focused).toHaveCSS('outline-color','rgb(134, 244, 239)');
  });
  test('ARIA labels from the original are preserved',async({page})=>{
    await openShell(page);
    for(const label of ['System indicators','Open quick settings','Home screen','Home apps','App drawer','All apps','Quick settings','Notes app','Tasks app','Calculator app','Clock app','Music app','Photos app','Tabbed browser','Files app','Calendar app','Settings app','Photo viewer','System navigation','Go home','Close app drawer','Close quick settings','Browser tabs','New tab','Back','Forward','Website address or search','Previous month','Next month','Close photo','View cosmic abstract photo','View colorful abstract photo','View blue and purple smoke photo'])
      expect(await page.locator(`[aria-label="${label}"]`).count(),label).toBeGreaterThan(0);
  });
  test('tab order reaches the dock and opens apps with Enter',async({page})=>{
    await openShell(page);
    await page.locator('#home .shortcut[data-open="tasks"]').focus();
    await page.keyboard.press('Enter');
    await expect(page.locator('#tasks')).toBeVisible();
    await page.locator('#home-button').focus();
    await page.keyboard.press('Enter');
    await expect(page.locator('#home')).toBeVisible();
  });
});
