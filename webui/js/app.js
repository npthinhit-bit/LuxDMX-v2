/**
 * LuxDMX Setup Portal - Main Application Logic
 * Handles WiFi provisioning flow: scan -> select -> password -> save -> success
 */
const LuxDMXApp = {
    transport: null,
    currentNetwork: null,
    retryCount: 0,
    maxRetries: 3,

    /**
     * Initialize the application
     */
    init() {
        this.transport = createTransport();
        this.bindEvents();
        this.showLoading('Starting setup...');
        this.checkConnection();
    },

    /**
     * Bind UI event handlers
     */
    bindEvents() {
        const backBtn = document.getElementById('backToScan');
        if (backBtn) {
            backBtn.addEventListener('click', () => this.goToScan());
        }

        const scanAgainBtn = document.getElementById('scanAgainBtn');
        if (scanAgainBtn) {
            scanAgainBtn.addEventListener('click', () => this.scan());
        }

        const passwordForm = document.getElementById('passwordForm');
        if (passwordForm) {
            passwordForm.addEventListener('submit', (e) => {
                e.preventDefault();
                this.saveCredentials();
            });
        }

        const retryBtn = document.getElementById('retryBtn');
        if (retryBtn) {
            retryBtn.addEventListener('click', () => this.retry());
        }
    },

    /**
     * Show a specific screen by ID
     */
    showScreen(screenId) {
        document.querySelectorAll('.screen').forEach(screen => {
            screen.classList.remove('active');
        });
        const screen = document.getElementById(screenId);
        if (screen) {
            screen.classList.add('active');
        }
    },

    /**
     * Show loading screen with message
     */
    showLoading(message = 'Connecting...') {
        document.getElementById('loadingStatus').textContent = message;
        this.showScreen('loadingScreen');
    },

    /**
     * Check if the device is reachable
     */
    async checkConnection() {
        try {
            const reachable = await this.transport.isReachable();
            if (reachable) {
                const info = await this.transport.getInfo();
                console.log('Device info:', info);
            }
            this.scan();
        } catch (error) {
            this.retryCount++;
            if (this.retryCount <= this.maxRetries) {
                this.showLoading(`Retry ${this.retryCount}/${this.maxRetries}...`);
                setTimeout(() => this.checkConnection(), 2000);
            } else {
                this.showLoading('Connection timeout');
                // In mock mode, proceed despite errors
                if (this.transport instanceof MockTransport) {
                    this.scan();
                }
            }
        }
    },

    /**
     * Scan for WiFi networks
     */
    async scan() {
        this.showLoading('Scanning for networks...');

        try {
            const networks = await this.transport.scan();
            this.renderNetworks(networks);
            this.showScreen('scanScreen');
        } catch (error) {
            console.error('Scan error:', error);
            if (this.transport instanceof MockTransport) {
                // Retry in mock mode
                setTimeout(() => this.scan(), 500);
            } else {
                this.showError('Failed to scan for networks. Please try again.');
            }
        }
    },

    /**
     * Render network list in the UI
     */
    renderNetworks(networks) {
        const listEl = document.getElementById('networkList');
        if (!listEl) return;

        listEl.innerHTML = '';

        if (networks.length === 0) {
            listEl.innerHTML = '<p class="no-networks">No networks found. Move closer to your router.</p>';
            return;
        }

        // Sort by signal strength (strongest first)
        networks.sort((a, b) => b.rssi - a.rssi);

        networks.forEach(network => {
            const item = this.createNetworkItem(network);
            listEl.appendChild(item);
        });
    },

    /**
     * Create a network list item element
     */
    createNetworkItem(network) {
        const div = document.createElement('div');
        div.className = 'network-item';
        div.addEventListener('click', () => this.selectNetwork(network));

        const infoDiv = document.createElement('div');
        infoDiv.className = 'network-info';

        const nameDiv = document.createElement('div');
        nameDiv.className = 'network-name';
        nameDiv.textContent = network.ssid || '<hidden>';
        infoDiv.appendChild(nameDiv);

        const detailsDiv = document.createElement('div');
        detailsDiv.className = 'network-details';

        // Signal bars
        const signal = document.createElement('span');
        signal.className = this.getSignalClass(network.rssi);
        signal.textContent = '📶';
        detailsDiv.appendChild(signal);

        // Lock icon for encrypted networks
        if (network.encrypted) {
            const lock = document.createElement('span');
            lock.className = 'lock-icon';
            lock.textContent = '🔒';
            detailsDiv.appendChild(lock);
        }

        infoDiv.appendChild(detailsDiv);
        div.appendChild(infoDiv);

        return div;
    },

    /**
     * Get CSS class for signal strength
     */
    getSignalClass(rssi) {
        if (rssi >= -50) return 'signal-strong';
        if (rssi >= -70) return 'signal-medium';
        return 'signal-weak';
    },

    /**
     * Select a network and go to password screen
     */
    selectNetwork(network) {
        this.currentNetwork = network;

        // Update password screen
        const ssidEl = document.getElementById('selectedSsid');
        if (ssidEl) {
            ssidEl.textContent = network.ssid;
        }

        // Clear password field
        const pwEl = document.getElementById('password');
        if (pwEl) {
            pwEl.value = '';
        }

        // Focus password field
        if (pwEl) {
            pwEl.focus();
        }

        this.showScreen('passwordScreen');
    },

    /**
     * Go back to scan screen
     */
    goToScan() {
        this.currentNetwork = null;
        this.showScreen('scanScreen');
    },

    /**
     * Save WiFi credentials
     */
    async saveCredentials() {
        const passwordEl = document.getElementById('password');
        const saveBtnText = document.getElementById('saveBtnText');
        const savingSpinner = document.getElementById('savingSpinner');

        if (!this.currentNetwork) {
            return;
        }

        // Show saving state
        if (saveBtnText) saveBtnText.style.display = 'none';
        if (savingSpinner) savingSpinner.style.display = 'inline-block';

        try {
            await this.transport.saveCredentials(
                this.currentNetwork.ssid,
                passwordEl ? passwordEl.value : ''
            );

            // Show success screen
            this.showScreen('successScreen');
        } catch (error) {
            console.error('Save error:', error);
            this.showError(error.message || 'Failed to save credentials. Please try again.');

            // Reset save button
            if (saveBtnText) saveBtnText.style.display = 'inline';
            if (savingSpinner) savingSpinner.style.display = 'none';
        }
    },

    /**
     * Show error screen
     */
    showError(message = 'An error occurred') {
        const errorEl = document.getElementById('errorMessage');
        if (errorEl) {
            errorEl.textContent = message;
        }
        this.showScreen('errorScreen');
    },

    /**
     * Retry after error
     */
    retry() {
        this.retryCount = 0;
        this.currentNetwork = null;
        this.scan();
    }
};

// Export for testing
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { LuxDMXApp };
}
