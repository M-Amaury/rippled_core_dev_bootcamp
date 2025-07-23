<template>
  <div class="min-h-screen bg-gradient-to-br from-blue-50 to-purple-50 dark:from-gray-900 dark:to-gray-800">
    <div class="container mx-auto px-4 py-8">
      <!-- Header -->
      <header class="text-center mb-8">
        <h1 class="text-4xl font-bold text-gray-800 dark:text-white mb-2">
          🪙 MPToken Testing Interface
        </h1>
        <p class="text-lg text-gray-600 dark:text-gray-300">
          Advanced testing tool for XRP Ledger MPToken functionality
        </p>
        <div class="mt-4 flex items-center justify-center space-x-4">
          <div class="flex items-center space-x-2">
            <div 
              :class="[
                'w-3 h-3 rounded-full',
                connectionStatusColor
              ]"
            ></div>
            <span class="text-sm font-medium" :class="connectionStatusTextColor">
              {{ xrplStore.connectionStatus.toUpperCase() }}
            </span>
          </div>
          <div class="text-sm text-gray-500">
            Server: {{ xrplStore.serverUrl }}
          </div>
        </div>
      </header>

      <!-- Connection Panel -->
      <div class="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mb-6">
        <h2 class="text-xl font-semibold mb-4 flex items-center">
          <Icon name="heroicons:wifi" class="mr-2" />
          Connection Settings
        </h2>
        <div class="flex items-center space-x-4">
          <input
            v-model="serverUrl"
            type="text"
            placeholder="WebSocket URL"
            class="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
          <button
            @click="handleConnection"
            :disabled="xrplStore.connectionStatus === 'connecting'"
            class="px-6 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
          >
            {{ xrplStore.isConnected ? 'Disconnect' : 'Connect' }}
          </button>
        </div>
      </div>

      <!-- Account Panel -->
      <div class="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mb-6">
        <h2 class="text-xl font-semibold mb-4 flex items-center">
          <Icon name="heroicons:user" class="mr-2" />
          Account Management
        </h2>
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <div>
            <div class="flex items-center space-x-4 mb-4">
              <input
                v-model="accountSeed"
                type="text"
                placeholder="Account seed (auto-generated if empty)"
                class="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
              />
              <button
                @click="generateAccount"
                :disabled="!xrplStore.isConnected"
                class="px-4 py-2 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
              >
                Generate
              </button>
              <button
                @click="loadAccount"
                :disabled="!accountSeed || !xrplStore.isConnected"
                class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
              >
                Load
              </button>
            </div>
          </div>
          <div class="space-y-2">
            <div class="flex justify-between">
              <span class="font-medium">Address:</span>
              <span class="text-sm font-mono break-all">{{ xrplStore.currentAccount.address || '-' }}</span>
            </div>
            <div class="flex justify-between">
              <span class="font-medium">Balance:</span>
              <span class="font-mono" :class="balanceClass">{{ xrplStore.formattedBalance }}</span>
            </div>
            <div class="flex justify-between">
              <span class="font-medium">Sequence:</span>
              <span class="font-mono">{{ xrplStore.currentAccount.sequence }}</span>
            </div>
            <div class="flex justify-between">
              <span class="font-medium">Account Status:</span>
              <span :class="accountStatusClass">{{ accountStatusText }}</span>
            </div>
          </div>
        </div>
        
        <!-- Funding Panel -->
        <div v-if="xrplStore.isAccountLoaded" class="mt-6">
          <FundingPanel />
        </div>
      </div>

      <!-- Main Tabs -->
      <div class="bg-white dark:bg-gray-800 rounded-lg shadow-lg overflow-hidden">
        <!-- Tab Navigation -->
        <div class="border-b border-gray-200 dark:border-gray-700">
          <nav class="flex space-x-8 px-6">
            <button
              v-for="tab in tabs"
              :key="tab.id"
              @click="activeTab = tab.id"
              :class="[
                'py-4 px-2 border-b-2 font-medium text-sm transition-colors',
                activeTab === tab.id
                  ? 'border-blue-500 text-blue-600 dark:text-blue-400'
                  : 'border-transparent text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-300'
              ]"
            >
              <Icon :name="tab.icon" class="inline mr-2" />
              {{ tab.name }}
            </button>
          </nav>
        </div>

        <!-- Tab Content -->
        <div class="p-6">
          <!-- Create Issuance Tab -->
          <div v-show="activeTab === 'create'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">Create MPToken Issuance</h3>
            <CreateIssuanceForm />
          </div>

          <!-- Manage Issuance Tab -->
          <div v-show="activeTab === 'manage'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">Manage MPToken Issuance</h3>
            <ManageIssuanceForm />
          </div>

          <!-- Authorize Tab -->
          <div v-show="activeTab === 'authorize'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">Authorization Management</h3>
            <AuthorizeForm />
          </div>

          <!-- Payment Tab -->
          <div v-show="activeTab === 'payment'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">MPToken Payments</h3>
            <PaymentForm />
          </div>

          <!-- Explorer Tab -->
          <div v-show="activeTab === 'explorer'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">MPToken Explorer</h3>
            <ExplorerPanel />
          </div>

          <!-- Automation Tab -->
          <div v-show="activeTab === 'automation'" class="space-y-6">
            <h3 class="text-lg font-semibold mb-4">Automation & Testing</h3>
            <AutomationPanel />
          </div>
        </div>
      </div>

      <!-- Transaction Log -->
      <div class="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mt-6">
        <div class="flex items-center justify-between mb-4">
          <h2 class="text-xl font-semibold flex items-center">
            <Icon name="heroicons:clipboard-document-list" class="mr-2" />
            Transaction Log
          </h2>
          <div class="flex space-x-2">
            <button
              @click="xrplStore.clearTransactions"
              class="px-4 py-2 bg-gray-500 hover:bg-gray-600 text-white rounded-lg font-medium transition-colors"
            >
              Clear
            </button>
            <button
              @click="xrplStore.exportTransactions"
              class="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-colors"
            >
              Export
            </button>
          </div>
        </div>
        <TransactionLog />
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const serverUrl = ref('ws://localhost:6006')
const accountSeed = ref('')
const activeTab = ref('create')

