<template>
  <div class="space-y-6">
    <!-- Load Issuance Section -->
    <div class="bg-gray-50 dark:bg-gray-800 rounded-lg p-4">
      <h4 class="font-medium mb-3">Load Existing Issuance</h4>
      <div class="flex space-x-3">
        <input
          v-model="issuanceId"
          type="text"
          placeholder="MPToken Issuance ID (starts with 00000...)"
          class="flex-1 px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
        <button
          @click="loadIssuance"
          :disabled="!issuanceId || loading"
          class="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          {{ loading ? 'Loading...' : 'Load' }}
        </button>
      </div>
    </div>

    <!-- Issuance Details -->
    <div v-if="issuanceDetails" class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:information-circle" class="mr-2 text-blue-500" />
        Issuance Details
      </h4>
      
      <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Issuer</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ issuanceDetails.Issuer }}</p>
        </div>
        
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Outstanding Amount</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ formatAmount(issuanceDetails.OutstandingAmount) }}</p>
        </div>
        
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Maximum Amount</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ formatAmount(issuanceDetails.MaximumAmount) || 'Unlimited' }}</p>
        </div>
        
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Transfer Fee</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ issuanceDetails.TransferFee || 0 }} basis points</p>
        </div>
        
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Asset Scale</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ issuanceDetails.AssetScale || 0 }}</p>
        </div>
        
        <div>
          <label class="block text-sm font-medium text-gray-600 dark:text-gray-400">Flags</label>
          <p class="font-mono text-sm bg-gray-100 dark:bg-gray-600 p-2 rounded">{{ formatFlags(issuanceDetails.Flags) }}</p>
        </div>
      </div>
    </div>

    <!-- Management Actions -->
    <div v-if="issuanceDetails" class="space-y-4">
      <h4 class="font-semibold text-lg flex items-center">
        <Icon name="heroicons:cog-6-tooth" class="mr-2 text-purple-500" />
        Management Actions
      </h4>

      <!-- Authorize/Unauthorize Section -->
      <div class="bg-white dark:bg-gray-700 rounded-lg p-4 border border-gray-200 dark:border-gray-600">
        <h5 class="font-medium mb-3">Authorization Management</h5>
        <div class="space-y-3">
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Holder Address (optional - leave empty for self)
            </label>
            <input
              v-model="holderAddress"
              type="text"
              placeholder="rAddress... (leave empty to authorize yourself)"
              class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
            />
          </div>
          <div class="flex space-x-3">
            <button
              @click="authorizeHolder"
              :disabled="!canManage || loading"
              class="px-4 py-2 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
            >
              Authorize
            </button>
            <button
              @click="unauthorizeHolder"
              :disabled="!canManage || loading"
              class="px-4 py-2 bg-red-600 hover:bg-red-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
            >
              Unauthorize
            </button>
          </div>
        </div>
      </div>

      <!-- Lock/Unlock Tokens -->
      <div class="bg-white dark:bg-gray-700 rounded-lg p-4 border border-gray-200 dark:border-gray-600">
        <h5 class="font-medium mb-3">Token Management</h5>
        <div class="space-y-3">
          <div>
            <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
              Amount to Lock/Unlock
            </label>
            <input
              v-model="lockAmount"
              type="text"
              placeholder="Amount (e.g., 1000)"
              class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
            />
          </div>
          <div class="flex space-x-3">
            <button
              @click="lockTokens"
              :disabled="!canManage || !lockAmount || loading"
              class="px-4 py-2 bg-orange-600 hover:bg-orange-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
            >
              Lock Tokens
            </button>
            <button
              @click="unlockTokens"
              :disabled="!canManage || !lockAmount || loading"
              class="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
            >
              Unlock Tokens
            </button>
          </div>
        </div>
      </div>

      <!-- Dangerous Actions -->
      <div class="bg-red-50 dark:bg-red-900/20 rounded-lg p-4 border border-red-200 dark:border-red-800">
        <h5 class="font-medium mb-3 text-red-800 dark:text-red-300">Dangerous Actions</h5>
        <div class="space-y-3">
          <button
            @click="destroyIssuance"
            :disabled="!canDestroy || loading"
            class="px-4 py-2 bg-red-600 hover:bg-red-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
          >
            🗑️ Destroy Issuance (PERMANENT)
          </button>
          <p class="text-sm text-red-600 dark:text-red-400">
            This action is permanent and cannot be undone. Only works if outstanding amount is 0.
          </p>
        </div>
      </div>
    </div>

    <!-- Error/Success Messages -->
    <div v-if="message" :class="messageClass" class="p-4 rounded-lg">
      {{ message }}
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const issuanceId = ref('')
const issuanceDetails = ref(null)
const holderAddress = ref('')
const lockAmount = ref('')
const loading = ref(false)
const message = ref('')
const messageType = ref('info')

