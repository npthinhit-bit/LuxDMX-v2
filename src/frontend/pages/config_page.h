#pragma once
#include <Arduino.h>

static const char CONFIG_PAGE_BODY[] PROGMEM = R"=====(
<div class="container py-4" style="max-width:960px">

  <!-- Fold/unfold every section at once. Individual sections toggle from their header. -->
  <div class="d-flex justify-content-end mb-2">
    <!-- Label matches the folded default; the script re-labels it from the real state on load. -->
    <button type="button" class="btn btn-outline-secondary btn-sm" id="sec-all">Expand all</button>
  </div>

  <!-- Art-Net + Device config form -->
  <form method="POST" action="/config" id="cfg-form">

    <!-- Hardware board / pin picker (issue #12) -->
    <div class="card mb-3" data-sec="board">
      <div class="card-header d-flex align-items-center">
        <span class="sec-title">Hardware board</span>
        <span class="text-secondary small ms-2" id="board-detected"></span>
      </div>
      <div class="card-body">
        <label class="form-label text-secondary small mb-1">Board</label>
        <div class="d-flex gap-2 flex-wrap align-items-start">
          <select class="form-select" id="board-sel" name="board" style="max-width:340px"></select>
          <button type="button" class="btn btn-outline-primary" id="board-apply">Apply template</button>
          <button type="button" class="btn btn-outline-secondary" id="board-open">Show board &amp; pick pins</button>
        </div>
        <div class="form-text">
          Pick your board to apply its tested pin map in one click, then fine-tune by clicking
          pins on the diagram. The pick button next to each GPIO field opens the board so you
          click the actual pin instead of guessing its number. Unknown board? Choose
          <strong>Custom</strong> and the validator still flags strapping, flash, input-only and
          Ethernet-reserved pins for your chip.
        </div>
        <div id="board-hardwired" class="form-text mt-2"></div>
        <div id="pin-unlock-row" class="form-check form-switch mt-1" style="display:none">
          <input class="form-check-input" type="checkbox" id="pin-unlock">
          <label class="form-check-label small text-secondary" for="pin-unlock">Advanced: unlock the fixed GPIO pins (only if you reworked the board wiring)</label>
        </div>
        <div id="pin-warnings" class="mt-2"></div>
      </div>
    </div>

    <div class="card mb-3" data-sec="protocol">
      <div class="card-header"><span class="sec-title">Protocol</span></div>
      <div class="card-body">
        <div class="mb-3">
          <label class="form-label text-secondary small mb-1">Input protocol</label>
          <select class="form-select" name="protocol" id="proto-sel">
            <option value="0">Art-Net only</option>
            <option value="1">sACN / E1.31 only</option>
            <option value="2">Both (Art-Net + sACN)</option>
          </select>
        </div>
        <div class="form-text">The input protocol applies to the whole device. The universe is set per DMX output below.</div>
        <div class="form-check form-switch mt-3">
          <input class="form-check-input" type="checkbox" name="artrdm" id="artrdm-sw" value="1">
          <label class="form-check-label small" for="artrdm-sw">RDM over Art-Net</label>
        </div>
        <div class="form-text">Let a console do RDM to the fixtures over Art-Net (ArtPoll / ArtTodRequest / ArtRdm). Only active on an RDM-capable output (one with a DE/RE pin). Discovery is spread across DMX frames so it never stalls the output.</div>
      </div>
    </div>

    <div class="card mb-3" data-sec="network">
      <div class="card-header"><span class="sec-title">Network</span></div>
      <div class="card-body">
        <div id="net-mode-row" class="mb-3" style="display:none">
          <div class="form-check form-switch">
            <input class="form-check-input" type="checkbox" name="useeth" id="useeth-sw" value="1">
            <label class="form-check-label small" for="useeth-sw">Use wired Ethernet instead of WiFi</label>
          </div>
          <div class="form-text">Off = WiFi. On = wired Ethernet (built-in PHY on the WT32-ETH01, or a W5500 module configured below). Applied after reboot; reconnect at the new address.</div>
        </div>
        <div id="wifi-mode-row" class="mb-3">
          <label class="form-label text-secondary small mb-1">WiFi mode</label>
          <select class="form-select" name="wifimode" id="wifi-mode">
            <option value="0">Client (join an existing network, STA)</option>
            <option value="1">Standalone access point (AP)</option>
          </select>
          <div class="form-text">Client joins your router. Access point makes the device its own WiFi network for quick standalone tests, no router needed.</div>
        </div>
        <div id="ap-pw-row" class="mb-3" style="display:none">
          <label class="form-label text-secondary small mb-1">AP password</label>
          <input class="form-control" type="text" name="appw" id="ap-pw" value="" placeholder="(open network)">
          <div class="form-text">The AP broadcasts the hostname below as its SSID. 8 or more characters enables WPA2; leave empty for an open network.</div>
        </div>
        <div id="ap-fb-row" class="mb-3" style="display:none">
          <label class="form-label text-secondary small mb-1">If the wired Ethernet link is down</label>
          <select class="form-select" name="fbmode" id="fb-mode">
            <option value="0">Keep retrying the wired link (recommended)</option>
            <option value="1">Standalone WiFi AP (needs an AP password)</option>
            <option value="2">Reboot and retry</option>
            <option value="3">Join WiFi (fall back to your saved WiFi network)</option>
          </select>
          <div class="form-text">
            What to do when wired Ethernet is selected but the cable is unplugged or the switch is off.
            <b>Keep retrying</b> stays reachable once the link is back and never opens a hotspot (best on a show).
            <b>Standalone AP</b> makes the device its own network (set an AP password below; it refuses to open an
            unsecured AP). <b>Reboot</b> power-cycles to re-attempt the link. <b>Join WiFi</b> falls back to your
            saved WiFi network (below) when there is no wired link, handy if the same box is sometimes wired and
            sometimes not. To switch to a brand-new WiFi, hold the BOOT button at power-up for the setup portal
            (a link drop alone never moves the device to another network).
          </div>
        </div>
        <div id="sta-creds-row" class="mb-3">
          <label class="form-label text-secondary small mb-1">WiFi network (SSID)</label>
          <input class="form-control mb-2" type="text" name="wifissid" id="wifi-ssid" value="" placeholder="MyNetwork">
          <label class="form-label text-secondary small mb-1">WiFi password</label>
          <input class="form-control" type="text" name="wifipsk" id="wifi-psk" value="" placeholder="(leave blank to keep the current one)">
          <div class="form-text">The router network to join in WiFi client mode, or the network the wired link-loss fallback hands off to. Leave the password blank to keep the one already stored. To set this up without the web UI (first run), join the <b>LuxDMX-setup</b> access point and follow the page that pops up.</div>
        </div>
        <div id="static-row" class="form-check form-switch mb-3">
          <input class="form-check-input" type="checkbox" name="staticip" id="static-sw" value="1">
          <label class="form-check-label small" for="static-sw">Use static IP (otherwise DHCP)</label>
        </div>
        <div id="static-fields">
          <div class="mb-2">
            <label class="form-label text-secondary small mb-1">IP address</label>
            <input class="form-control" type="text" name="ip" id="net-ip" value="" placeholder="192.168.1.50">
          </div>
          <div class="mb-2">
            <label class="form-label text-secondary small mb-1">Gateway</label>
            <input class="form-control" type="text" name="gateway" id="net-gw" value="" placeholder="192.168.1.1">
          </div>
          <div class="mb-2">
            <label class="form-label text-secondary small mb-1">Subnet mask</label>
            <input class="form-control" type="text" name="subnet" id="net-sn" value="" placeholder="255.255.255.0">
          </div>
          <div>
            <label class="form-label text-secondary small mb-1">DNS (optional)</label>
            <input class="form-control" type="text" name="dns" id="net-dns" value="" placeholder="192.168.1.1">
          </div>
        </div>
        <div class="form-text mt-2">Applied after reboot. Wrong values can make the device unreachable. Use the BOOT-button reset to recover.</div>
        <div class="form-check form-switch mt-3">
          <input class="form-check-input" type="checkbox" name="ipprog" id="ipprog-sw" value="1">
          <label class="form-check-label small" for="ipprog-sw">Allow remote IP config over Art-Net (ArtIpProg)</label>
        </div>
        <div class="form-text">Lets a controller (e.g. DMX-Workshop) read and change this node's IP / mask / gateway over the network, so a box that landed on an unreachable address is recoverable without the BOOT button. <b>Off by default:</b> Art-Net has no password, so while this is on anyone on the network can renumber the device. The new address applies after a reboot.</div>
      </div>
    </div>

    <!-- Wired Ethernet: one selector for the PHY (or None). Replaces the old W5500
         enable switch + the separate PHY/RMII dropdowns. -->
    <div id="w5500-card" class="card mb-3" data-sec="wired" style="display:none">
      <div class="card-header"><span class="sec-title">Wired Ethernet</span></div>
      <div class="card-body">
        <label class="form-label text-secondary small mb-1">Wired Ethernet PHY</label>
        <select class="form-select" id="wired-sel"><option value="none">None (WiFi only)</option></select>
        <div class="form-text">
          Pick the wired hardware, or <b>None</b> to run on WiFi. <b>W5500</b> and <b>DM9051</b> are
          external SPI modules (any ESP32 / ESP32-S3) that share the same pins below; DM9051 is a
          W5500 alternative and is untested on hardware so far. The <b>RMII</b> PHYs use the classic
          ESP32's built-in MAC (an S3 has none, so it only offers the SPI modules). Choosing a PHY
          reveals its pins and the <i>Use wired Ethernet</i> switch in the Network card above.
        </div>
        <!-- Hidden, JS-driven fields the firmware reads (kept so the NVS keys / OTA stay the same) -->
        <input type="checkbox" name="ethon" id="ethon-sw" value="1" hidden>
        <select name="wiredphy" id="wired-phy" hidden><option value="0"></option><option value="1"></option></select>
        <select name="ethspiphy" id="eth-spi-phy" hidden><option value="0"></option><option value="1"></option></select>
        <select name="rmiiphy" id="rmii-phy" hidden><option value="0"></option><option value="1"></option><option value="2"></option><option value="3"></option><option value="4"></option><option value="5"></option></select>

        <div id="w5500-pins" class="mt-3" style="display:none">
          <div class="form-text mb-2">GPIO pins for the W5500 / DM9051 SPI module. Defaults are the classic-ESP32 VSPI pins.</div>
          <div class="row g-2">
            <div class="col"><label class="form-label text-secondary small mb-1">CS</label>
              <input class="form-control" type="number" name="ethcs" id="eth-cs" min="-1" max="48" value="5"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">SCK</label>
              <input class="form-control" type="number" name="ethsck" id="eth-sck" min="-1" max="48" value="18"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">MOSI</label>
              <input class="form-control" type="number" name="ethmosi" id="eth-mosi" min="-1" max="48" value="23"></div>
          </div>
          <div class="row g-2 mt-1">
            <div class="col"><label class="form-label text-secondary small mb-1">MISO</label>
              <input class="form-control" type="number" name="ethmiso" id="eth-miso" min="-1" max="48" value="19"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">INT</label>
              <input class="form-control" type="number" name="ethint" id="eth-int" min="-1" max="48" value="4"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">RST</label>
              <input class="form-control" type="number" name="ethrst" id="eth-rst" min="-1" max="48" value="25"></div>
          </div>
          <div class="row g-2 mt-1">
            <div class="col-5"><label class="form-label text-secondary small mb-1">SPI clock (MHz)</label>
              <input class="form-control" type="number" name="ethfreq" id="eth-freq" min="1" max="80" value="20"></div>
          </div>
          <div class="form-text mt-2">If the module isn't detected, lower the SPI clock (e.g. <code>1</code>) to rule out long/loose jumper wiring. Applied after reboot.</div>
        </div>

        <div id="rmii-pins" class="mt-3" style="display:none">
          <div class="form-text mb-2">RMII wiring. The data lines are fixed by the EMAC; set the PHY address, REF_CLK and the management / power pins. Defaults match the WT32-ETH01 (LAN8720) wiring.</div>
          <div class="row g-2">
            <div class="col"><label class="form-label text-secondary small mb-1">REF_CLK</label>
              <select class="form-select" name="rmiiclk" id="rmii-clk">
                <option value="0">GPIO0 in (ext)</option><option value="1">GPIO0 out</option>
                <option value="2">GPIO16 out</option><option value="3">GPIO17 out</option>
              </select></div>
            <div class="col"><label class="form-label text-secondary small mb-1">PHY addr</label>
              <input class="form-control" type="number" name="rmiiaddr" id="rmii-addr" min="0" max="31" value="1"></div>
          </div>
          <div class="row g-2 mt-1">
            <div class="col"><label class="form-label text-secondary small mb-1">MDC</label>
              <input class="form-control" type="number" name="rmiimdc" id="rmii-mdc" min="0" max="48" value="23"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">MDIO</label>
              <input class="form-control" type="number" name="rmiimdio" id="rmii-mdio" min="0" max="48" value="18"></div>
            <div class="col"><label class="form-label text-secondary small mb-1">PHY power</label>
              <input class="form-control" type="number" name="rmiipwr" id="rmii-pwr" min="-1" max="48" value="16"></div>
          </div>
          <div class="form-text mt-2">Data lines are fixed by the EMAC: TXD0 19, TXD1 22, TX_EN 21, RXD0 25, RXD1 26, RX_DV 27. Keep DMX / LED off those plus the pins above. Applied after reboot.</div>
        </div>
      </div>
    </div>

    <div class="card mb-3" data-sec="led">
      <div class="card-header"><span class="sec-title">Status LED</span></div>
      <div class="card-body">
        <div class="mb-3">
          <label class="form-label text-secondary small mb-1">LED type</label>
          <select class="form-select" name="ledtype" id="led-type">
            <option value="0">Disabled</option>
            <option value="1">Plain GPIO (active high)</option>
            <option value="2">WS2812 RGB (NeoPixel)</option>
            <option value="3">5-LED status panel (LuxDMX v6)</option>
          </select>
          <div class="form-text">
            Same status language on every type: <span class="text-success">green</span> = up (slow blink = DMX coming in) &nbsp;&middot;&nbsp; <span class="text-primary">blue</span> = RDM / identify &nbsp;&middot;&nbsp; <span class="text-warning">orange</span> = Ethernet on WiFi fallback &nbsp;&middot;&nbsp; <span class="text-danger">red</span> = no network &nbsp;&middot;&nbsp; white/Knight-Rider = booting.<br>
            5-LED panel shows the same states one LED at a time (its amber LED carries the orange fallback state).
          </div>
        </div>
        <div id="led-pin-row">
          <label class="form-label text-secondary small mb-1">GPIO pin</label>
          <input class="form-control" type="number" name="ledpin" id="led-pin" min="-1" max="48" value="2">
          <div class="form-text">−1 to disable</div>
        </div>
        <div id="led5-row" class="row g-2" style="display:none">
          <div class="col">
            <label class="form-label text-secondary small mb-1">Red</label>
            <input class="form-control" type="number" name="ledr" id="led-r" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Green</label>
            <input class="form-control" type="number" name="ledg" id="led-g" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Yellow</label>
            <input class="form-control" type="number" name="ledy" id="led-y" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Blue</label>
            <input class="form-control" type="number" name="ledb" id="led-b" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">White</label>
            <input class="form-control" type="number" name="ledw" id="led-w" min="-1" max="48" value="-1">
          </div>
          <div class="form-text">Per-LED GPIO, active-high. −1 = absent.</div>
        </div>
      </div>
    </div>

    <!-- Display (issue #5) -->
    <div class="card mb-3" data-sec="display">
      <div class="card-header"><span class="sec-title">Display</span></div>
      <div class="card-body">
        <div class="mb-3">
          <label class="form-label text-secondary small mb-1">Display type</label>
          <select class="form-select" name="disptype" id="disp-type">
            <option value="0">Disabled</option>
            <option value="1">SSD1306 128&times;64 OLED (I²C)</option>
            <option value="2">SSD1306 128&times;32 OLED (I²C)</option>
            <option value="3">SH1106 128&times;64 OLED (I²C)</option>
            <option value="4">SSD1351 128&times;128 colour OLED (SPI)</option>
          </select>
          <div class="form-text">
            Shows IP, universe, FPS, source count and link status; auto-switches to a warning
            banner on a source conflict, identify, or manual override. I²C address is
            auto-detected (0x3C / 0x3D); the colour panel mirrors the status-LED colours.
          </div>
        </div>

        <div id="disp-i2c">
          <div class="row g-2">
            <div class="col">
              <label class="form-label text-secondary small mb-1">SDA pin</label>
              <input class="form-control" type="number" name="dispsda" id="disp-sda" min="-1" max="48" value="21">
            </div>
            <div class="col">
              <label class="form-label text-secondary small mb-1">SCL pin</label>
              <input class="form-control" type="number" name="dispscl" id="disp-scl" min="-1" max="48" value="22">
            </div>
          </div>
          <div class="form-text mt-2">
            ESP32 GPIO numbers. Avoid strapping pins such as <code>GPIO12</code>. On WT32-ETH01
            use <code>14</code>&nbsp;/&nbsp;<code>15</code> (most pins are taken by Ethernet).
          </div>
        </div>

        <div id="disp-spi" style="display:none">
          <div class="row g-2">
            <div class="col">
              <label class="form-label text-secondary small mb-1">CS</label>
              <input class="form-control" type="number" name="dispcs" id="disp-cs" min="-1" max="48" value="-1">
            </div>
            <div class="col">
              <label class="form-label text-secondary small mb-1">DC</label>
              <input class="form-control" type="number" name="dispdc" id="disp-dc" min="-1" max="48" value="-1">
            </div>
            <div class="col">
              <label class="form-label text-secondary small mb-1">RST</label>
              <input class="form-control" type="number" name="disprst" id="disp-rst" min="-1" max="48" value="-1">
            </div>
          </div>
          <div class="row g-2 mt-1">
            <div class="col">
              <label class="form-label text-secondary small mb-1">SCK / CLK</label>
              <input class="form-control" type="number" name="dispsck" id="disp-sck" min="-1" max="48" value="-1">
            </div>
            <div class="col">
              <label class="form-label text-secondary small mb-1">MOSI / DIN</label>
              <input class="form-control" type="number" name="dispmosi" id="disp-mosi" min="-1" max="48" value="-1">
            </div>
          </div>
          <div class="form-text mt-2">
            Hardware-SPI pins for the colour OLED. SPI panels need ~5 pins, so they only fit the
            non-Ethernet boards (ESP32 / ESP32-S3), not WT32-ETH01 or the PoE carrier.
          </div>
        </div>

        <div id="disp-common" style="display:none">
          <div class="mt-3">
            <label class="form-label text-secondary small mb-1">Orientation</label>
            <select class="form-select" name="disprot" id="disp-rot">
              <option value="0">Normal</option>
              <option value="1">Flipped 180&deg;</option>
            </select>
          </div>
          <div class="form-text mt-2">Applied after reboot.</div>
        </div>
      </div>
    </div>

    <!-- On-unit controls: a rotary encoder and/or buttons drive a menu on the display (issue #24) -->
    <div class="card mb-3" data-sec="controls">
      <div class="card-header"><span class="sec-title">Controls (encoder + buttons)</span></div>
      <div class="card-body">
        <div class="form-text mb-3">
          Optional physical controls to pick the universe (and protocol) from the unit itself,
          shown live on the display. Wire a rotary encoder and/or up to four buttons to any free
          GPIOs and leave a pin at <code>-1</code> to skip it. Turn or tap to move through the menu,
          press to select, hold to go back. All off until you set a pin.
        </div>

        <div class="row g-2">
          <div class="col">
            <label class="form-label text-secondary small mb-1">Encoder A</label>
            <input class="form-control" type="number" name="enca" id="enc-a" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Encoder B</label>
            <input class="form-control" type="number" name="encb" id="enc-b" min="-1" max="48" value="-1">
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Push</label>
            <input class="form-control" type="number" name="encsw" id="enc-sw" min="-1" max="48" value="-1">
          </div>
        </div>
        <div class="row g-2 mt-1 align-items-end">
          <div class="col">
            <label class="form-label text-secondary small mb-1">Steps per detent</label>
            <select class="form-select" name="encsteps" id="enc-steps">
              <option value="1">1 (non-detented)</option>
              <option value="2">2 (half-step)</option>
              <option value="4">4 (standard EC11)</option>
            </select>
          </div>
          <div class="col">
            <div class="form-check">
              <input class="form-check-input" type="checkbox" name="encrev" id="enc-rev" value="1">
              <label class="form-check-label small" for="enc-rev">Reverse direction</label>
            </div>
          </div>
        </div>

        <hr class="my-3">
        <label class="form-label text-secondary small mb-1">Buttons</label>
        <div class="form-text mb-2">
          Any push-button to GND (the default). A long press fills in whatever action is missing,
          so even a single button can drive the whole menu (tap&nbsp;=&nbsp;move, hold&nbsp;=&nbsp;select).
          The encoder's own push is always Select (tap) / Back (hold).
        </div>

        <div class="row g-2 mt-1">
          <div class="col-4">
            <label class="form-label text-secondary small mb-1">Button 1 pin</label>
            <input class="form-control" type="number" name="btn1pin" id="btn1-pin" min="-1" max="48" value="-1">
          </div>
          <div class="col-8">
            <label class="form-label text-secondary small mb-1">Button 1 action</label>
            <select class="form-select" name="btn1act" id="btn1-act">
              <option value="0">Off</option><option value="1">Enter / Select</option>
              <option value="2">Back</option><option value="3">Next (+)</option><option value="4">Prev (-)</option>
            </select>
          </div>
        </div>
        <div class="row g-2 mt-1">
          <div class="col-4">
            <label class="form-label text-secondary small mb-1">Button 2 pin</label>
            <input class="form-control" type="number" name="btn2pin" id="btn2-pin" min="-1" max="48" value="-1">
          </div>
          <div class="col-8">
            <label class="form-label text-secondary small mb-1">Button 2 action</label>
            <select class="form-select" name="btn2act" id="btn2-act">
              <option value="0">Off</option><option value="1">Enter / Select</option>
              <option value="2">Back</option><option value="3">Next (+)</option><option value="4">Prev (-)</option>
            </select>
          </div>
        </div>
        <div class="row g-2 mt-1">
          <div class="col-4">
            <label class="form-label text-secondary small mb-1">Button 3 pin</label>
            <input class="form-control" type="number" name="btn3pin" id="btn3-pin" min="-1" max="48" value="-1">
          </div>
          <div class="col-8">
            <label class="form-label text-secondary small mb-1">Button 3 action</label>
            <select class="form-select" name="btn3act" id="btn3-act">
              <option value="0">Off</option><option value="1">Enter / Select</option>
              <option value="2">Back</option><option value="3">Next (+)</option><option value="4">Prev (-)</option>
            </select>
          </div>
        </div>
        <div class="row g-2 mt-1">
          <div class="col-4">
            <label class="form-label text-secondary small mb-1">Button 4 pin</label>
            <input class="form-control" type="number" name="btn4pin" id="btn4-pin" min="-1" max="48" value="-1">
          </div>
          <div class="col-8">
            <label class="form-label text-secondary small mb-1">Button 4 action</label>
            <select class="form-select" name="btn4act" id="btn4-act">
              <option value="0">Off</option><option value="1">Enter / Select</option>
              <option value="2">Back</option><option value="3">Next (+)</option><option value="4">Prev (-)</option>
            </select>
          </div>
        </div>

        <div class="row g-2 mt-2 align-items-end">
          <div class="col">
            <div class="form-check">
              <input class="form-check-input" type="checkbox" name="btnah" id="btn-ah" value="1">
              <label class="form-check-label small" for="btn-ah">Buttons wired active-high (to&nbsp;3V3)</label>
            </div>
          </div>
          <div class="col">
            <label class="form-label text-secondary small mb-1">Menu top universe</label>
            <input class="form-control" type="number" name="ctlunimax" id="ctl-unimax" min="1" max="511" value="15">
          </div>
        </div>
        <div class="form-text mt-2">
          The universe knob wraps <code>0…top</code>. Keep it at 15 for a single Art-Net net;
          raise it if you want to reach higher universes from the knob.
        </div>
      </div>
    </div>

    <!-- DMX outputs: up to 2 independent universes -->
    <div class="card mb-3" data-sec="outputs-help">
      <div class="card-header"><span class="sec-title">DMX Outputs</span></div>
      <div class="card-body">
        <div class="form-text" style="line-height:1.5">
           Each output is an independent DMX universe driven by its own ESP32 UART + RS485 transceiver.
           Up to <strong>4</strong> outputs are supported on the ESP32-S3 (the 3 UARTs — UART0 is the serial
           console — serve RDM RX on outputs A+B; outputs C+D are DMX-only via RMT). Pin numbers are <strong>ESP32 GPIO numbers</strong> (the <code>GPIO&nbsp;xx</code> /
           <code>Gxx</code> label printed on the board), matched to the transceiver pins by function:
           <strong>TX → DI</strong> (driver input), <strong>RX → RO</strong> (receiver output, needed
           for RDM), <strong>RTS → EN</strong> (direction / DE-RE; <strong>set it to enable RDM</strong>,
           leave at <code>−1</code> for auto-direction modules such as the Waveshare C).
           Each RDM-capable output needs a <em>distinct</em> UART port. RDM runs on the first enabled output
           that has an RTS pin set. Wire the RS485 <code>A</code>/<code>B</code> → XLR pin&nbsp;3/2,
           GND → XLR pin&nbsp;1, and add a 120&nbsp;Ω terminator at the end of each chain.
        </div>
      </div>
    </div>

    <div id="outputs-container"></div>

    <!-- Cloned once per output by buildOutputs() -->
    <template id="out-tpl">
      <div class="card mb-3 out-card">
        <div class="card-header d-flex align-items-center">
          <span class="sec-title out-title">DMX Output</span>
          <div class="form-check form-switch mb-0 ms-3">
            <input class="form-check-input out-en" type="checkbox" value="1">
            <label class="form-check-label small out-en-label">Enabled</label>
          </div>
        </div>
        <div class="card-body out-body">
          <div class="row g-3">
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">Universe (0-32767)</label>
              <input class="form-control out-uni" type="number" min="0" max="32767" value="0">
              <div class="form-text out-uni-hint"></div>
            </div>
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">UART port</label>
              <select class="form-select out-port">
                <option value="1">UART1</option>
                <option value="2">UART2</option>
                <option value="0">None (DMX-only)</option>
              </select>
              <div class="form-text">Must differ from the other output.</div>
            </div>
            <div class="col-sm-4">
              <label class="form-label text-secondary small mb-1">TX pin → DI</label>
              <input class="form-control out-tx" type="number" min="-1" max="48" value="-1">
            </div>
            <div class="col-sm-4">
              <label class="form-label text-secondary small mb-1">RX pin → RO</label>
              <input class="form-control out-rx" type="number" min="-1" max="48" value="-1">
            </div>
            <div class="col-sm-4">
              <label class="form-label text-secondary small mb-1">RTS pin → EN</label>
              <input class="form-control out-rts" type="number" min="-1" max="48" value="-1">
            </div>
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">Merge mode</label>
              <select class="form-select out-merge">
                <option value="0">Off (last source wins)</option>
                <option value="1">HTP (highest takes precedence)</option>
                <option value="2">LTP (latest source wins)</option>
              </select>
              <div class="form-text">Combine two consoles on this universe. sACN priority is honoured when merging is on.</div>
            </div>
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">On signal loss</label>
              <select class="form-select out-loss">
                <option value="0">Hold last frame</option>
                <option value="1">Blackout (send zeros)</option>
                <option value="2">Stop sending</option>
              </select>
              <div class="form-text">What this output does when its Art-Net/sACN source stops (after ~2.5 s). Hold keeps the last look; blackout drives every channel to 0; stop idles the line so fixtures run their own DMX-loss failsafe.</div>
            </div>
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">Transmit style</label>
              <select class="form-select out-style">
                <option value="0">Continuous (free-run)</option>
                <option value="1">Delta (follow the input)</option>
              </select>
              <div class="form-text out-style-note">Continuous clocks DMX at the fixed rate below, whatever the console does. Delta sends one DMX frame per received packet, so the wire follows your console exactly and nothing gets repeated.</div>
            </div>
            <div class="col-sm-6">
              <label class="form-label text-secondary small mb-1">DMX output rate</label>
              <select class="form-select out-rate">
                <option value="0">40 fps (25 ms)</option>
                <option value="1">41.7 fps (24 ms, fastest)</option>
                <option value="2">33.3 fps (30 ms)</option>
                <option value="3">25 fps (40 ms)</option>
                <option value="4">20 fps (50 ms)</option>
              </select>
              <div class="form-text">Match your console to avoid repeated frames: MagicQ and MADRIX both run 33.3 fps, most other software sits near 40. In Delta this is only the fall-back rate used while the source is quiet. Applies immediately, no restart.</div>
            </div>
          </div>
          <div class="form-text mt-2 out-rdm-note">Set RTS/EN to a GPIO to enable RDM on this output; leave −1 to disable.</div>
        </div>
      </div>
    </template>

    <div class="card mb-3" data-sec="device">
      <div class="card-header"><span class="sec-title">Device</span></div>
      <div class="card-body">
        <div class="mb-3">
          <label class="form-label text-secondary small mb-1">Hostname</label>
          <div class="input-group">
            <input class="form-control" type="text" name="hostname" id="dev-host" maxlength="32" value="">
            <span class="input-group-text">.local</span>
          </div>
          <div class="form-text" id="dev-host-url"></div>
        </div>
        <div class="mb-3">
          <label class="form-label text-secondary small mb-1">OTA Password</label>
          <input class="form-control" type="text" name="otapw" id="dev-otapw" maxlength="32" value="">
          <div class="form-text text-secondary small">
            Only used for network OTA from the Arduino IDE / PlatformIO
            (<code>upload_protocol = espota</code>). The browser "Firmware Update" below
            does not use it.
          </div>
        </div>
        <!-- Save lives in the fixed #save-bar at the bottom of the page -->
        <hr style="border-color:#30363d">
        <div class="d-flex align-items-center justify-content-between flex-wrap gap-2">
          <div>
            <div class="fw-semibold small">Restart</div>
            <div class="form-text mb-0">
              Restarts the device without changing a thing. DMX output stops for about
              ten seconds, so don't do it mid-show. Handy if a long-running gateway starts
              misbehaving, or to free up memory before a firmware update.
            </div>
          </div>
          <button type="button" class="btn btn-outline-secondary btn-sm flex-shrink-0" id="reboot-btn">Restart device</button>
        </div>
      </div>
    </div>
  </form>

  <!-- Firmware OTA -->
  <div class="card mb-3" data-sec="firmware">
    <div class="card-header"><span class="sec-title">Firmware Update</span></div>
    <div class="card-body">

      <div class="mb-3">
        <div class="d-flex align-items-center justify-content-between flex-wrap gap-2 mb-2">
          <span class="fw-semibold small">Update from LuxDMX.org</span>
          <span class="text-secondary small">Installed: <strong id="cur-ver">v?</strong></span>
        </div>

        <div class="form-check form-switch mb-3">
          <input class="form-check-input" type="checkbox" id="auto-update-sw">
          <label class="form-check-label small" for="auto-update-sw">
            Auto-update: install new firmware automatically when available
          </label>
        </div>

        <div class="table-responsive">
          <table class="table table-sm align-middle mb-0" style="--bs-table-bg:transparent;font-size:.8rem">
            <thead>
              <tr class="text-secondary">
                <th>Version</th><th>Released</th><th style="width:48%">Changes</th><th></th>
              </tr>
            </thead>
            <tbody id="ver-rows">
              <tr><td colspan="4" class="text-secondary text-center small py-2">Loading versions&hellip;</td></tr>
            </tbody>
          </table>
        </div>
        <div class="form-text">Select any version to install. The device reboots automatically when done.</div>

        <!-- Hidden form used to trigger a version-targeted OTA -->
        <form method="POST" action="/ota/github" id="ota-form" class="d-none">
          <input type="hidden" name="version" id="ota-version">
        </form>
      </div>

      <hr style="border-color:#30363d">

      <div>
        <div class="fw-semibold small mb-1">Manual upload</div>
        <p class="text-secondary small mb-2">Flash a local <code>firmware.bin</code> file directly.</p>
        <form method="POST" action="/ota/upload" enctype="multipart/form-data"
              class="d-flex gap-2 align-items-center flex-wrap">
          <input type="file" name="firmware" accept=".bin"
                 class="form-control form-control-sm" style="max-width:280px"
                 onchange="document.getElementById('upload-btn').disabled = !this.files.length">
          <button type="submit" class="btn btn-outline-secondary btn-sm" id="upload-btn" disabled>
            Upload &amp; Flash
          </button>
        </form>
      </div>

      <hr style="border-color:#30363d">

      <div>
        <div class="fw-semibold small mb-1">Install from a URL</div>
        <p class="text-secondary small mb-2">
          Point the device at a <code>.bin</code> it can reach and let it fetch the file itself.
          Unlike the upload above, the device reboots first and downloads with a fresh, unfragmented
          heap, so this still works on a box that has been running a long time and refuses an upload
          partway through. Prefer <code>http://</code>: an <code>https://</code> handshake needs
          about 50 KB of contiguous memory, which is usually the very thing you have run out of.
        </p>
        <form method="POST" action="/ota/url" class="d-flex gap-2 align-items-center flex-wrap">
          <input type="url" name="url" id="ota-url" placeholder="http://192.168.1.20:8000/firmware.bin"
                 class="form-control form-control-sm" style="max-width:380px"
                 oninput="document.getElementById('ota-url-btn').disabled = !/^https?:\/\/\S+$/.test(this.value)">
          <button type="submit" class="btn btn-outline-secondary btn-sm" id="ota-url-btn" disabled>
            Fetch &amp; Flash
          </button>
        </form>
      </div>

    </div>
  </div>

  <!-- WiFi Reset: danger zone -->
  <div class="card border-danger mt-2" data-sec="danger">
    <div class="card-header" style="color:#f33abc;background:#1c0a16;border-color:#6e1a4f"><span class="sec-title">Danger Zone</span></div>
    <div class="card-body">
      <p class="text-secondary small mb-3">
        Erases the stored WiFi password and reboots into setup AP mode
        (<strong>DMX-Gateway</strong>). Art-Net universe and other settings are preserved.
      </p>
      <div class="form-check mb-3">
        <input class="form-check-input border-danger" type="checkbox" id="confirm-reset"
               onchange="document.getElementById('reset-btn').disabled = !this.checked">
        <label class="form-check-label small" for="confirm-reset">
          I understand this will disconnect the device from WiFi
        </label>
      </div>
      <form method="POST" action="/reset">
        <button type="submit" class="btn btn-outline-danger btn-sm" id="reset-btn" disabled>
          Reset WiFi &amp; Reboot
        </button>
      </form>
    </div>
  </div>
  </div>
<div id="save-bar"><div class="save-inner d-flex justify-content-between align-items-center flex-wrap gap-2"><button type="submit" form="cfg-form" id="save-btn" class="btn btn-primary btn-sm" disabled>Save settings</button></div></div>
<div class="app-modal" id="board-modal"><div class="card board-card"><div class="card-header d-flex align-items-center justify-content-between"><span id="board-modal-title">Board pin map</span><div class="d-flex gap-1"><button type="button" class="btn btn-outline-secondary btn-sm py-0 px-2" id="board-print" title="Print pin assignment">Print</button><button type="button" class="btn btn-outline-secondary btn-sm py-0 px-2" id="board-modal-close" title="Close">&times;</button></div></div><div class="card-body p-2"><div class="mb-1"><select class="form-select form-select-sm" id="board-sel-modal"></select></div><div id="board-svg-wrap" class="board-wrap"></div><div id="board-pick-hint" class="d-none small text-secondary mt-1"></div></div><div class="card-footer d-flex justify-content-end"><button type="button" class="btn btn-primary btn-sm" id="board-modal-done">Done</button></div></div></div>
)=====";
