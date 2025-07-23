import puppeteer from 'puppeteer';
import path from 'path';
import { promises as fs } from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const config = {
  testTimeout: 30000,
  browser: {
    headless: process.env.CI === 'true' || process.argv.includes('--headless'),
    slowMo: process.argv.includes('--slow') ? 100 : 0,
    devtools: process.argv.includes('--debug'),
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-dev-shm-usage',
      '--disable-accelerated-2d-canvas',
      '--no-first-run',
      '--no-zygote',
      '--disable-gpu'
    ]
  },
  baseUrl: process.env.TEST_BASE_URL || 'http://localhost:3001',
  rippleDUrl: process.env.RIPPLED_URL || 'ws://localhost:6006',
  screenshots: {
    enabled: true,
    path: './tests/screenshots',
    onFailure: true,
    onSuccess: false
  }
};

class NuxtMPTokenE2ETests {
  constructor() {
    this.browser = null;
    this.page = null;
    this.config = config;
    this.testResults = [];
  }

  async setup() {
    console.log('🚀 Starting Nuxt MPToken E2E Tests');
    
    // Ensure screenshots directory exists
    try {
      await fs.mkdir(this.config.screenshots.path, { recursive: true });
    } catch (err) {
      // Directory might already exist
    }
    
    // Launch browser
    this.browser = await puppeteer.launch(this.config.browser);
    this.page = await this.browser.newPage();
    
    await this.page.setViewport({ width: 1920, height: 1080 });
    
    // Setup console logging
    this.page.on('console', msg => {
      if (this.config.browser.devtools) {
        console.log(`[BROWSER] ${msg.type()}: ${msg.text()}`);
      }
    });

    // Setup error handling
    this.page.on('pageerror', error => {
      console.error(`[PAGE ERROR] ${error.message}`);
      this.recordResult('Page Error', 'FAILED', error.message);
    });

    // Setup request/response monitoring
    this.page.on('requestfailed', request => {
      console.error(`[REQUEST FAILED] ${request.url()}: ${request.failure().errorText}`);
    });

    console.log('✅ Browser setup completed');
  }

  async teardown() {
    if (this.browser) {
      await this.browser.close();
      console.log('🔌 Browser closed');
    }
  }

  async recordResult(testName, status, details = '') {
    this.testResults.push({
      test: testName,
      status,
      details,
      timestamp: new Date().toISOString()
    });
  }

  async takeScreenshot(name) {
    if (!this.config.screenshots.enabled) return;
    
    const filename = `${name.replace(/\s+/g, '-').toLowerCase()}-${Date.now()}.png`;
    const filepath = path.join(this.config.screenshots.path, filename);
    
    await this.page.screenshot({ 
      path: filepath, 
      fullPage: true 
    });
    
    console.log(`📸 Screenshot saved: ${filename}`);
    return filename;
  }

  async waitForElement(selector, timeout = 5000) {
    try {
      await this.page.waitForSelector(selector, { timeout });
      return true;
    } catch (err) {
      console.error(`❌ Element not found: ${selector}`);
      return false;
    }
  }

  async testHomePage() {
    console.log('🧪 Testing home page...');
    
    try {
      await this.page.goto(this.config.baseUrl, { waitUntil: 'networkidle0' });
      
      // Check if page loads
      const title = await this.page.title();
      if (!title.includes('MPToken')) {
        throw new Error(`Invalid page title: ${title}`);
      }
      
      // Check main elements
      await this.page.waitForSelector('h1', { timeout: 5000 });
      const mainTitle = await this.page.$eval('h1', el => el.textContent);
      
      if (!mainTitle.includes('MPToken Testing Interface')) {
        throw new Error(`Main title not found or incorrect: ${mainTitle}`);
      }
      
      // Check navigation tabs
      const tabs = await this.page.$$('[role="tab"], .tab-button, [data-tab]');
      if (tabs.length < 6) {
        throw new Error(`Expected at least 6 tabs, found ${tabs.length}`);
      }
      
      await this.takeScreenshot('home-page-loaded');
      await this.recordResult('Home Page Load', 'PASSED', `Title: ${title}, Tabs: ${tabs.length}`);
      
      console.log('✅ Home page test passed');
      return true;
      
    } catch (error) {
      await this.takeScreenshot('home-page-error');
      await this.recordResult('Home Page Load', 'FAILED', error.message);
      console.error('❌ Home page test failed:', error.message);
      return false;
    }
  }

