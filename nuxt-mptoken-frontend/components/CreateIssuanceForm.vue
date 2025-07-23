<template>
  <form @submit.prevent="createIssuance" class="space-y-6">
    <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
      <div>
        <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
          Asset Scale (0-19)
        </label>
        <input
          v-model.number="form.assetScale"
          type="number"
          min="0"
          max="19"
          class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
      </div>

      <div>
        <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
          Transfer Fee (basis points, 0-50000)
        </label>
        <input
          v-model.number="form.transferFee"
          type="number"
          min="0"
          max="50000"
          class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
      </div>

      <div>
        <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
          Maximum Amount (optional)
        </label>
        <input
          v-model="form.maximumAmount"
          type="text"
          placeholder="Leave empty for unlimited"
          class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
      </div>

      <div>
        <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
          Metadata (hex, optional)
        </label>
        <input
          v-model="form.metadata"
          type="text"
          placeholder="Optional metadata in hex"
          class="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
        />
      </div>
    </div>

    <div>
      <label class="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-3">
        Flags
      </label>
      <div class="grid grid-cols-2 md:grid-cols-3 gap-3">
        <label
          v-for="flag in availableFlags"
          :key="flag"
          class="flex items-center space-x-2 cursor-pointer"
        >
          <input
            v-model="form.flags"
            :value="flag"
            type="checkbox"
            class="rounded border-gray-300 text-blue-600 focus:ring-blue-500"
          />
          <span class="text-sm text-gray-700 dark:text-gray-300">{{ flag }}</span>
        </label>
      </div>
    </div>

    <div class="flex justify-end space-x-4">
      <button
        type="button"
        @click="resetForm"
        class="px-6 py-2 bg-gray-500 hover:bg-gray-600 text-white rounded-lg font-medium transition-colors"
      >
        Reset
      </button>
      <button
        type="submit"
        :disabled="!canSubmit"
        class="px-6 py-2 bg-blue-600 hover:bg-blue-700 disabled:opacity-50 text-white rounded-lg font-medium transition-colors"
      >
        Create Issuance
      </button>
    </div>
  </form>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useXRPLStore } from '~/stores/xrpl'

const xrplStore = useXRPLStore()

const form = ref({
  assetScale: 2,
  transferFee: 0,
  maximumAmount: '',
  metadata: '',
  flags: []
})

const availableFlags = [
  'Can Lock',
  'Require Auth',
  'Can Escrow',
  'Can Trade',
  'Can Transfer',
  'Can Clawback'
]

const canSubmit = computed(() => {
  return xrplStore.isConnected && 
         xrplStore.isAccountLoaded &&
         form.value.assetScale >= 0 && 
         form.value.assetScale <= 19 &&
         form.value.transferFee >= 0 && 
         form.value.transferFee <= 50000
})

async function createIssuance() {
  if (!canSubmit.value) return

  try {
    const result = await xrplStore.createMPTokenIssuance({
      assetScale: form.value.assetScale,
      transferFee: form.value.transferFee,
      maximumAmount: form.value.maximumAmount || undefined,
      metadata: form.value.metadata || undefined,
      flags: form.value.flags
    })

    console.log('Issuance created:', result)
    
    // Reset form on success
    resetForm()
  } catch (error) {
    console.error('Failed to create issuance:', error)
  }
}

function resetForm() {
  form.value = {
    assetScale: 2,
    transferFee: 0,
    maximumAmount: '',
    metadata: '',
    flags: []
  }
}
</script>