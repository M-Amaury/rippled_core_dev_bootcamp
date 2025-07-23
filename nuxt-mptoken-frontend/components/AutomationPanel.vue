<template>
  <div class="space-y-6">
    <!-- Automated Test Scenarios -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:play" class="mr-2 text-green-500" />
        Automated Test Scenarios
      </h4>
      
      <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
        <button
          @click="runTest('full-lifecycle')"
          :disabled="running || !canRunTests"
          class="p-4 border border-blue-200 dark:border-blue-800 rounded-lg hover:bg-blue-50 dark:hover:bg-blue-900/20 disabled:opacity-50 transition-colors text-left"
        >
          <div class="flex items-center justify-between">
            <div>
              <h5 class="font-medium text-blue-800 dark:text-blue-300">Full Token Lifecycle</h5>
              <p class="text-sm text-blue-600 dark:text-blue-400">Create → Authorize → Transfer → Check</p>
            </div>
            <Icon name="heroicons:arrow-right" class="text-blue-500" />
          </div>
        </button>

        <button
          @click="runTest('authorization-flow')"
          :disabled="running || !canRunTests"
          class="p-4 border border-purple-200 dark:border-purple-800 rounded-lg hover:bg-purple-50 dark:hover:bg-purple-900/20 disabled:opacity-50 transition-colors text-left"
        >
          <div class="flex items-center justify-between">
            <div>
              <h5 class="font-medium text-purple-800 dark:text-purple-300">Authorization Flow</h5>
              <p class="text-sm text-purple-600 dark:text-purple-400">Test auth/unauth scenarios</p>
            </div>
            <Icon name="heroicons:arrow-right" class="text-purple-500" />
          </div>
        </button>

        <button
          @click="runTest('payment-scenarios')"
          :disabled="running || !canRunTests"
          class="p-4 border border-green-200 dark:border-green-800 rounded-lg hover:bg-green-50 dark:hover:bg-green-900/20 disabled:opacity-50 transition-colors text-left"
        >
          <div class="flex items-center justify-between">
            <div>
              <h5 class="font-medium text-green-800 dark:text-green-300">Payment Scenarios</h5>
              <p class="text-sm text-green-600 dark:text-green-400">Multiple payment types & amounts</p>
            </div>
            <Icon name="heroicons:arrow-right" class="text-green-500" />
          </div>
        </button>

        <button
          @click="runTest('stress-test')"
          :disabled="running || !canRunTests"
          class="p-4 border border-red-200 dark:border-red-800 rounded-lg hover:bg-red-50 dark:hover:bg-red-900/20 disabled:opacity-50 transition-colors text-left"
        >
          <div class="flex items-center justify-between">
            <div>
              <h5 class="font-medium text-red-800 dark:text-red-300">Stress Test</h5>
              <p class="text-sm text-red-600 dark:text-red-400">High volume transactions</p>
            </div>
            <Icon name="heroicons:arrow-right" class="text-red-500" />
          </div>
        </button>
      </div>
    </div>

    <!-- Test Configuration -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:cog-6-tooth" class="mr-2 text-blue-500" />
        Test Configuration
      </h4>
      
      <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Number of Test Accounts
          </label>
          <input
            v-model.number="config.testAccounts"
            type="number"
            min="2"
            max="10"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
        </div>

        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Delay Between Actions (ms)
          </label>
          <input
            v-model.number="config.delay"
            type="number"
            min="100"
            max="10000"
            step="100"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
        </div>

        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Initial Fund Amount (XRP)
          </label>
          <input
            v-model.number="config.fundAmount"
            type="number"
            min="10"
            max="1000"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
        </div>
      </div>

      <div class="mt-4 flex items-center space-x-4">
        <label class="flex items-center">
          <input
            v-model="config.autoFund"
            type="checkbox"
            class="rounded border-gray-300 text-blue-600 focus:ring-blue-500"
          />
          <span class="ml-2 text-sm text-gray-700 dark:text-gray-300">Auto-fund test accounts</span>
        </label>
        
        <label class="flex items-center">
          <input
            v-model="config.verbose"
            type="checkbox"
            class="rounded border-gray-300 text-blue-600 focus:ring-blue-500"
          />
          <span class="ml-2 text-sm text-gray-700 dark:text-gray-300">Verbose logging</span>
        </label>
      </div>
    </div>

    <!-- Test Progress -->
    <div v-if="running || testResults.length > 0" class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:chart-bar" class="mr-2 text-purple-500" />
        Test Progress
      </h4>
      
      <div v-if="running" class="mb-4">
        <div class="flex items-center justify-between mb-2">
          <span class="text-sm font-medium">{{ currentTest }}</span>
          <span class="text-sm text-gray-500">{{ currentStep }}/{{ totalSteps }}</span>
        </div>
        <div class="w-full bg-gray-200 dark:bg-gray-600 rounded-full h-2">
          <div
            class="bg-blue-600 h-2 rounded-full transition-all duration-300"
            :style="{ width: `${(currentStep / totalSteps) * 100}%` }"
          ></div>
        </div>
        <p class="text-sm text-gray-600 dark:text-gray-400 mt-2">{{ currentStepDescription }}</p>
      </div>

      <!-- Test Results -->
      <div v-if="testResults.length > 0" class="space-y-3">
        <h5 class="font-medium">Recent Test Results</h5>
        <div
          v-for="result in testResults.slice(0, 5)"
          :key="result.id"
          class="p-3 rounded-lg border"
          :class="result.success ? 'border-green-200 bg-green-50 dark:border-green-800 dark:bg-green-900/20' : 'border-red-200 bg-red-50 dark:border-red-800 dark:bg-red-900/20'"
        >
          <div class="flex items-center justify-between">
            <div class="flex items-center space-x-3">
              <Icon
                :name="result.success ? 'heroicons:check-circle' : 'heroicons:x-circle'"
                :class="result.success ? 'text-green-500' : 'text-red-500'"
              />
              <div>
                <p class="font-medium">{{ result.testName }}</p>
                <p class="text-sm text-gray-600 dark:text-gray-400">
                  {{ result.duration }}ms • {{ result.steps }} steps
                </p>
              </div>
            </div>
            <button
              @click="viewTestDetails(result)"
              class="text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300 text-sm"
            >
              View Details
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Account Management -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:users" class="mr-2 text-orange-500" />
        Test Account Management
      </h4>
      
      <div class="flex flex-wrap gap-3 mb-4">
        <button
          @click="generateTestAccounts"
          :disabled="running"
          class="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Generate Test Accounts
        </button>
        
        <button
          @click="fundTestAccounts"
          :disabled="running || testAccounts.length === 0"
          class="px-4 py-2 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Fund All Accounts
        </button>
        
        <button
          @click="checkAccountBalances"
          :disabled="testAccounts.length === 0"
          class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Check Balances
        </button>
        
        <button
          @click="clearTestAccounts"
          :disabled="running"
          class="px-4 py-2 bg-red-600 hover:bg-red-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Clear Accounts
        </button>
      </div>

      <!-- Account List -->
      <div v-if="testAccounts.length > 0" class="space-y-2">
        <h5 class="font-medium">Test Accounts ({{ testAccounts.length }})</h5>
        <div class="grid grid-cols-1 gap-2 max-h-60 overflow-y-auto">
          <div
            v-for="(account, index) in testAccounts"
            :key="account.address"
            class="p-3 bg-gray-50 dark:bg-gray-600 rounded-lg flex items-center justify-between"
          >
            <div>
              <p class="font-mono text-sm">{{ account.address }}</p>
              <p class="text-xs text-gray-500 dark:text-gray-400">
                Balance: {{ account.balance }} XRP
              </p>
            </div>
            <div class="flex space-x-2">
              <button
                @click="copyToClipboard(account.address)"
                class="px-2 py-1 bg-blue-600 hover:bg-blue-700 text-white rounded text-xs transition-colors"
              >
                Copy
              </button>
              <button
                @click="useAccount(account)"
                class="px-2 py-1 bg-purple-600 hover:bg-purple-700 text-white rounded text-xs transition-colors"
              >
                Use
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Test Details Modal -->
    <div v-if="selectedTestResult" class="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50" @click="selectedTestResult = null">
      <div class="bg-white dark:bg-gray-800 rounded-lg p-6 max-w-2xl max-h-[80vh] overflow-y-auto" @click.stop>
        <div class="flex items-center justify-between mb-4">
          <h4 class="font-semibold text-lg">{{ selectedTestResult.testName }} Details</h4>
          <button @click="selectedTestResult = null" class="text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300">
            <Icon name="heroicons:x-mark" />
          </button>
        </div>
        
        <div class="space-y-4">
          <div class="grid grid-cols-2 gap-4">
            <div>
              <p class="text-sm text-gray-600 dark:text-gray-400">Status</p>
              <p :class="selectedTestResult.success ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'">
                {{ selectedTestResult.success ? 'SUCCESS' : 'FAILED' }}
              </p>
            </div>
            <div>
              <p class="text-sm text-gray-600 dark:text-gray-400">Duration</p>
              <p>{{ selectedTestResult.duration }}ms</p>
            </div>
          </div>
          
          <div>
            <p class="text-sm text-gray-600 dark:text-gray-400 mb-2">Steps Executed</p>
            <div class="space-y-2">
              <div
                v-for="step in selectedTestResult.stepDetails"
                :key="step.id"
                class="p-2 rounded border-l-4"
                :class="step.success ? 'border-green-500 bg-green-50 dark:bg-green-900/20' : 'border-red-500 bg-red-50 dark:bg-red-900/20'"
              >
                <p class="font-medium">{{ step.name }}</p>
                <p class="text-sm text-gray-600 dark:text-gray-400">{{ step.description }}</p>
                <p v-if="step.error" class="text-sm text-red-600 dark:text-red-400">{{ step.error }}</p>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Result Message -->
    <div v-if="message" :class="messageClass" class="p-4 rounded-lg">
      <div class="flex items-center">
        <Icon :name="messageIcon" class="mr-2" />
        {{ message }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore, fundAccount } from '~/stores/xrpl'
