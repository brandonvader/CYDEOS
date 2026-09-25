#include "apps/recorder/audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

#include "apps/recorder/recording.h"
#include "boards/board_config.h"

#if defined(BOARD_ES3C28P)
#include <AudioBoard.h>
using namespace audio_driver;
#endif

static int channelOffset = 0;

int barHeights[NUM_BARS] = {0};
static double vReal[FFT_SAMPLES];
static double vImag[FFT_SAMPLES];
static ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, (double)SAMPLE_RATE);

#if defined(BOARD_ES3C28P)
// ES8311 codec bring-up (I2C only - chip power-up, ADC/mic path, MCLK-ratio
// coefficients, I2S format bits). Never touches the I2S peripheral itself;
// capture below still goes through the plain i2s_read() API, same as
// Hosyond. output_device is DAC_OUTPUT_NONE for this capture-only v1 - the
// seam for adding speaker playback later is changing just this config,
// not the capture path.
static AudioBoard codecBoard(AudioDriverES8311, NoPins);

void initES8311Codec() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL); // shared bus with the FT6336G touch controller
  pinMode(AUDIO_EN_PIN, OUTPUT);
  digitalWrite(AUDIO_EN_PIN, LOW); // low = enable, per vendor pin table
  delay(20); // let the codec's power rail settle before addressing it on I2C -
             // without this, early register writes intermittently NACK/timeout
             // (confirmed on real hardware; recording still worked despite the
             // errors, but this removes them outright)

  CodecConfig cfg;
  cfg.input_device = ADC_INPUT_LINE1;
  cfg.output_device = DAC_OUTPUT_NONE;
  cfg.i2s.bits = BIT_LENGTH_16BITS;
  cfg.i2s.rate = RATE_16K;
  if (!codecBoard.begin(cfg)) {
    Serial.println("ES8311 codec init failed");
  }
}
#endif

void installI2S() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      // Sized generously (256ms of buffering vs. a naive 64ms) - a slow
      // loop() iteration (heavy TFT drawing, particularly the spectrum
      // redraw) was overrunning the DMA buffer before it could be
      // drained, silently dropping audio. See CLAUDE.md.
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

#if defined(BOARD_ES3C28P)
  i2s_pin_config_t pin_config = {
      .mck_io_num = I2S_MCK,
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_DI};
#else
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_SD};
#endif

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void calibrateChannelOffset() {
  int32_t buf[512];
  size_t bytesRead = 0;

  // Discard warm-up reads - samples right after i2s_driver_install can be
  // garbage while the peripheral settles.
  for (int i = 0; i < 3; i++) {
    i2s_read(I2S_PORT, buf, sizeof(buf), &bytesRead, portMAX_DELAY);
  }

  i2s_read(I2S_PORT, buf, sizeof(buf), &bytesRead, portMAX_DELAY);
  int words = bytesRead / sizeof(int32_t);

  int64_t sumEven = 0, sumOdd = 0;
  int nEven = 0, nOdd = 0;
  for (int i = 0; i < words; i++) {
    int32_t v = abs(buf[i] >> 8);
    if (i % 2 == 0) {
      sumEven += v;
      nEven++;
    } else {
      sumOdd += v;
      nOdd++;
    }
  }
  int64_t avgEven = nEven ? sumEven / nEven : 0;
  int64_t avgOdd = nOdd ? sumOdd / nOdd : 0;
  Serial.printf("Calibration: even-slot avg=%lld, odd-slot avg=%lld\n", (long long)avgEven, (long long)avgOdd);
  channelOffset = (avgEven >= avgOdd) ? 0 : 1;
}

// The INMP441/ES8311 path doesn't swing anywhere near full-scale for
// normal room/speaker volume - apply gain here, in the higher-precision
// 24-bit domain before truncating to 16-bit, so the gain doesn't just
// amplify truncation quantization noise. Clamp in the 24-bit domain too,
// so loud transients clip cleanly instead of wrapping around. See
// CLAUDE.md.
#define MIC_GAIN 16

static int16_t gainedSample16(int32_t s24) {
  int32_t amplified = s24 * MIC_GAIN;
  if (amplified > 8388607) amplified = 8388607;
  if (amplified < -8388608) amplified = -8388608;
  return (int16_t)(amplified >> 8);
}

static void updateSpectrum(int32_t *rawBuf, int words) {
  int n = 0;
  for (int i = channelOffset; i < words && n < FFT_SAMPLES; i += 2) {
    int32_t s24 = rawBuf[i] >> 8;
    vReal[n] = (double)gainedSample16(s24);
    vImag[n] = 0;
    n++;
  }
  while (n < FFT_SAMPLES) {
    vReal[n] = 0;
    vImag[n] = 0;
    n++;
  }

  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  int usableBins = FFT_SAMPLES / 2;
  int binsPerBar = usableBins / NUM_BARS;
  for (int b = 0; b < NUM_BARS; b++) {
    int start = b * binsPerBar;
    int end = start + binsPerBar;
    double sum = 0;
    for (int k = start; k < end && k < usableBins; k++) sum += vReal[k];
    double avg = binsPerBar > 0 ? sum / binsPerBar : 0;
    int h = (int)(avg / 150.0);
    // SPECTRUM_H isn't known here (it's a layout constant owned by the UI
    // module) - the UI's drawSpectrum() clamps bar height to what fits on
    // screen when it draws, so an over-tall value here is harmless.
    if (h < 0) h = 0;
    barHeights[b] = h;
  }
}

bool processAudioChunk() {
  int32_t rawBuf[1024];
  bool gotSpectrumData = false;

  while (true) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, rawBuf, sizeof(rawBuf), &bytesRead, 0);
    int words = bytesRead / sizeof(int32_t);
    if (words <= 0) break;

    if (recState == REC_RECORDING && recFile) {
      int16_t pcmBuf[512];
      int pcmCount = 0;
      for (int i = channelOffset; i < words; i += 2) {
        int32_t s24 = rawBuf[i] >> 8;
        pcmBuf[pcmCount++] = gainedSample16(s24);
        if (pcmCount >= 512) break;
      }
      fwrite(pcmBuf, sizeof(int16_t), pcmCount, recFile);
      samplesWritten += pcmCount;
    }

    if (recState == REC_RECORDING) {
      updateSpectrum(rawBuf, words);
      gotSpectrumData = true;
    }
  }

  return gotSpectrumData;
}
