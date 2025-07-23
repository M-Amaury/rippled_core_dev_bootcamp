<template>
  <div class="space-y-6">
    <!-- Account Explorer -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:user-circle" class="mr-2 text-blue-500" />
        Account MPTokens
      </h4>
      
      <div class="flex space-x-3 mb-4">
        <input
          v-model="accountAddress"
          type="text"
          placeholder="rAddress... (account to explore)"
          class="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
        <button
          @click="loadAccountTokens"
          :disabled="!accountAddress || loading"
          class="px-6 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          {{ loading ? 'Loading...' : 'Explore' }}
        </button>
        <button
          @click="useCurrentAccount"
          :disabled="!xrplStore.isAccountLoaded"
          class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Use Current
        </button>
      </div>

      <!-- Account Tokens -->
      <div v-if="accountTokens" class="space-y-4">
        <div class="flex items-center justify-between">
          <h5 class="font-medium">MPTokens ({{ accountTokens.length }})</h5>
          <button
            @click="refreshAccountTokens"
            class="text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300 text-sm"
          >
            <Icon name="heroicons:arrow-path" class="inline mr-1" />
            Refresh
          </button>
        </div>
        
        <div v-if="accountTokens.length === 0" class="text-center py-8 text-gray-500 dark:text-gray-400">
          No MPTokens found for this account
        </div>
        
        <div v-else class="grid grid-cols-1 gap-3">
          <div
            v-for="token in accountTokens"
            :key="token.issuanceId"
            class="p-4 border border-gray-200 dark:border-gray-600 rounded-lg hover:bg-gray-50 dark:hover:bg-gray-600 transition-colors"
          >
            <div class="flex items-center justify-between">
              <div class="flex-1">
                <p class="font-medium">{{ token.issuanceId }}</p>
                <p class="text-sm text-gray-500 dark:text-gray-400">Balance: {{ formatAmount(token.balance) }}</p>
                <p class="text-sm text-gray-500 dark:text-gray-400">Locked: {{ formatAmount(token.lockedAmount) }}</p>
              </div>
              <div class="flex space-x-2">
                <button
                  @click="viewIssuanceDetails(token.issuanceId)"
                  class="px-3 py-1 bg-blue-600 hover:bg-blue-700 text-white rounded text-sm transition-colors"
                >
                  Details
                </button>
                <button
                  @click="copyToClipboard(token.issuanceId)"
                  class="px-3 py-1 bg-gray-500 hover:bg-gray-600 text-white rounded text-sm transition-colors"
                >
                  Copy ID
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Issuance Explorer -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:document-magnifying-glass" class="mr-2 text-green-500" />
        Issuance Lookup
      </h4>
      
      <div class="flex space-x-3 mb-4">
        <input
          v-model="issuanceId"
          type="text"
          placeholder="00000000... (MPToken Issuance ID)"
          class="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
        <button
          @click="loadIssuanceDetails"
          :disabled="!issuanceId || loading"
          class="px-6 py-2 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          {{ loading ? 'Loading...' : 'Lookup' }}
        </button>
      </div>

      <!-- Issuance Details -->
      <div v-if="issuanceDetails" class="space-y-4">
        <div class="flex items-center justify-between">
          <h5 class="font-medium">Issuance Information</h5>
          <div class="flex space-x-2">
            <button
              @click="refreshIssuanceDetails"
              class="text-green-600 hover:text-green-800 dark:text-green-400 dark:hover:text-green-300 text-sm"
            >
              <Icon name="heroicons:arrow-path" class="inline mr-1" />
              Refresh
            </button>
            <button
              @click="copyIssuanceData"
              class="text-blue-600 hover:text-blue-800 dark:text-blue-400 dark:hover:text-blue-300 text-sm"
            >
              <Icon name="heroicons:clipboard" class="inline mr-1" />
              Copy Data
            </button>
          </div>
        </div>
        
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          <div class="p-4 bg-gray-50 dark:bg-gray-600 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Issuer</p>
            <p class="font-mono text-sm break-all">{{ issuanceDetails.Issuer }}</p>
          </div>
          
          <div class="p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Outstanding Amount</p>
            <p class="font-bold text-lg text-blue-600 dark:text-blue-400">{{ formatAmount(issuanceDetails.OutstandingAmount) }}</p>
          </div>
          
          <div class="p-4 bg-purple-50 dark:bg-purple-900/20 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Maximum Amount</p>
            <p class="font-bold text-lg text-purple-600 dark:text-purple-400">{{ formatAmount(issuanceDetails.MaximumAmount) || 'Unlimited' }}</p>
          </div>
          
          <div class="p-4 bg-yellow-50 dark:bg-yellow-900/20 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Transfer Fee</p>
            <p class="font-bold text-lg text-yellow-600 dark:text-yellow-400">{{ issuanceDetails.TransferFee || 0 }} bp</p>
          </div>
          
          <div class="p-4 bg-green-50 dark:bg-green-900/20 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Asset Scale</p>
            <p class="font-bold text-lg text-green-600 dark:text-green-400">{{ issuanceDetails.AssetScale || 0 }}</p>
          </div>
          
          <div class="p-4 bg-red-50 dark:bg-red-900/20 rounded-lg">
            <p class="text-sm text-gray-600 dark:text-gray-400">Flags</p>
            <p class="font-bold text-sm text-red-600 dark:text-red-400">{{ formatFlags(issuanceDetails.Flags) }}</p>
          </div>
        </div>

        <!-- Metadata -->
        <div v-if="issuanceDetails.Metadata" class="p-4 bg-gray-50 dark:bg-gray-600 rounded-lg">
          <p class="text-sm text-gray-600 dark:text-gray-400 mb-2">Metadata</p>
          <p class="font-mono text-sm break-all">{{ issuanceDetails.Metadata }}</p>
          <p class="text-xs text-gray-500 dark:text-gray-400 mt-1">
            Decoded: {{ decodeMetadata(issuanceDetails.Metadata) }}
          </p>
        </div>

        <!-- Holders -->
        <div class="p-4 bg-white dark:bg-gray-700 border border-gray-200 dark:border-gray-600 rounded-lg">
          <div class="flex items-center justify-between mb-3">
            <h6 class="font-medium">Token Holders</h6>
            <button
              @click="loadHolders"
              :disabled="loading"
              class="px-3 py-1 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded text-sm transition-colors"
            >
              Load Holders
            </button>
          </div>
          
          <div v-if="holders && holders.length > 0" class="space-y-2">
            <div
              v-for="holder in holders"
              :key="holder.account"
              class="flex items-center justify-between p-3 bg-gray-50 dark:bg-gray-600 rounded"
            >
              <span class="font-mono text-sm">{{ holder.account }}</span>
              <span class="font-medium">{{ formatAmount(holder.balance) }}</span>
            </div>
          </div>
          
          <div v-else-if="holders" class="text-center py-4 text-gray-500 dark:text-gray-400">
            No holders found
          </div>
        </div>
      </div>
    </div>

    <!-- Network Statistics -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:chart-bar" class="mr-2 text-purple-500" />
        Network Statistics
      </h4>
      
      <div class="grid grid-cols-1 md:grid-cols-4 gap-4">
        <div class="p-4 bg-blue-50 dark:bg-blue-900/20 rounded-lg text-center">
          <p class="text-2xl font-bold text-blue-600 dark:text-blue-400">{{ networkStats.totalIssuances }}</p>
          <p class="text-sm text-gray-600 dark:text-gray-400">Total Issuances</p>
        </div>
        
        <div class="p-4 bg-green-50 dark:bg-green-900/20 rounded-lg text-center">
          <p class="text-2xl font-bold text-green-600 dark:text-green-400">{{ networkStats.totalHolders }}</p>
          <p class="text-sm text-gray-600 dark:text-gray-400">Total Holders</p>
        </div>
        
        <div class="p-4 bg-purple-50 dark:bg-purple-900/20 rounded-lg text-center">
          <p class="text-2xl font-bold text-purple-600 dark:text-purple-400">{{ networkStats.totalVolume }}</p>
          <p class="text-sm text-gray-600 dark:text-gray-400">Total Volume</p>
        </div>
        
        <div class="p-4 bg-yellow-50 dark:bg-yellow-900/20 rounded-lg text-center">
          <p class="text-2xl font-bold text-yellow-600 dark:text-yellow-400">{{ networkStats.activeIssuances }}</p>
          <p class="text-sm text-gray-600 dark:text-gray-400">Active Issuances</p>
        </div>
      </div>
      
      <div class="mt-4 flex justify-center">
        <button
          @click="loadNetworkStats"
          :disabled="loading"
          class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          {{ loading ? 'Loading...' : 'Refresh Stats' }}
        </button>
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
import { ref, computed, onMounted } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const accountAddress = ref('')
const issuanceId = ref('')
const accountTokens = ref(null)
const issuanceDetails = ref(null)
const holders = ref(null)
const loading = ref(false)
const message = ref('')
const messageType = ref('info')