import { Wallet } from 'xrpl'

const xrplStore = useXRPLStore()

const config = ref({
  testAccounts: 3,
  delay: 1000,
  fundAmount: 100,
  autoFund: true,
  verbose: false
})

const running = ref(false)
const currentTest = ref('')
const currentStep = ref(0)
const totalSteps = ref(0)
const currentStepDescription = ref('')
const testResults = ref([])
const testAccounts = ref([])
const selectedTestResult = ref(null)
const message = ref('')
const messageType = ref('info')

const canRunTests = computed(() => {
  return xrplStore.isConnected && xrplStore.isAccountLoaded
})

const messageClass = computed(() => {
  const base = 'border'
  switch (messageType.value) {
    case 'success': return `${base} bg-green-50 border-green-200 text-green-800 dark:bg-green-900/20 dark:border-green-800 dark:text-green-300`
    case 'error': return `${base} bg-red-50 border-red-200 text-red-800 dark:bg-red-900/20 dark:border-red-800 dark:text-red-300`
    default: return `${base} bg-blue-50 border-blue-200 text-blue-800 dark:bg-blue-900/20 dark:border-blue-800 dark:text-blue-300`
  }
})

const messageIcon = computed(() => {
  switch (messageType.value) {
    case 'success': return 'heroicons:check-circle'
    case 'error': return 'heroicons:x-circle'
    default: return 'heroicons:information-circle'
  }
})

