import puppeteer from 'puppeteer';

class DebugTest {
  constructor() {
    this.browser = null;
    this.page = null;
  }

  async init() {
    console.log('🔍 Starting Debug Test for MPToken Interface');
    
    this.browser = await puppeteer.launch({
      headless: false, // Show browser for debugging
      slowMo: 100,
      devtools: true,
      args: ['--no-sandbox', '--disable-setuid-sandbox']
    });

    this.page = await this.browser.newPage();
    await this.page.setViewport({ width: 1920, height: 1080 });
    
    // Setup console logging
    this.page.on('console', msg => {
      console.log(`[BROWSER] ${msg.type()}: ${msg.text()}`);
    });

    // Setup error handling
    this.page.on('pageerror', error => {
      console.error(`[PAGE ERROR] ${error.message}`);
    });

    // Load the interface
    console.log('📡 Navigating to http://localhost:3001');
    await this.page.goto('http://localhost:3001', { waitUntil: 'networkidle0' });
    
    console.log('✅ Page loaded successfully');
  }

  async diagnoseInterface() {
    console.log('\n🔍 Diagnosing Interface...');
    
    // Check page title
    const title = await this.page.title();
    console.log(`📝 Page title: "${title}"`);
    
    // Check if main content is loaded
    const mainContent = await this.page.$('body');
    if (mainContent) {
      console.log('✅ Main body element found');
    } else {
      console.log('❌ Main body element not found');
    }
    
    // Check for h1 element
    const h1 = await this.page.$('h1');
    if (h1) {
      const h1Text = await this.page.evaluate(el => el.textContent, h1);
      console.log(`✅ H1 found: "${h1Text}"`);
    } else {
      console.log('❌ H1 element not found');
    }
    
    // Check for connection elements
    const serverInput = await this.page.$('input[type="text"]');
    if (serverInput) {
      const placeholder = await serverInput.getAttribute('placeholder');
      console.log(`✅ Input found with placeholder: "${placeholder}"`);
    } else {
      console.log('❌ No input elements found');
    }
    
    // Find all buttons
    const buttons = await this.page.$$('button');
    console.log(`🔘 Found ${buttons.length} buttons:`);
    for (let i = 0; i < Math.min(buttons.length, 10); i++) {
      const buttonText = await this.page.evaluate(el => el.textContent, buttons[i]);
      const buttonClasses = await this.page.evaluate(el => el.className, buttons[i]);
      console.log(`  ${i + 1}. "${buttonText.trim()}" (classes: ${buttonClasses})`);
    }
    
    // Check for tab elements
    const tabs = await this.page.$$('[data-tab]');
    console.log(`📑 Found ${tabs.length} elements with data-tab attribute`);
    
    // Check for any Vue/Nuxt specific indicators
    const vueApps = await this.page.$$('[data-nuxt-component]');
    console.log(`🖼️ Found ${vueApps.length} Nuxt components`);
    
    // Check for loading states
    const loadingElements = await this.page.$$('[class*="loading"]');
    console.log(`⏳ Found ${loadingElements.length} loading elements`);
    
    // Check for error messages
    const errorElements = await this.page.$$('[class*="error"]');
    console.log(`❌ Found ${errorElements.length} error elements`);
  }

  async testConnection() {
    console.log('\n🔗 Testing Connection Functionality...');
    
    try {
      // Look for server input more specifically
      const serverInput = await this.page.$('input[placeholder*="WebSocket"], input[value*="ws://"], input[placeholder*="URL"]');
      if (serverInput) {
        console.log('✅ Found server input field');
        await serverInput.click();
        await serverInput.evaluate(el => el.value = '');
        await serverInput.type('ws://localhost:6006');
        console.log('✅ Server URL entered');
      } else {
        console.log('❌ Server input field not found');
      }
      
      // Look for connect button more specifically
      const connectButton = await this.page.evaluateHandle(() => {
        const buttons = Array.from(document.querySelectorAll('button'));
        return buttons.find(btn => btn.textContent.toLowerCase().includes('connect'));
      });
      
      const connectButtonElement = await connectButton.asElement();
      if (connectButtonElement) {
        console.log('✅ Found connect button');
        await connectButtonElement.click();
        console.log('✅ Connect button clicked');
        
        // Wait a bit and check for status changes
        await this.page.waitForTimeout(3000);
        
        // Look for connection status
        const statusText = await this.page.evaluate(() => {
          const elements = Array.from(document.querySelectorAll('*'));
          const statusEl = elements.find(el => 
            el.textContent.toLowerCase().includes('connected') || 
            el.textContent.toLowerCase().includes('connecting') ||
            el.textContent.toLowerCase().includes('disconnected')
          );
          return statusEl ? statusEl.textContent : null;
        });
        
        if (statusText) {
          console.log(`📊 Connection status: ${statusText}`);
        } else {
          console.log('⚠️ No connection status element found');
        }
        
      } else {
        console.log('❌ Connect button not found');
      }
      
    } catch (error) {
      console.error(`❌ Connection test error: ${error.message}`);
    }
  }

