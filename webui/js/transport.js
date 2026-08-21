/**
 * Transport layer for LuxDMX provisioning app.
 * Abstracts API communication so the app can run against either:
 * - A real device (HTTP requests)
 * - A mock transport (for browser/playwright testing)
 */
class Transport {
    /**
     * Scan for available WiFi networks.
     * @returns {Promise<Array<{ssid: string, rssi: number, authmode: number, encrypted: boolean}>>}
     */
    async scan() {
        throw new Error('Not implemented');
    }

    /**
     * Get device info.
     * @returns {Promise<object>}
     */
    async getInfo() {
        throw new Error('Not implemented');
    }

    /**
     * Save WiFi credentials and reboot.
     * @param {string} ssid - WiFi network name
     * @param {string} password - WiFi password (may be empty for open networks)
     */
    async saveCredentials(ssid, password) {
        throw new Error('Not implemented');
    }

    /**
     * Check if device is reachable.
     * @returns {Promise<boolean>}
     */
    async isReachable() {
        throw new Error('Not implemented');
    }
}

/**
 * HTTP transport for real device communication.
 * Makes HTTP requests to the ESP32 setup portal.
 */
class HttpTransport extends Transport {
    constructor(baseUrl = '') {
        super();
        this.baseUrl = baseUrl;
    }

    async scan() {
        try {
            const response = await fetch(this.baseUrl + '/wifi/scan');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const data = await response.json();
            return data.map(item => ({
                ssid: item.ssid || '',
                rssi: item.rssi || -100,
                authmode: item.authmode || 0,
                encrypted: (item.authmode || 0) > 0
            }));
        } catch (error) {
            throw new Error('Scan failed: ' + error.message);
        }
    }

    async getInfo() {
        const response = await fetch(this.baseUrl + '/info.json');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return response.json();
    }

    async saveCredentials(ssid, password) {
        const formData = new URLSearchParams();
        formData.append('ssid', ssid);
        formData.append('psk', password || '');

        const response = await fetch(this.baseUrl + '/setup', {
            method: 'POST',
            body: formData
        });

        if (!response.ok) {
            throw new Error(`Save failed: HTTP ${response.status}`);
        }

        return response.json();
    }

    async isReachable() {
        try {
            const response = await fetch(this.baseUrl + '/info.json', {
                signal: AbortSignal.timeout(3000)
            });
            return response.ok;
        } catch {
            return false;
        }
    }
}

/**
 * Mock transport for browser testing without hardware.
 * Simulates network behavior with configurable mock data.
 */
class MockTransport extends Transport {
    constructor(options = {}) {
        super();
        this.delay = options.delay || 500;
        this.networks = options.networks || [
            { ssid: 'HomeNetwork', rssi: -45, authmode: 3, encrypted: true },
            { ssid: 'OfficeWiFi', rssi: -62, authmode: 3, encrypted: true },
            { ssid: 'CoffeeShop', rssi: -78, authmode: 0, encrypted: false },
            { ssid: 'NeighborWiFi', rssi: -85, authmode: 3, encrypted: true },
        ];
        this.deviceInfo = options.deviceInfo || {
            device: 'LuxDMX',
            firmware: '2.0.0',
            board: 'esp32s3_n16r8',
            ip: '192.168.4.1',
            rssi: 0,
            heap_free: 40732
        };
        this.saveDelay = options.saveDelay || 2000;
    }

    async _wait(ms = this.delay) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    async scan() {
        await this._wait();
        return [...this.networks];
    }

    async getInfo() {
        await this._wait();
        return { ...this.deviceInfo };
    }

    async saveCredentials(ssid, password) {
        await this._wait(this.saveDelay);

        // Simulate a save failure for specific test SSIDs
        if (ssid === 'FAIL_TEST') {
            throw new Error('Save failed: Simulated error for test');
        }

        return { status: 'ok', message: 'WiFi saved, rebooting...' };
    }

    async isReachable() {
        await this._wait();
        return true;
    }
}

/**
 * Factory function to create the appropriate transport.
 * If localStorage.luxdmx_mock is set to 'true', uses MockTransport for testing.
 */
function createTransport(baseUrl) {
    if (localStorage && localStorage.getItem('luxdmx_mock') === 'true') {
        return new MockTransport();
    }
    return new HttpTransport(baseUrl);
}

// Export for module systems (if available)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { Transport, HttpTransport, MockTransport, createTransport };
}