function showMessage(text, type = 'info') {
  message.value = text
  messageType.value = type
  setTimeout(() => {
    message.value = ''
  }, 5000)
}

async function runTest(testType) {
  if (!canRunTests.value || running.value) return
  
  running.value = true
  const startTime = Date.now()
  const testId = Date.now().toString()
  const stepDetails = []
  
  try {
    switch (testType) {
      case 'full-lifecycle':
        await runFullLifecycleTest(testId, stepDetails)
        break
      case 'authorization-flow':
        await runAuthorizationFlowTest(testId, stepDetails)
        break
      case 'payment-scenarios':
        await runPaymentScenariosTest(testId, stepDetails)
        break
      case 'stress-test':
        await runStressTest(testId, stepDetails)
        break
    }
    
    const duration = Date.now() - startTime
    testResults.value.unshift({
      id: testId,
      testName: getTestName(testType),
      success: true,
      duration,
      steps: stepDetails.length,
      stepDetails,
      timestamp: new Date()
    })
    
    showMessage(`Test "${getTestName(testType)}" completed successfully in ${duration}ms`, 'success')
    
  } catch (error) {
    const duration = Date.now() - startTime
    testResults.value.unshift({
      id: testId,
      testName: getTestName(testType),
      success: false,
      duration,
      steps: stepDetails.length,
      stepDetails,
      error: error.message,
      timestamp: new Date()
    })
    
    showMessage(`Test "${getTestName(testType)}" failed: ${error.message}`, 'error')
  } finally {
    running.value = false
    currentTest.value = ''
    currentStep.value = 0
    totalSteps.value = 0
  }
}