const canManage = computed(() => {
  return xrplStore.isConnected && 
         xrplStore.isAccountLoaded && 
         issuanceDetails.value &&
         issuanceDetails.value.Issuer === xrplStore.currentAccount.address
})

const canDestroy = computed(() => {
  return canManage.value && 
         (!issuanceDetails.value.OutstandingAmount || issuanceDetails.value.OutstandingAmount === '0')
})

const messageClass = computed(() => {
  const base = 'border'
  switch (messageType.value) {
    case 'success': return `${base} bg-green-50 border-green-200 text-green-800 dark:bg-green-900/20 dark:border-green-800 dark:text-green-300`
    case 'error': return `${base} bg-red-50 border-red-200 text-red-800 dark:bg-red-900/20 dark:border-red-800 dark:text-red-300`
    default: return `${base} bg-blue-50 border-blue-200 text-blue-800 dark:bg-blue-900/20 dark:border-blue-800 dark:text-blue-300`
  }
})

function showMessage(text, type = 'info') {
  message.value = text
  messageType.value = type
  setTimeout(() => {
    message.value = ''
  }, 5000)
}

async function loadIssuance() {
  if (!issuanceId.value) return
  
  loading.value = true
  try {
    const response = await xrplStore.client.request({
      command: 'ledger_entry',
      mpt_issuance_id: issuanceId.value
    })
    
    if (response.result && response.result.node) {
      issuanceDetails.value = response.result.node
      showMessage('Issuance loaded successfully', 'success')
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

async function authorizeHolder() {
  if (!canManage.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenAuthorize',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: issuanceId.value,
      ...(holderAddress.value && { Holder: holderAddress.value })
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Authorization successful! Hash: ${result.result.hash}`, 'success')
  } catch (error) {
    showMessage(`Authorization failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function unauthorizeHolder() {
  if (!canManage.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenAuthorize',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: issuanceId.value,
      Flags: 1, // tfUnauthorize flag
      ...(holderAddress.value && { Holder: holderAddress.value })
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Unauthorization successful! Hash: ${result.result.hash}`, 'success')
  } catch (error) {
    showMessage(`Unauthorization failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function lockTokens() {
  if (!canManage.value || !lockAmount.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenIssuanceSet',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: issuanceId.value,
      Flags: 1 // tfMPTLock
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Tokens locked successfully! Hash: ${result.result.hash}`, 'success')
    await loadIssuance() // Refresh details
  } catch (error) {
    showMessage(`Lock failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function unlockTokens() {
  if (!canManage.value || !lockAmount.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenIssuanceSet',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: issuanceId.value,
      Flags: 2 // tfMPTUnlock
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Tokens unlocked successfully! Hash: ${result.result.hash}`, 'success')
    await loadIssuance() // Refresh details
  } catch (error) {
    showMessage(`Unlock failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function destroyIssuance() {
  if (!canDestroy.value) return
  
  if (!confirm('Are you sure you want to destroy this issuance? This action is PERMANENT and cannot be undone!')) {
    return
  }
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenIssuanceDestroy',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: issuanceId.value
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Issuance destroyed! Hash: ${result.result.hash}`, 'success')
    
    // Clear the details
    issuanceDetails.value = null
    issuanceId.value = ''
  } catch (error) {
    showMessage(`Destroy failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function submitTransaction(transaction) {
  const prepared = await xrplStore.client.autofill(transaction)
  const signed = xrplStore.currentAccount.wallet.sign(prepared)
  const result = await xrplStore.client.submitAndWait(signed.tx_blob)
  
  // Add to transaction log
  xrplStore.addTransaction({
    id: result.result.hash,
    type: transaction.TransactionType,
    status: result.result.meta.TransactionResult,
    timestamp: new Date(),
    details: { transaction: prepared, result }
  })
  
  await xrplStore.refreshAccountInfo()
  return result
}

function formatAmount(amount) {
  if (!amount) return '0'
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
</script>