<template>
  <div class="bg-yellow-50 dark:bg-yellow-900/20 rounded-lg p-4 border border-yellow-200 dark:border-yellow-800">
    <h4 class="font-medium text-yellow-800 dark:text-yellow-300 mb-3 flex items-center">
      <Icon name="heroicons:banknotes" class="mr-2" />
      Account Funding
    </h4>
    
    <div class="space-y-3">
      <div class="flex items-center justify-between">
        <span class="text-sm text-yellow-700 dark:text-yellow-400">Current Balance:</span>
        <span class="font-bold text-yellow-800 dark:text-yellow-300">{{ xrplStore.formattedBalance }}</span>
      </div>
      
      <div v-if="needsFunding" class="p-3 bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 rounded-lg">
        <p class="text-sm text-red-800 dark:text-red-300 mb-2">
          ⚠️ Your account needs XRP to send transactions (minimum 10 XRP recommended)
        </p>
        <button
          @click="fundCurrentAccount"
          :disabled="funding"
          class="px-4 py-2 bg-red-600 hover:bg-red-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors flex items-center"
        >
          <Icon v-if="funding" name="heroicons:arrow-path" class="animate-spin mr-2" />
          <Icon v-else name="heroicons:plus" class="mr-2" />
          {{ funding ? 'Funding...' : 'Fund Account (1000 XRP)' }}
        </button>
      </div>
      
      <div v-else class="p-3 bg-green-50 dark:bg-green-900/20 border border-green-200 dark:border-green-800 rounded-lg">
        <p class="text-sm text-green-800 dark:text-green-300">
          ✅ Account has sufficient XRP for transactions
        </p>
      </div>

      <!-- Fund Other Account -->
      <div class="border-t border-yellow-200 dark:border-yellow-700 pt-3">
        <h5 class="text-sm font-medium text-yellow-800 dark:text-yellow-300 mb-2">Fund Another Account</h5>
        <div class="flex space-x-2">
          <input
            v-model="targetAddress"
            type="text"
            placeholder="rAddress..."
            class="flex-1 px-3 py-2 text-sm border border-yellow-300 dark:border-yellow-600 rounded-lg focus:ring-2 focus:ring-yellow-500 dark:bg-yellow-900/10 dark:text-yellow-100"
          />
          <input
            v-model="fundAmount"
            type="number"
            placeholder="1000"
            min="10"
            max="10000"
            class="w-20 px-3 py-2 text-sm border border-yellow-300 dark:border-yellow-600 rounded-lg focus:ring-2 focus:ring-yellow-500 dark:bg-yellow-900/10 dark:text-yellow-100"
          />
          <button
            @click="fundTargetAccount"
            :disabled="!targetAddress || !fundAmount || funding"
            class="px-3 py-2 bg-yellow-600 hover:bg-yellow-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors text-sm"
          >
            Fund
          </button>
        </div>
      </div>

      <!-- Recent Funding Transactions -->
      <div v-if="recentFundings.length > 0" class="border-t border-yellow-200 dark:border-yellow-700 pt-3">
        <h5 class="text-sm font-medium text-yellow-800 dark:text-yellow-300 mb-2">Recent Fundings</h5>
        <div class="space-y-1">
          <div
            v-for="funding in recentFundings.slice(0, 3)"
            :key="funding.id"
            class="flex items-center justify-between text-xs text-yellow-700 dark:text-yellow-400"
          >
            <span class="font-mono">{{ funding.destination.substring(0, 15) }}...</span>
            <span>{{ funding.amount }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Message -->
    <div v-if="message" class="mt-3 p-2 rounded-lg text-sm" :class="messageClass">
      {{ message }}
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const funding = ref(false)
const targetAddress = ref('')
const fundAmount = ref(1000)
const message = ref('')
const messageType = ref('info')

const needsFunding = computed(() => {
  const balance = parseFloat(xrplStore.currentAccount.balance)
  return balance < 10 // Less than 10 XRP
})

const recentFundings = computed(() => {
  return xrplStore.transactions
    .filter(tx => tx.type === 'ACCOUNT_FUNDED' && tx.status === 'SUCCESS')
    .map(tx => ({
      id: tx.id,
      destination: tx.details.destination,
      amount: tx.details.amount
    }))
})

const messageClass = computed(() => {
  switch (messageType.value) {
    case 'success': return 'bg-green-100 text-green-800 dark:bg-green-900/20 dark:text-green-300'
    case 'error': return 'bg-red-100 text-red-800 dark:bg-red-900/20 dark:text-red-300'
    default: return 'bg-blue-100 text-blue-800 dark:bg-blue-900/20 dark:text-blue-300'
  }
})

function showMessage(text, type = 'info') {
  message.value = text
  messageType.value = type
  setTimeout(() => {
    message.value = ''
  }, 5000)
}

async function fundCurrentAccount() {
  if (!xrplStore.currentAccount.address) {
    showMessage('No account loaded', 'error')
    return
  }

  funding.value = true
  try {
    await xrplStore.fundAccount(xrplStore.currentAccount.address, '1000')
    showMessage('Account funded successfully!', 'success')
  } catch (error) {
    showMessage(`Funding failed: ${error.message}`, 'error')
  } finally {
    funding.value = false
  }
}

async function fundTargetAccount() {
  if (!targetAddress.value || !fundAmount.value) return

  funding.value = true
  try {
    await xrplStore.fundAccount(targetAddress.value, fundAmount.value.toString())
    showMessage(`Account ${targetAddress.value} funded successfully!`, 'success')
    targetAddress.value = ''
    fundAmount.value = 1000
  } catch (error) {
    showMessage(`Funding failed: ${error.message}`, 'error')
  } finally {
    funding.value = false
  }
}
</script>