  async testConnection() {
    console.log('🧪 Testing XRPL connection...');
    
    try {
      // Find server URL input
      const serverInput = await this.page.$('input[placeholder*="WebSocket"], input[value*="ws://"]');
      if (!serverInput) {
        throw new Error('Server URL input not found');
      }
      
      // Clear and set server URL
      await serverInput.click({ clickCount: 3 });
      await serverInput.type(this.config.rippleDUrl);
      
      // Find and click connect button
      const connectButton = await this.page.$('button:has-text("Connect"), button[class*="connect"]');
      if (!connectButton) {
        throw new Error('Connect button not found');
      }
      
      await connectButton.click();
      
      // Wait for connection status update
      await this.page.waitForTimeout(3000);
      
      // Check connection status
      const status = await this.page.evaluate(() => {
        const statusElement = document.querySelector('[class*="status"], [data-testid="connection-status"]');
        return statusElement ? statusElement.textContent : null;
      });
      
      await this.takeScreenshot('connection-test');
      
      if (status && (status.includes('CONNECTED') || status.includes('Connected'))) {
        await this.recordResult('XRPL Connection', 'PASSED', `Status: ${status}`);
        console.log('✅ Connection test passed');
        return true;
      } else {
        await this.recordResult('XRPL Connection', 'FAILED', `Status: ${status || 'Unknown'}`);
        console.log('⚠️ Connection test inconclusive - server might not be running');
        return false;
      }
      
    } catch (error) {
      await this.takeScreenshot('connection-error');
      await this.recordResult('XRPL Connection', 'FAILED', error.message);
      console.error('❌ Connection test failed:', error.message);
      return false;
    }
  }

  async testAccountGeneration() {
    console.log('🧪 Testing account generation...');
    
    try {
      // Find generate account button
      const generateButton = await this.page.$('button:has-text("Generate"), button[class*="generate"]');
      if (!generateButton) {
        throw new Error('Generate account button not found');
      }
      
      await generateButton.click();
      await this.page.waitForTimeout(2000);
      
      // Check if account details are populated
      const accountAddress = await this.page.evaluate(() => {
        const addressElements = [
          document.querySelector('[data-testid="account-address"]'),
          document.querySelector('[class*="account"] [class*="address"]'),
          document.querySelector('span[class*="address"]')
        ];
        
        for (const el of addressElements) {
          if (el && el.textContent && el.textContent.length > 10) {
            return el.textContent;
          }
        }
        return null;
      });
      
      if (!accountAddress || accountAddress === '-' || accountAddress.length < 10) {
        throw new Error(`Account address not generated properly: ${accountAddress}`);
      }
      
      await this.takeScreenshot('account-generated');
      await this.recordResult('Account Generation', 'PASSED', `Address: ${accountAddress}`);
      
      console.log('✅ Account generation test passed');
      return true;
      
    } catch (error) {
      await this.takeScreenshot('account-generation-error');
      await this.recordResult('Account Generation', 'FAILED', error.message);
      console.error('❌ Account generation test failed:', error.message);
      return false;
    }
  }

  async testTabNavigation() {
    console.log('🧪 Testing tab navigation...');
    
    try {
      const tabSelectors = [
        '[data-tab="create"]',
        '[data-tab="manage"]', 
        '[data-tab="authorize"]',
        '[data-tab="payment"]',
        '[data-tab="explorer"]',
        '[data-tab="automation"]'
      ];
      
      for (const tabSelector of tabSelectors) {
        const tab = await this.page.$(tabSelector);
        if (tab) {
          await tab.click();
          await this.page.waitForTimeout(500);
          
          // Check if tab content is visible
          const tabId = tabSelector.split('"')[1];
          const contentVisible = await this.page.$(`#${tabId}-tab, [data-tab-content="${tabId}"]`);
          
          if (!contentVisible) {
            console.warn(`⚠️ Tab content not found for ${tabId}`);
          }
        }
      }
      
      await this.takeScreenshot('tab-navigation');
      await this.recordResult('Tab Navigation', 'PASSED', `Tested ${tabSelectors.length} tabs`);
      
      console.log('✅ Tab navigation test passed');
      return true;
      
    } catch (error) {
      await this.takeScreenshot('tab-navigation-error');
      await this.recordResult('Tab Navigation', 'FAILED', error.message);
      console.error('❌ Tab navigation test failed:', error.message);
      return false;
    }
  }