const networkStats = ref({
  totalIssuances: '-',
  totalHolders: '-',
  totalVolume: '-',
  activeIssuances: '-'
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

function useCurrentAccount() {
  if (xrplStore.currentAccount.address) {
    accountAddress.value = xrplStore.currentAccount.address
  }
}

async function loadAccountTokens() {
  if (!accountAddress.value) return
  
  loading.value = true
  try {
    const response = await xrplStore.client.request({
      command: 'account_objects',
      account: accountAddress.value,
      type: 'mpt_issuance'
    })
    
    accountTokens.value = response.result.account_objects.map(obj => ({
      issuanceId: obj.MPTokenIssuanceID,
      balance: obj.MPTAmount || '0',
      lockedAmount: obj.LockedAmount || '0',
      flags: obj.Flags || 0
    }))
    
    showMessage(`Found ${accountTokens.value.length} MPTokens`, 'success')
    
  } catch (error) {
    showMessage(`Failed to load account tokens: ${error.message}`, 'error')
    accountTokens.value = []
  } finally {
    loading.value = false
  }
}

async function refreshAccountTokens() {
  await loadAccountTokens()
}

async function loadIssuanceDetails() {
  if (!issuanceId.value) return
  
  loading.value = true
  try {
    const response = await xrplStore.client.request({
      command: 'ledger_entry',
      mpt_issuance_id: issuanceId.value
    })
    
    if (response.result && response.result.node) {
      issuanceDetails.value = response.result.node
      holders.value = null // Reset holders when loading new issuance
      showMessage('Issuance details loaded successfully', 'success')
    } else {
      throw new Error('Issuance not found')
    }
    
  } catch (error) {
    showMessage(`Failed to load issuance: ${error.message}`, 'error')
    issuanceDetails.value = null
  } finally {
    loading.value = false
  }
}

async function refreshIssuanceDetails() {
  await loadIssuanceDetails()
}

async function viewIssuanceDetails(id) {
  issuanceId.value = id
  await loadIssuanceDetails()
}

async function loadHolders() {
  if (!issuanceId.value) return
  
  loading.value = true
  try {
    // This is a simplified approach - in reality, you'd need to scan through accounts
    // or use a more sophisticated indexing system
    const response = await xrplStore.client.request({
      command: 'ledger_data',
      type: 'mpt_issuance'
    })
    
    // Filter and process holders
    holders.value = []
    showMessage('Holder scanning is simplified in this demo', 'info')
    
  } catch (error) {
    showMessage(`Failed to load holders: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function loadNetworkStats() {
  loading.value = true
  try {
    // Simulate network statistics
    networkStats.value = {
      totalIssuances: Math.floor(Math.random() * 1000),
      totalHolders: Math.floor(Math.random() * 5000),
      totalVolume: (Math.random() * 1000000).toFixed(0),
      activeIssuances: Math.floor(Math.random() * 500)
    }
    
    showMessage('Network statistics updated (simulated)', 'success')
    
  } catch (error) {
    showMessage(`Failed to load network stats: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

function copyToClipboard(text) {
  navigator.clipboard.writeText(text).then(() => {
    showMessage('Copied to clipboard!', 'success')
  }).catch(() => {
    showMessage('Failed to copy to clipboard', 'error')
  })
}

function copyIssuanceData() {
  if (issuanceDetails.value) {
    const data = JSON.stringify(issuanceDetails.value, null, 2)
    copyToClipboard(data)
  }
}

function formatAmount(amount) {
  if (!amount || amount === '0') return '0'
  return new Intl.NumberFormat().format(amount)
}

function formatFlags(flags) {
  if (!flags) return 'None'
  
  const flagNames = []
  if (flags & 0x00000001) flagNames.push('Can Lock')
  if (flags & 0x00000002) flagNames.push('Require Auth')
  if (flags & 0x00000004) flagNames.push('Can Escrow')
  if (flags & 0x00000008) flagNames.push('Can Trade')
  if (flags & 0x00000010) flagNames.push('Can Transfer')
  if (flags & 0x00000020) flagNames.push('Can Clawback')
  
  return flagNames.length > 0 ? flagNames.join(', ') : 'None'
}

function decodeMetadata(hex) {
  try {
    if (hex.startsWith('0x')) hex = hex.slice(2)
    return Buffer.from(hex, 'hex').toString('utf8')
  } catch {
    return 'Unable to decode'
  }
}

// Load initial data
onMounted(() => {
  loadNetworkStats()
  if (xrplStore.currentAccount.address) {
    useCurrentAccount()
  }
})
</script>