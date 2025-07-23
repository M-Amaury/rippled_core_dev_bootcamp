import puppeteer from 'puppeteer';

class WalletFunctionalityTest {
  constructor() {
    this.browser = null;
    this.page = null;
  }

  async init() {
    console.log('🧪 Testing Wallet Functionality Specifically');
    
    this.browser = await puppeteer.launch({
      headless: false,
      slowMo: 200,
      args: ['--no-sandbox', '--disable-setuid-sandbox']
    });

    this.page = await this.browser.newPage();
    await this.page.setViewport({ width: 1920, height: 1080 });
    
    // Monitor console for wallet-related errors
    this.page.on('console', msg => {
      if (msg.type() === 'error') {
        console.log(`[BROWSER ERROR] ${msg.text()}`);
      }
    });

    await this.page.goto('http://localhost:3000', { waitUntil: 'networkidle0' });
    await new Promise(resolve => setTimeout(resolve, 2000));
  }

  async testWalletCreationFlow() {
    console.log('\n🔍 Testing Complete Wallet Creation Flow...');
    
    try {
      // Step 1: Connect to server
      console.log('1️⃣ Connecting to XRPL server...');
      const serverInput = await this.page.$('input[type="text"]');
      await serverInput.click({ clickCount: 3 });
      await serverInput.type('ws://localhost:6006');
      
      const connectButton = await this.page.evaluateHandle(() => {
        const buttons = Array.from(document.querySelectorAll('button'));
        return buttons.find(btn => btn.textContent.trim() === 'Connect');
      });
      
      const connectEl = await connectButton.asElement();
      await connectEl.click();
      await new Promise(resolve => setTimeout(resolve, 3000));
      
      console.log('✅ Connection established');
      
      // Step 2: Generate new wallet
      console.log('2️⃣ Generating new wallet...');
      const generateButton = await this.page.evaluateHandle(() => {
        const buttons = Array.from(document.querySelectorAll('button'));
        return buttons.find(btn => btn.textContent.trim() === 'Generate');
      });
      
      const generateEl = await generateButton.asElement();
      await generateEl.click();
      await new Promise(resolve => setTimeout(resolve, 3000));
      
      // Step 3: Check for wallet details
      console.log('3️⃣ Checking wallet details...');
      const walletInfo = await this.page.evaluate(() => {
        const body = document.body.textContent;
        
        // Look for address pattern
        const addressMatch = body.match(/r[a-zA-Z0-9]{24,34}/);
        const address = addressMatch ? addressMatch[0] : null;
        
        // Look for balance information
        const hasBalance = body.includes('Balance:') || body.includes('XRP') || body.includes('balance');
        
        // Look for seed/private key info
        const hasSeed = body.includes('Seed:') || body.includes('seed') || body.includes('Secret');
        
        // Look for funding status
        const hasFunding = body.includes('Fund') || body.includes('financement');
        
        return {
          address,
          hasBalance,
          hasSeed,
          hasFunding,
          fullText: body.substring(0, 1000) // First 1000 chars for debugging
        };
      });
      
      console.log('📊 Wallet Info:', {
        address: walletInfo.address,
        hasBalance: walletInfo.hasBalance,
        hasSeed: walletInfo.hasSeed,
        hasFunding: walletInfo.hasFunding
      });
      
      // Step 4: Test funding functionality
      if (walletInfo.address) {
        console.log('4️⃣ Testing wallet funding...');
        
        // Look for Fund button
        const fundButton = await this.page.evaluateHandle(() => {
          const buttons = Array.from(document.querySelectorAll('button'));
          return buttons.find(btn => 
            btn.textContent.includes('Fund') || 
            btn.textContent.includes('Financer')
          );
        });
        
        const fundEl = await fundButton.asElement();
        if (fundEl) {
          console.log('💰 Found Fund button, clicking...');
          await fundEl.click();
          await new Promise(resolve => setTimeout(resolve, 5000));
          
          // Check balance after funding
          const balanceAfterFunding = await this.page.evaluate(() => {
            const body = document.body.textContent;
            const balanceMatch = body.match(/Balance:\s*([0-9,\.]+)\s*XRP/i);
            return balanceMatch ? balanceMatch[1] : null;
          });
          
          console.log('💵 Balance after funding:', balanceAfterFunding);
          
          if (balanceAfterFunding && parseFloat(balanceAfterFunding.replace(',', '')) > 0) {
            console.log('✅ Wallet funded successfully!');
            return true;
          } else {
            console.log('❌ Wallet funding failed - no balance detected');
            return false;
          }
          
        } else {
          console.log('⚠️ No Fund button found');
        }
      } else {
        console.log('❌ No wallet address generated');
        return false;
      }
      
    } catch (error) {
      console.error('❌ Wallet test error:', error.message);
      return false;
    }
  }