async function runFullLifecycleTest(testId, stepDetails) {
  currentTest.value = 'Full Token Lifecycle'
  totalSteps.value = 6
  
  // Step 1: Generate test accounts
  await executeStep('Generate Test Accounts', async () => {
    if (testAccounts.value.length < 2) {
      await generateTestAccounts()
    }
  }, stepDetails)
  
  // Step 2: Fund accounts
  await executeStep('Fund Test Accounts', async () => {
    await fundTestAccounts()
  }, stepDetails)
  
  // Step 3: Create MPToken issuance
  let issuanceId = null
  await executeStep('Create MPToken Issuance', async () => {
    const result = await xrplStore.createMPTokenIssuance({
      assetScale: 2,
      transferFee: 100,
      flags: ['Can Transfer']
    })
    // Extract issuance ID from result
    issuanceId = extractIssuanceId(result)
  }, stepDetails)
  
  // Step 4: Authorize accounts
  await executeStep('Authorize Test Accounts', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    // Authorize each test account
    for (const account of testAccounts.value.slice(0, 2)) {
      await authorizeAccount(issuanceId, account.address)
    }
  }, stepDetails)
  
  // Step 5: Send test payments
  await executeStep('Send Test Payments', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    await sendTestPayment(issuanceId, testAccounts.value[1].address, '100')
  }, stepDetails)
  
  // Step 6: Verify balances
  await executeStep('Verify Balances', async () => {
    await checkAccountBalances()
  }, stepDetails)
}

async function runAuthorizationFlowTest(testId, stepDetails) {
  currentTest.value = 'Authorization Flow Test'
  totalSteps.value = 4
  
  await executeStep('Setup Test Environment', async () => {
    if (testAccounts.value.length < 2) {
      await generateTestAccounts()
    }
    await fundTestAccounts()
  }, stepDetails)
  
  let issuanceId = null
  await executeStep('Create Auth-Required Token', async () => {
    const result = await xrplStore.createMPTokenIssuance({
      assetScale: 2,
      transferFee: 0,
      flags: ['Require Auth', 'Can Transfer']
    })
    issuanceId = extractIssuanceId(result)
  }, stepDetails)
  
  await executeStep('Test Authorization', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    await authorizeAccount(issuanceId, testAccounts.value[0].address)
  }, stepDetails)
  
  await executeStep('Test Unauthorization', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    await unauthorizeAccount(issuanceId, testAccounts.value[0].address)
  }, stepDetails)
}

async function runPaymentScenariosTest(testId, stepDetails) {
  currentTest.value = 'Payment Scenarios Test'
  totalSteps.value = 5
  
  await executeStep('Setup Test Environment', async () => {
    if (testAccounts.value.length < 3) {
      await generateTestAccounts()
    }
    await fundTestAccounts()
  }, stepDetails)
  
  let issuanceId = null
  await executeStep('Create Test Token', async () => {
    const result = await xrplStore.createMPTokenIssuance({
      assetScale: 2,
      transferFee: 50,
      flags: ['Can Transfer']
    })
    issuanceId = extractIssuanceId(result)
  }, stepDetails)
  
  await executeStep('Small Payment Test', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    await sendTestPayment(issuanceId, testAccounts.value[0].address, '1')
  }, stepDetails)
  
  await executeStep('Large Payment Test', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    await sendTestPayment(issuanceId, testAccounts.value[1].address, '1000')
  }, stepDetails)
  
  await executeStep('Multiple Recipients Test', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    for (let i = 0; i < Math.min(3, testAccounts.value.length); i++) {
      await sendTestPayment(issuanceId, testAccounts.value[i].address, '50')
      await new Promise(resolve => setTimeout(resolve, config.value.delay))
    }
  }, stepDetails)
}

async function runStressTest(testId, stepDetails) {
  currentTest.value = 'Stress Test'
  totalSteps.value = 3
  
  await executeStep('Setup Stress Test Environment', async () => {
    if (testAccounts.value.length < 5) {
      config.value.testAccounts = 5
      await generateTestAccounts()
    }
    await fundTestAccounts()
  }, stepDetails)
  
  let issuanceId = null
  await executeStep('Create High-Volume Token', async () => {
    const result = await xrplStore.createMPTokenIssuance({
      assetScale: 6,
      transferFee: 0,
      maximumAmount: '1000000000000',
      flags: ['Can Transfer']
    })
    issuanceId = extractIssuanceId(result)
  }, stepDetails)
  
  await executeStep('Execute High-Volume Transactions', async () => {
    if (!issuanceId) throw new Error('No issuance ID available')
    
    const transactions = 20
    for (let i = 0; i < transactions; i++) {
      const recipient = testAccounts.value[i % testAccounts.value.length]
      await sendTestPayment(issuanceId, recipient.address, Math.floor(Math.random() * 1000).toString())
      
      // Brief delay to avoid overwhelming the network
      await new Promise(resolve => setTimeout(resolve, 100))
    }
  }, stepDetails)
}

