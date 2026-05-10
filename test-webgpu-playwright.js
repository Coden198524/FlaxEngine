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
  const logs = [];

  // Capture console messages
  page.on('console', msg => {
    const text = msg.text();
    console.log('CONSOLE:', text);
    logs.push(text);
  });

  // Capture page errors
  page.on('pageerror', error => {
    console.log('PAGE ERROR:', error.message);
    logs.push('ERROR: ' + error.message);
  });

  console.log('Navigating to Bistro demo...');
  try {
    await page.goto('http://localhost:8000/index.html', {
      waitUntil: 'domcontentloaded',
      timeout: 60000
    });
  } catch (e) {
    console.log('Navigation timeout or error:', e.message);
  }

  // Wait for 30 seconds to capture errors
  console.log('Waiting 30 seconds to capture logs...');
  await new Promise(resolve => setTimeout(resolve, 30000));

  // Save logs to file
  fs.writeFileSync('E:/Work/FlaxEngine/webgpu-test-log.txt', logs.join('\n'));
  console.log('Logs saved to webgpu-test-log.txt');
  console.log('Total log entries:', logs.length);

  await browser.close();
})();