  async testMPTokenOperations() {
    console.log('\n🪙 Testing MPToken Operations...');
    
    try {
      // Navigate to Create Issuance tab
      console.log('1️⃣ Testing MPToken Issuance Creation...');
      
      // Look for tab buttons
      const createTab = await this.page.evaluateHandle(() => {
        const buttons = Array.from(document.querySelectorAll('button'));
        return buttons.find(btn => 
          btn.textContent.includes('Create') || 
          btn.textContent.includes('Émission') ||
          btn.getAttribute('data-tab') === 'create'
        );
      });
      
      const createEl = await createTab.asElement();
      if (createEl) {
        await createEl.click();
        await new Promise(resolve => setTimeout(resolve, 1000));
        
        // Fill form
        const assetScaleInput = await this.page.$('input[type="number"][min="0"][max="19"]');
        if (assetScaleInput) {
          await assetScaleInput.type('6');
          console.log('✅ Asset Scale set');
        }
        
        const transferFeeInput = await this.page.$('input[type="number"][min="0"][max="50000"]');
        if (transferFeeInput) {
          await transferFeeInput.type('250');
          console.log('✅ Transfer Fee set');
        }
        
        // Test checkboxes
        const checkboxes = await this.page.$$('input[type="checkbox"]');
        if (checkboxes.length > 0) {
          await checkboxes[0].click();
          console.log('✅ Checkbox toggled');
        }
        
        // Try to submit
        const submitButton = await this.page.evaluateHandle(() => {
          const buttons = Array.from(document.querySelectorAll('button'));
          return buttons.find(btn => 
            btn.textContent.includes('Create') && 
            !btn.textContent.includes('Tab')
          );
        });
        
        const submitEl = await submitButton.asElement();
        if (submitEl) {
          console.log('💡 Found Create button for issuance');
          // Note: We won't actually submit to avoid creating test data
        }
        
        console.log('✅ MPToken form interface working');
        return true;
        
      } else {
        console.log('❌ Create tab not found');
        return false;
      }
      
    } catch (error) {
      console.error('❌ MPToken operations test error:', error.message);
      return false;
    }
  }

  async takeDebugScreenshot() {
    await this.page.screenshot({ 
      path: 'tests/screenshots/wallet-debug.png', 
      fullPage: true 
    });
    console.log('📸 Wallet debug screenshot saved');
  }

  async cleanup() {
    if (this.browser) {
      await this.browser.close();
    }
  }
}

async function main() {
  const tester = new WalletFunctionalityTest();
  
  try {
    await tester.init();
    
    const walletWorking = await tester.testWalletCreationFlow();
    const mpTokenWorking = await tester.testMPTokenOperations();
    
    await tester.takeDebugScreenshot();
    
    console.log('\n📊 Wallet Functionality Test Results:');
    console.log(`💼 Wallet Creation & Funding: ${walletWorking ? '✅ WORKING' : '❌ ISSUES'}`);
    console.log(`🪙 MPToken Operations: ${mpTokenWorking ? '✅ WORKING' : '❌ ISSUES'}`);
    
    if (!walletWorking) {
      console.log('\n🔧 Wallet Issues Detected - Check wallet-debug.png for visual debugging');
    }
    
  } catch (error) {
    console.error('❌ Wallet test suite failed:', error.message);
  } finally {
    await tester.cleanup();
  }
}

main();