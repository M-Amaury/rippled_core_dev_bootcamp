<template>
  <div class="space-y-6">
    <!-- Authorization Form -->
    <div class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <div class="space-y-4">
        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            MPToken Issuance ID
          </label>
          <input
            v-model="form.issuanceId"
            type="text"
            placeholder="00000000... (MPToken Issuance ID)"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
        </div>

        <div>
          <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
            Holder Address (optional)
          </label>
          <input
            v-model="form.holderAddress"
            type="text"
            placeholder="rAddress... (leave empty for self-authorization)"
            class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
          />
          <p class="text-sm text-gray-500 dark:text-gray-400 mt-1">
            If empty, will authorize your own account ({{ xrplStore.currentAccount.address || 'No account loaded' }})
          </p>
        </div>

        <!-- Action Buttons -->
        <div class="flex space-x-4">
          <button
            @click="authorize"
            :disabled="!canSubmit || loading"
            class="flex-1 px-6 py-3 bg-green-600 hover:bg-green-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors flex items-center justify-center"
          >
            <Icon v-if="loading" name="heroicons:arrow-path" class="animate-spin mr-2" />
            <Icon v-else name="heroicons:check-circle" class="mr-2" />
            {{ loading ? 'Authorizing...' : 'Authorize' }}
          </button>
          
          <button
            @click="unauthorize"
            :disabled="!canSubmit || loading"
            class="flex-1 px-6 py-3 bg-red-600 hover:bg-red-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors flex items-center justify-center"
          >
            <Icon v-if="loading" name="heroicons:arrow-path" class="animate-spin mr-2" />
            <Icon v-else name="heroicons:x-circle" class="mr-2" />
            {{ loading ? 'Unauthorizing...' : 'Unauthorize' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Quick Actions -->
    <div class="bg-blue-50 dark:bg-blue-900/20 rounded-lg p-4 border border-blue-200 dark:border-blue-800">
      <h4 class="font-medium text-blue-800 dark:text-blue-300 mb-3">Quick Actions</h4>
      <div class="grid grid-cols-1 md:grid-cols-2 gap-3">
        <button
          @click="selfAuthorize"
          :disabled="!canSubmit || loading"
          class="px-4 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Self-Authorize Current Account
        </button>
        <button
          @click="checkAuthStatus"
          :disabled="!form.issuanceId || loading"
          class="px-4 py-2 bg-purple-600 hover:bg-purple-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
        >
          Check Authorization Status
        </button>
      </div>
    </div>

    <!-- Authorization Status -->
    <div v-if="authStatus" class="bg-white dark:bg-gray-700 rounded-lg p-6 border border-gray-200 dark:border-gray-600">
      <h4 class="font-semibold text-lg mb-4 flex items-center">
        <Icon name="heroicons:shield-check" class="mr-2 text-green-500" />
        Authorization Status
      </h4>
      
      <div class="space-y-3">
        <div class="flex justify-between items-center p-3 bg-gray-50 dark:bg-gray-600 rounded-lg">
          <span class="font-medium">Issuance ID:</span>
          <span class="font-mono text-sm">{{ authStatus.issuanceId }}</span>
        </div>
        
        <div class="flex justify-between items-center p-3 bg-gray-50 dark:bg-gray-600 rounded-lg">
          <span class="font-medium">Account:</span>
          <span class="font-mono text-sm">{{ authStatus.account }}</span>
        </div>
        
        <div class="flex justify-between items-center p-3 rounded-lg" :class="authStatus.authorized ? 'bg-green-50 dark:bg-green-900/20' : 'bg-red-50 dark:bg-red-900/20'">
          <span class="font-medium">Status:</span>
          <span :class="authStatus.authorized ? 'text-green-600 dark:text-green-400' : 'text-red-600 dark:text-red-400'">
            {{ authStatus.authorized ? '✅ Authorized' : '❌ Not Authorized' }}
          </span>
        </div>
      </div>
    </div>

    <!-- Help Section -->
    <div class="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-4 border border-yellow-200 dark:border-yellow-800">
      <h4 class="font-medium text-yellow-800 dark:text-yellow-300 mb-2">
        <Icon name="heroicons:light-bulb" class="inline mr-2" />
        How Authorization Works
      </h4>
      <ul class="text-sm text-yellow-700 dark:text-yellow-400 space-y-1">
        <li>• <strong>Authorize:</strong> Grants permission to hold/receive this MPToken</li>
        <li>• <strong>Unauthorize:</strong> Revokes permission (only issuer can do this)</li>
        <li>• <strong>Self-Authorization:</strong> Allows your account to receive the token</li>
        <li>• Some tokens require authorization before you can receive them</li>
      </ul>
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
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const form = ref({
  issuanceId: '',
  holderAddress: ''
})

const loading = ref(false)
const message = ref('')
const messageType = ref('info')
const authStatus = ref(null)

const canSubmit = computed(() => {
  return xrplStore.isConnected && 
         xrplStore.isAccountLoaded && 
         form.value.issuanceId.trim().length > 0
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
  }, 7000)
}

async function authorize() {
  if (!canSubmit.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenAuthorize',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: form.value.issuanceId,
      ...(form.value.holderAddress && { Holder: form.value.holderAddress })
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Authorization successful! Transaction: ${result.result.hash}`, 'success')
    
    // Update auth status
    await checkAuthStatus()
    
  } catch (error) {
    showMessage(`Authorization failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function unauthorize() {
  if (!canSubmit.value) return
  
  loading.value = true
  try {
    const transaction = {
      TransactionType: 'MPTokenAuthorize',
      Account: xrplStore.currentAccount.address,
      MPTokenIssuanceID: form.value.issuanceId,
      Flags: 1, // tfUnauthorize
      ...(form.value.holderAddress && { Holder: form.value.holderAddress })
    }
    
    const result = await submitTransaction(transaction)
    showMessage(`Unauthorization successful! Transaction: ${result.result.hash}`, 'success')
    
    // Update auth status
    await checkAuthStatus()
    
  } catch (error) {
    showMessage(`Unauthorization failed: ${error.message}`, 'error')
  } finally {
    loading.value = false
  }
}

async function selfAuthorize() {
  form.value.holderAddress = '' // Clear holder to self-authorize
  await authorize()
}

async function checkAuthStatus() {
  if (!form.value.issuanceId) return
  
  loading.value = true
  try {
    const targetAccount = form.value.holderAddress || xrplStore.currentAccount.address
    
    // Check if the account has an MPToken object for this issuance
    const response = await xrplStore.client.request({
      command: 'account_objects',
      account: targetAccount,
      type: 'mpt_issuance'
    })
    
    const isAuthorized = response.result.account_objects.some(obj => 
      obj.MPTokenIssuanceID === form.value.issuanceId
    )
    
    authStatus.value = {
      issuanceId: form.value.issuanceId,
      account: targetAccount,
      authorized: isAuthorized
    }
    
    showMessage(`Authorization status checked for ${targetAccount}`, 'info')
    
  } catch (error) {
    showMessage(`Failed to check authorization: ${error.message}`, 'error')
    authStatus.value = null
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

// Clear form
function resetForm() {
  form.value = {
    issuanceId: '',
    holderAddress: ''
  }
  authStatus.value = null
  message.value = ''
}
</script>