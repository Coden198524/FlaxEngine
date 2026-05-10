const playwright = require('playwright-core');
const fs = require('fs');

(async () => {
  // Try to find Chrome installation
  const chromePaths = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
    process.env.LOCALAPPDATA + '\\Google\\Chrome\\Application\\chrome.exe'
  ];

  let chromePath = null;
  for (const path of chromePaths) {
    if (fs.existsSync(path)) {
      chromePath = path;
      break;
    }
  }

  if (!chromePath) {
    console.error('Chrome not found. Please install Chrome or specify path.');
    process.exit(1);
  }

  console.log('Using Chrome at:', chromePath);

  const browser = await playwright.chromium.launch({
    executablePath: chromePath,
    headless: false,
    args: ['--enable-unsafe-webgpu']
  });

  const context = await browser.newContext();
  const page = await context.newPage();

  // Capture console messages
  page.on('console', msg => {
    console.log('CONSOLE:', msg.text());
  });

  // Capture page errors
  page.on('pageerror', error => {
    console.log('PAGE ERROR:', error.message);
  });

  console.log('Navigating to test page...');
  await page.goto('file:///E:/Work/FlaxEngine/test-webgpu-simple.html', {
    waitUntil: 'domcontentloaded',
    timeout: 10000
  });

  // Wait for test to complete
  console.log('Waiting for test to complete...');
  await new Promise(resolve => setTimeout(resolve, 5000));

  // Get the output div content
  const output = await page.$eval('#output', el => el.innerHTML);
  console.log('\n=== Test Results ===');
  console.log(output.replace(/<[^>]*>/g, '\n').replace(/\n+/g, '\n'));

  await browser.close();
})();