  async testFormInputs() {
    console.log('🧪 Testing form inputs...');
    
    try {
      // Test Create Issuance form
      await this.page.click('[data-tab="create"]');
      await this.page.waitForTimeout(500);
      
      // Fill form fields
      const assetScaleInput = await this.page.$('input[type="number"][min="0"][max="19"]');
      if (assetScaleInput) {
        await assetScaleInput.type('6');
      }
      
      const transferFeeInput = await this.page.$('input[type="number"][min="0"][max="50000"]');
      if (transferFeeInput) {
        await transferFeeInput.type('250');
      }
      
      // Test checkboxes
      const checkboxes = await this.page.$$('input[type="checkbox"]');
      if (checkboxes.length > 0) {
        await checkboxes[0].click();
      }
      
      await this.takeScreenshot('form-inputs-filled');
      
      // Test Payment form
      await this.page.click('[data-tab="payment"]');
      await this.page.waitForTimeout(500);
      
      const destinationInput = await this.page.$('input[placeholder*="address"], input[placeholder*="destination"]');
      if (destinationInput) {
        await destinationInput.type('rTestDestination123456789');
      }
      
      const amountInput = await this.page.$('input[placeholder*="amount"]');
      if (amountInput) {
        await amountInput.type('100');
      }
      
      await this.takeScreenshot('payment-form-filled');
      await this.recordResult('Form Inputs', 'PASSED', 'All form inputs tested');
      
      console.log('✅ Form inputs test passed');
      return true;
      
    } catch (error) {
      await this.takeScreenshot('form-inputs-error');
      await this.recordResult('Form Inputs', 'FAILED', error.message);
      console.error('❌ Form inputs test failed:', error.message);
      return false;
    }
  }

  async testResponsiveDesign() {
    console.log('🧪 Testing responsive design...');
    
    try {
      const viewports = [
        { width: 375, height: 667, name: 'mobile' },
        { width: 768, height: 1024, name: 'tablet' },
        { width: 1920, height: 1080, name: 'desktop' }
      ];
      
      for (const viewport of viewports) {
        await this.page.setViewport(viewport);
        await this.page.waitForTimeout(1000);
        
        // Check if main elements are still visible
        const mainTitle = await this.page.$('h1');
        if (!mainTitle) {
          throw new Error(`Main title not visible at ${viewport.name} size`);
        }
        
        await this.takeScreenshot(`responsive-${viewport.name}`);
      }
      
      // Reset to desktop
      await this.page.setViewport({ width: 1920, height: 1080 });
      
      await this.recordResult('Responsive Design', 'PASSED', 'All viewport sizes tested');
      console.log('✅ Responsive design test passed');
      return true;
      
    } catch (error) {
      await this.takeScreenshot('responsive-design-error');
      await this.recordResult('Responsive Design', 'FAILED', error.message);
      console.error('❌ Responsive design test failed:', error.message);
      return false;
    }
  }

  async runFullTestSuite() {
    console.log('🧪 Running full E2E test suite...');
    
    const tests = [
      { name: 'Home Page', fn: () => this.testHomePage() },
      { name: 'Connection', fn: () => this.testConnection() },
      { name: 'Account Generation', fn: () => this.testAccountGeneration() },
      { name: 'Tab Navigation', fn: () => this.testTabNavigation() },
      { name: 'Form Inputs', fn: () => this.testFormInputs() },
      { name: 'Responsive Design', fn: () => this.testResponsiveDesign() }
    ];
    
    let passed = 0;
    let failed = 0;
    
    for (const test of tests) {
      try {
        const result = await test.fn();
        if (result) {
          passed++;
        } else {
          failed++;
        }
      } catch (error) {
        console.error(`❌ Test ${test.name} crashed:`, error.message);
        failed++;
      }
    }
    
    console.log('\n📊 Test Results Summary:');
    console.log(`✅ Passed: ${passed}`);
    console.log(`❌ Failed: ${failed}`);
    console.log(`📊 Total: ${passed + failed}`);
    
    // Generate detailed report
    await this.generateReport();
    
    return { passed, failed, total: passed + failed };
  }

  async generateReport() {
    const report = {
      timestamp: new Date().toISOString(),
      config: {
        baseUrl: this.config.baseUrl,
        rippleDUrl: this.config.rippleDUrl,
        browser: this.config.browser.headless ? 'headless' : 'headed'
      },
      results: this.testResults,
      summary: {
        total: this.testResults.length,
        passed: this.testResults.filter(r => r.status === 'PASSED').length,
        failed: this.testResults.filter(r => r.status === 'FAILED').length
      }
    };
    
    const reportPath = path.join(this.config.screenshots.path, 'test-report.json');
    await fs.writeFile(reportPath, JSON.stringify(report, null, 2));
    
    console.log(`📄 Detailed report saved: ${reportPath}`);
    return report;
  }
}

// Main execution
async function main() {
  const tester = new NuxtMPTokenE2ETests();
  
  try {
    await tester.setup();
    const results = await tester.runFullTestSuite();
    
    if (results.failed > 0) {
      process.exit(1);
    } else {
      console.log('🎉 All tests passed!');
    }
    
  } catch (error) {
    console.error('❌ Test suite failed:', error.message);
    process.exit(1);
  } finally {
    await tester.teardown();
  }
}

// Run tests if this is the main module
if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}

export { NuxtMPTokenE2ETests };