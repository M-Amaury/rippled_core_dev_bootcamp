module.exports = {
  // Test configuration
  testTimeout: 30000,
  
  // Browser configuration
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
  
  // Test environment
  baseUrl: process.env.TEST_BASE_URL || 'http://localhost:3001',
  rippleDUrl: process.env.RIPPLED_URL || 'ws://localhost:6006',
  
  // Screenshots and videos
  screenshots: {
    enabled: true,
    path: './tests/screenshots',
    onFailure: true,
    onSuccess: false
  },
  
  // Test data
  testAccounts: {
    master: {
      seed: 'sEdTM1uX8pu2do5XvTnutH6HsouMaM2',
      address: 'rLNaPoKeeBjZe2qs6x52yVPZpZ8td4dc6w'
    },
    test1: {
      seed: 'sEdSJHS4oiAdz7w2X2ni1gFiqtbJHqE',
      address: 'rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH'
    },
    test2: {
      seed: 'sEd7rBGm5kxzauRTAV2hbsNz7N46twN',
      address: 'rDNvEQSzCkqMx1nFJwg4EYtaTNZLf99Kn5'
    }
  }
};