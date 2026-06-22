#include "nrf_spectrum.h"
#include "core/configPins.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <interface.h>

#define CHANNELS 80
#define RGB565(r, g, b) ((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
uint8_t channel[CHANNELS];

#define _BW tftWidth / CHANNELS

static bool sampleChannelRpd(uint8_t channelIndex) {
    NRFradio.setChannel(channelIndex);
    NRFradio.startListening();
    delayMicroseconds(1000);
    bool rpd = NRFradio.testRPD() || NRFradio.testCarrier();
    if (!rpd) rpd = NRFradio.testRPD();
    NRFradio.stopListening();
    NRFradio.flush_rx();
    return rpd;
}

String scanChannels(bool web) {
    String result = "{";

    uint8_t rpdValues[CHANNELS] = {0};
    uint8_t hitCount = 0;

    for (int i = 0; i < CHANNELS; i++) {
        const bool rpd = sampleChannelRpd((uint8_t)i);
        if (rpd) {
            channel[i] = (uint8_t)min(100, (int)channel[i] + 35);
            hitCount++;
        } else {
            channel[i] = (uint8_t)((channel[i] * 3) / 4);
        }
        rpdValues[i] = channel[i];
    }

    for (int i = 0; i < CHANNELS; i++) {
        const int level = rpdValues[i];
        const int x = i * _BW;
        const int c = i;

        tft.drawFastVLine(
            x, tftHeight - (10 + level), level, (i % 2 == 0) ? bruceConfig.priColor : TFT_DARKGREY
        );
        tft.drawFastVLine(
            x, 0, tftHeight - (9 + level), (i % 8) ? TFT_BLACK : RGB565(25, 25, 25)
        );
        tft.drawFastVLine(x, 0, level, bruceConfig.secColor);
        if (c % 5 == 0 && c != 0) { tft.drawCentreString(String(c).c_str(), x, tftHeight / 2, 1); }

        if (web) {
            if (i > 0) result += ",";
            result += String(level);
        }
    }

    tft.fillRect(0, 0, tftWidth, 24, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("hits:" + String(hitCount) + " G0=exit", 0, 0, 1);

    if (web) result += "}";
    return result;
}

void nrf_spectrum() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.drawString("2.40Ghz", 0, tftHeight - LH);
    tft.drawCentreString("2.44Ghz", tftWidth / 2, tftHeight - LH, 1);
    tft.drawRightString("2.48Ghz", tftWidth, tftHeight - LH, 1);

    if (!nrf_start(NRF_MODE_SPI)) {
        Serial.println("Fail Starting radio");
        displayError("NRF24 not found");
        delay(500);
        return;
    }

    suspendSpectrumRadioIsolation();

    if (!nrf_rebindRadio()) {
        resumeSpectrumRadioIsolation();
        displayError("NRF24 rebind failed");
        delay(1000);
        return;
    }

    const bool spiOk = nrf_verifyRadioLink();
    tft.fillRect(0, 0, tftWidth, 36, bruceConfig.bgColor);
    tft.setTextColor(spiOk ? TFT_GREEN : TFT_RED, bruceConfig.bgColor);
    tft.drawString(
        String("SPI:") + (spiOk ? "OK" : "FAIL") + " CE=" + String((int)bruceConfigPins.NRF24_bus.io0) +
            " CS=" + String((int)bruceConfigPins.NRF24_bus.cs),
        0,
        0,
        1
    );
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("ch42=" + String(NRFradio.getChannel()), 0, 12, 1);

    if (!spiOk) {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.drawString("Check SPI 40/14/39", 0, 24, 1);
        delay(3000);
        resumeSpectrumRadioIsolation();
        return;
    }

    NRFradio.powerUp();
    delay(5);
    NRFradio.setAutoAck(false);
    NRFradio.disableCRC();
    NRFradio.setAddressWidth(2);
    const uint8_t noiseAddress[][2] = {
        {0x55, 0x55},
        {0xAA, 0xAA},
        {0xA0, 0xAA},
        {0xAB, 0xAA},
        {0xAC, 0xAA},
        {0xAD, 0xAA}
    };
    for (uint8_t i = 0; i < 6; ++i) { NRFradio.openReadingPipe(i, noiseAddress[i]); }
    NRFradio.setDataRate(RF24_1MBPS);
    NRFradio.setPALevel(RF24_PA_MAX);

    while (digitalRead(0) == HIGH) { scanChannels(); }

    NRFradio.stopListening();
    NRFradio.powerDown();
    resumeSpectrumRadioIsolation();
    delay(250);
}