const tabs = [
  { id: 'create', name: 'Create Issuance', icon: 'heroicons:plus-circle' },
  { id: 'manage', name: 'Manage', icon: 'heroicons:cog-6-tooth' },
  { id: 'authorize', name: 'Authorize', icon: 'heroicons:lock-closed' },
  { id: 'payment', name: 'Payment', icon: 'heroicons:banknotes' },
  { id: 'explorer', name: 'Explorer', icon: 'heroicons:magnifying-glass' },
  { id: 'automation', name: 'Automation', icon: 'heroicons:play' }
]

const connectionStatusColor = computed(() => {
  switch (xrplStore.connectionStatus) {
    case 'connected': return 'bg-green-500'
    case 'connecting': return 'bg-yellow-500'
    case 'error': return 'bg-red-500'
    default: return 'bg-gray-500'
  }
})

const connectionStatusTextColor = computed(() => {
  switch (xrplStore.connectionStatus) {
    case 'connected': return 'text-green-600 dark:text-green-400'
    case 'connecting': return 'text-yellow-600 dark:text-yellow-400'
    case 'error': return 'text-red-600 dark:text-red-400'
    default: return 'text-gray-600 dark:text-gray-400'
  }
})

const balanceClass = computed(() => {
  const balance = parseFloat(xrplStore.currentAccount.balance)
  if (balance === 0) return 'text-gray-600 dark:text-gray-400'
  if (balance < 10) return 'text-red-600 dark:text-red-400'
  if (balance < 100) return 'text-yellow-600 dark:text-yellow-400'
  return 'text-green-600 dark:text-green-400'
})

const accountStatusText = computed(() => {
  if (!xrplStore.isAccountLoaded) return 'No Account'
  const balance = parseFloat(xrplStore.currentAccount.balance)
  if (balance === 0) return 'Needs Funding'
  if (balance < 10) return 'Low Balance'
  return 'Ready'
})

const accountStatusClass = computed(() => {
  if (!xrplStore.isAccountLoaded) return 'text-gray-600 dark:text-gray-400'
  const balance = parseFloat(xrplStore.currentAccount.balance)
  if (balance === 0) return 'text-red-600 dark:text-red-400'
  if (balance < 10) return 'text-yellow-600 dark:text-yellow-400'
  return 'text-green-600 dark:text-green-400'
})

async function handleConnection() {
  if (xrplStore.isConnected) {
    await xrplStore.disconnect()
  } else {
    try {
      await xrplStore.connectToServer(serverUrl.value)
    } catch (error) {
      console.error('Connection failed:', error)
    }
  }
}

async function generateAccount() {
  try {
    const wallet = await xrplStore.generateAccount()
    accountSeed.value = wallet.seed
  } catch (error) {
    console.error('Failed to generate account:', error)
  }
}

async function loadAccount() {
  try {
    await xrplStore.loadAccount(accountSeed.value)
  } catch (error) {
    console.error('Failed to load account:', error)
  }
}

// Auto-refresh account info periodically
onMounted(() => {
  const interval = setInterval(async () => {
    if (xrplStore.isConnected && xrplStore.isAccountLoaded) {
      await xrplStore.refreshAccountInfo()
    }
  }, 10000) // Every 10 seconds

  onUnmounted(() => {
    clearInterval(interval)
  })
})
</script>