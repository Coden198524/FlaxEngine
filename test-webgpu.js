const puppeteer = require('puppeteer');
const fs = require('fs');

(async () => {
  const browser = await puppeteer.launch({
    headless: false,
    args: ['--enable-unsafe-webgpu']
  });

  const page = await browser.newPage();
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

  console.log('Navigating to FlaxGame...');
  await page.goto('http://localhost:8000/FlaxGame.html', {
    waitUntil: 'networkidle0',
    timeout: 60000
  });

  // Wait for 30 seconds to capture errors
  console.log('Waiting 30 seconds to capture logs...');
  await new Promise(resolve => setTimeout(resolve, 30000));

  // Save logs to file
  fs.writeFileSync('E:/Work/FlaxEngine/webgpu-test-log.txt', logs.join('\n'));
  console.log('Logs saved to webgpu-test-log.txt');

  await browser.close();
})();
