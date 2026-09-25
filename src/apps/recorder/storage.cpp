#include "apps/recorder/storage.h"

#include <Arduino.h>
#include <esp_vfs_fat.h> // FATFS/f_getfree - used by getSDFreeBytes() on both boards

#include "boards/board_config.h"

#if defined(BOARD_ES3C28P)
#include <SD_MMC.h>
#else
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>

static sdmmc_card_t *card = nullptr;
#endif

#if defined(BOARD_ES3C28P)
bool mountSD() {
  // ES3C28P wires the SD card over SDIO (4-line SD_MMC), not SPI - no
  // SPI-bus-sharing concern here (see the SPI3_HOST comment in the #else
  // branch below, which is Hosyond-specific).
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  for (int attempt = 1; attempt <= SD_MOUNT_RETRIES; attempt++) {
    if (SD_MMC.begin("/sdcard", false, false, BOARD_MAX_SDMMC_FREQ, 5)) {
      Serial.printf("SD mounted OK (attempt %d)\n", attempt);
      return true;
    }
    Serial.printf("SD mount attempt %d failed\n", attempt);
    delay(300);
  }
  return false;
}
#else
bool mountSD() {
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  // SDSPI_HOST_DEFAULT() defaults to SPI2_HOST (HSPI), the exact same
  // physical SPI peripheral TFT_eSPI uses (USE_HSPI_PORT) despite using
  // different pins - sharing one hardware SPI unit between two drivers
  // was stable under light load but broke down once I2S's interrupt
  // handling added timing pressure. Force SD onto the other physical
  // peripheral (SPI3_HOST/VSPI) instead. See CLAUDE.md.
  host.slot = SPI3_HOST;

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = SD_MOSI;
  bus_cfg.miso_io_num = SD_MISO;
  bus_cfg.sclk_io_num = SD_SCK;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = 4000;

  esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("SPI bus init failed: %s\n", esp_err_to_name(ret));
    return false;
  }

  for (int attempt = 1; attempt <= SD_MOUNT_RETRIES; attempt++) {
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)SD_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
      Serial.printf("SD mounted OK (attempt %d)\n", attempt);
      return true;
    }
    Serial.printf("SD mount attempt %d failed: %s\n", attempt, esp_err_to_name(ret));
    delay(300);
  }
  return false;
}
#endif // BOARD_ES3C28P

uint64_t getSDFreeBytes() {
  FATFS *fs;
  DWORD freeClusters;
  if (f_getfree("0:", &freeClusters, &fs) != FR_OK) return 0;
  return (uint64_t)freeClusters * fs->csize * 512;
}
