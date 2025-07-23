import puppeteer from 'puppeteer';

async function simpleDebug() {
  console.log('🔍 Simple Debug Test');
  
  const browser = await puppeteer.launch({
    headless: false,
    slowMo: 500,
    devtools: true
  });

  const page = await browser.newPage();
  await page.setViewport({ width: 1920, height: 1080 });
  
  console.log('📡 Going to http://localhost:3001');
  await page.goto('http://localhost:3001', { waitUntil: 'networkidle0' });
  
  // Wait for page to fully load
  await new Promise(resolve => setTimeout(resolve, 3000));
  
  console.log('📄 Checking page content...');
  
  // Get page title
  const title = await page.title();
  console.log(`Title: "${title}"`);
  
  // Get page content
  const bodyContent = await page.evaluate(() => document.body.textContent);
  console.log(`Body has ${bodyContent.length} characters`);
  
  // Check for key elements
  const h1Exists = await page.$('h1') !== null;
  console.log(`H1 exists: ${h1Exists}`);
  
  if (h1Exists) {
    const h1Text = await page.$eval('h1', el => el.textContent);
    console.log(`H1 text: "${h1Text}"`);
  }
  
  // Count buttons
  const buttonCount = await page.$$eval('button', buttons => buttons.length);
  console.log(`Found ${buttonCount} buttons`);
  
  // Get button texts
  const buttonTexts = await page.$$eval('button', buttons => 
    buttons.slice(0, 5).map(btn => btn.textContent.trim())
  );
  console.log('First 5 buttons:', buttonTexts);
  
  // Check for XRPL-related content
  const hasXRPL = bodyContent.toLowerCase().includes('xrpl') || 
                  bodyContent.toLowerCase().includes('mptoken') ||
                  bodyContent.toLowerCase().includes('ripple');
  console.log(`Contains XRPL content: ${hasXRPL}`);
  
  // Check for connection elements
  const hasConnectionInput = await page.$('input[placeholder*="ws://"]') !== null ||
                             await page.$('input[value*="ws://"]') !== null;
  console.log(`Has connection input: ${hasConnectionInput}`);
  
  // Take a screenshot
  await page.screenshot({ path: 'tests/screenshots/simple-debug.png', fullPage: true });
  console.log('📸 Screenshot saved: simple-debug.png');
  
  // Wait for user to inspect
  console.log('⏰ Waiting 5 seconds for manual inspection...');
  await new Promise(resolve => setTimeout(resolve, 5000));
  
  await browser.close();
  console.log('✅ Debug complete');
}

simpleDebug().catch(console.error);