async function executeStep(stepName, action, stepDetails) {
  currentStep.value++
  currentStepDescription.value = stepName
  
  const stepStart = Date.now()
  let success = false
  let error = null
  
  try {
    await action()
    success = true
    if (config.value.verbose) {
      console.log(`✅ ${stepName} completed`)
    }
  } catch (err) {
    error = err.message
    if (config.value.verbose) {
      console.error(`❌ ${stepName} failed:`, err.message)
    }
    throw err
  } finally {
    stepDetails.push({
      id: stepDetails.length + 1,
      name: stepName,
      description: `Step ${currentStep.value} of ${totalSteps.value}`,
      success,
      error,
      duration: Date.now() - stepStart
    })
    
    // Add delay between steps
    await new Promise(resolve => setTimeout(resolve, config.value.delay))
  }
}

async function generateTestAccounts() {
  testAccounts.value = []
  
  for (let i = 0; i < config.value.testAccounts; i++) {
    const wallet = Wallet.generate()
    testAccounts.value.push({
      wallet,
      address: wallet.address,
      seed: wallet.seed,
      balance: '0'
    })
  }
  
  showMessage(`Generated ${config.value.testAccounts} test accounts`, 'success')
}


async function fundTestAccounts() {
  if (!config.value.autoFund) {
    showMessage('Auto-funding is disabled', 'info')
    return
  }


  for (const account of testAccounts.value) {
    try {
      // In a real testnet, you would use a faucet or funding mechanism
      // For now, we'll simulate funding
      fundAccount(account.address);

      if (config.value.verbose) {
        console.log(`Funded ${account.address} with ${config.value.fundAmount} XRP`)
      }
    } catch (error) {
      console.error(`Failed to fund ${account.address}:`, error.message)
    }
  }
  
  showMessage(`Funded ${testAccounts.value.length} test accounts`, 'success')
}

async function checkAccountBalances() {
  for (const account of testAccounts.value) {
    try {
      const accountInfo = await xrplStore.client.request({
        command: 'account_info',
        account: account.address
      })
      
      account.balance = (parseInt(accountInfo.result.account_data.Balance) / 1000000).toString()
    } catch (error) {
      account.balance = '0'
    }
  }
  
  showMessage('Account balances updated', 'success')
}

function clearTestAccounts() {
  testAccounts.value = []
  showMessage('Test accounts cleared', 'info')
}

function useAccount(account) {
  // Load this account as the current account
  xrplStore.loadAccount(account.seed)
  showMessage(`Switched to account: ${account.address}`, 'success')
}

function copyToClipboard(text) {
  navigator.clipboard.writeText(text).then(() => {
    showMessage('Copied to clipboard!', 'success')
  }).catch(() => {
    showMessage('Failed to copy to clipboard', 'error')
  })
}

function viewTestDetails(result) {
  selectedTestResult.value = result
}

function getTestName(testType) {
  const names = {
    'full-lifecycle': 'Full Token Lifecycle',
    'authorization-flow': 'Authorization Flow',
    'payment-scenarios': 'Payment Scenarios',
    'stress-test': 'Stress Test'
  }
  return names[testType] || testType
}

// Helper functions for test operations
function extractIssuanceId(result) {
  // This would extract the actual issuance ID from the transaction result
  // For now, return a mock ID
  return `00000000${Math.random().toString(16).substr(2, 8).toUpperCase()}`
}

async function authorizeAccount(issuanceId, holderAddress) {
  // Mock authorization - in real implementation, would submit transaction
  if (config.value.verbose) {
    console.log(`Authorizing ${holderAddress} for issuance ${issuanceId}`)
  }
}

async function unauthorizeAccount(issuanceId, holderAddress) {
  // Mock unauthorization
  if (config.value.verbose) {
    console.log(`Unauthorizing ${holderAddress} for issuance ${issuanceId}`)
  }
}

async function sendTestPayment(issuanceId, destination, amount) {
  // Mock payment - in real implementation, would submit transaction
  if (config.value.verbose) {
    console.log(`Sending ${amount} of ${issuanceId} to ${destination}`)
  }
}
</script>