  async testAccountGeneration() {
    console.log('\n👤 Testing Account Generation...');
    
    try {
      // Look for generate button
      const generateButton = await this.page.evaluateHandle(() => {
        const buttons = Array.from(document.querySelectorAll('button'));
        return buttons.find(btn => btn.textContent.toLowerCase().includes('generate'));
      });
      
      const generateButtonElement = await generateButton.asElement();
      if (generateButtonElement) {
        console.log('✅ Found generate button');
        await generateButtonElement.click();
        console.log('✅ Generate button clicked');
        
        // Wait for account generation
        await this.page.waitForTimeout(2000);
        
        // Look for account address
        const addressElements = await this.page.$$eval('*', elements => 
          elements.filter(el => 
            el.textContent.match(/r[a-zA-Z0-9]{24,34}/) || 
            el.textContent.includes('Address:')
          )
        );
        
        console.log(`📧 Found ${addressElements.length} potential address elements`);
        
        // Look for balance information
        const balanceElements = await this.page.$$eval('*', elements => 
          elements.filter(el => 
            el.textContent.toLowerCase().includes('balance') ||
            el.textContent.includes('XRP')
          )
        );
        
        console.log(`💰 Found ${balanceElements.length} potential balance elements`);
        
      } else {
        console.log('❌ Generate button not found');
      }
      
    } catch (error) {
      console.error(`❌ Account generation test error: ${error.message}`);
    }
  }

  async inspectWalletFunctionality() {
    console.log('\n🔍 Inspecting Wallet Functionality...');
    
    try {
      // Check for wallet-related elements
      const walletElements = await this.page.$$eval('*', elements => 
        elements.filter(el => 
          el.textContent.toLowerCase().includes('wallet') ||
          el.textContent.toLowerCase().includes('seed') ||
          el.textContent.toLowerCase().includes('account')
        ).slice(0, 10) // Limit to first 10
      );
      
      console.log(`👛 Found ${walletElements.length} wallet-related elements`);
      
      // Check for funding elements
      const fundingElements = await this.page.$$eval('*', elements => 
        elements.filter(el => 
          el.textContent.toLowerCase().includes('fund') ||
          el.textContent.toLowerCase().includes('balance') ||
          el.textContent.toLowerCase().includes('xrp')
        ).slice(0, 10)
      );
      
      console.log(`💸 Found ${fundingElements.length} funding-related elements`);
      
      // Check for error messages
      const errorMessages = await this.page.$$eval('*', elements => 
        elements.filter(el => 
          el.textContent.toLowerCase().includes('error') ||
          el.textContent.toLowerCase().includes('failed') ||
          el.textContent.toLowerCase().includes('not found')
        ).slice(0, 5)
      );
      
      console.log(`⚠️ Found ${errorMessages.length} error message elements`);
      
      // Check browser console for errors
      console.log('\n📋 Browser Console Logs (last few):');
      
    } catch (error) {
      console.error(`❌ Wallet inspection error: ${error.message}`);
    }
  }

  async takeDebugScreenshot() {
    await this.page.screenshot({ 
      path: 'tests/screenshots/debug-full-page.png', 
      fullPage: true 
    });
    console.log('📸 Debug screenshot saved: debug-full-page.png');
  }

  async cleanup() {
    if (this.browser) {
      await this.browser.close();
    }
  }
}

// Main execution
async function main() {
  const debugTest = new DebugTest();
  
  try {
    await debugTest.init();
    await debugTest.diagnoseInterface();
    await debugTest.testConnection();
    await debugTest.testAccountGeneration();
    await debugTest.inspectWalletFunctionality();
    await debugTest.takeDebugScreenshot();
    
    console.log('\n🎉 Debug test completed! Check the debug-full-page.png screenshot.');
    
  } catch (error) {
    console.error('❌ Debug test failed:', error.message);
  } finally {
    await debugTest.cleanup();
  }
}

main();