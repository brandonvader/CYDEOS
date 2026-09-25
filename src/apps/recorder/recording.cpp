#include "apps/recorder/recording.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

#include "apps/recorder/recorder_ui.h"
#include "boards/board_config.h"
#include "core/clock.h"

RecState recState = REC_IDLE;
FILE *recFile = nullptr;
uint32_t samplesWritten = 0;
char lastRecordingPath[48] = "";

uint32_t lastFreeSpaceMs = 0;
bool showingStats = false;
uint32_t statsShownAtMs = 0;
uint32_t lastRecordingDataSize = 0;
uint64_t cachedFreeBytes = 0;

struct WavHeader {
  char riff[4] = {'R', 'I', 'F', 'F'};
  uint32_t chunkSize;
  char wave[4] = {'W', 'A', 'V', 'E'};
  char fmt[4] = {'f', 'm', 't', ' '};
  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = SAMPLE_RATE * 2;
  uint16_t blockAlign = 2;
  uint16_t bitsPerSample = 16;
  char data[4] = {'d', 'a', 't', 'a'};
  uint32_t dataSize;
};
static_assert(sizeof(WavHeader) == WAV_HEADER_SIZE, "WAV_HEADER_SIZE in recording.h must match sizeof(WavHeader)");

static void writeWavHeader(FILE *f, uint32_t dataSize) {
  WavHeader hdr;
  hdr.dataSize = dataSize;
  hdr.chunkSize = 36 + dataSize;
  fseek(f, 0, SEEK_SET);
  fwrite(&hdr, sizeof(WavHeader), 1, f);
}

bool startRecording() {
  char ts[20];
  getTimestampForFilename(ts, sizeof(ts));
  snprintf(lastRecordingPath, sizeof(lastRecordingPath), "/sdcard/rec_%s.wav", ts);

  for (int attempt = 1; attempt <= 3 && !recFile; attempt++) {
    recFile = fopen(lastRecordingPath, "wb");
    if (!recFile) {
      Serial.printf("Failed to open %s for recording (attempt %d)\n", lastRecordingPath, attempt);
      delay(100);
    }
  }
  if (!recFile) {
    return false;
  }
  WavHeader placeholder;
  placeholder.dataSize = 0;
  placeholder.chunkSize = 36;
  fwrite(&placeholder, sizeof(WavHeader), 1, recFile);
  samplesWritten = 0;

  Serial.printf("Recording to %s\n", lastRecordingPath);
  return true;
}

bool findMostRecentRecording() {
  DIR *dir = opendir("/sdcard");
  if (!dir) return false;

  char bestName[48] = "";
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strncmp(entry->d_name, "rec_", 4) == 0 && strstr(entry->d_name, ".wav")) {
      if (strcmp(entry->d_name, bestName) > 0) {
        strncpy(bestName, entry->d_name, sizeof(bestName) - 1);
      }
    }
  }
  closedir(dir);

  if (bestName[0] == '\0') return false;
  snprintf(lastRecordingPath, sizeof(lastRecordingPath), "/sdcard/%s", bestName);
  return true;
}

// Caps how much it reads (a very long recording could otherwise block for
// a long time with zero visibility) and logs progress so a real hang is
// distinguishable from "still working".
#define ANALYZE_MAX_SAMPLES (SAMPLE_RATE * 120) // first 2 minutes at most

void analyzeLastRecording() {
  FILE *f = fopen(lastRecordingPath, "rb");
  if (!f) {
    Serial.println("Level analysis: could not reopen file");
    return;
  }
  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  fseek(f, sizeof(WavHeader), SEEK_SET);
  Serial.printf("Level analysis starting: %s (%ld bytes)\n", lastRecordingPath, fileSize);

  int16_t buf[512];
  size_t n;
  int32_t peak = 0;
  double sumSquares = 0;
  uint32_t sampleCount = 0;
  uint32_t lastProgressMs = millis();
  bool truncated = false;
  while ((n = fread(buf, sizeof(int16_t), 512, f)) > 0) {
    for (size_t i = 0; i < n; i++) {
      int32_t v = abs((int32_t)buf[i]);
      if (v > peak) peak = v;
      sumSquares += (double)buf[i] * (double)buf[i];
    }
    sampleCount += n;
    if (millis() - lastProgressMs > 2000) {
      lastProgressMs = millis();
      Serial.printf("  ...analyzed %lu samples so far\n", (unsigned long)sampleCount);
    }
    if (sampleCount >= ANALYZE_MAX_SAMPLES) {
      truncated = true;
      break;
    }
  }
  fclose(f);

  if (sampleCount == 0) {
    Serial.println("Level analysis: file has no audio data");
    return;
  }

  double rms = sqrt(sumSquares / sampleCount);
  double peakDbfs = peak > 0 ? 20.0 * log10((double)peak / 32768.0) : -1000.0;
  double rmsDbfs = rms > 0 ? 20.0 * log10(rms / 32768.0) : -1000.0;

  Serial.printf("Level analysis (%s%s): peak=%ld (%.1f dBFS), RMS=%.0f (%.1f dBFS)\n",
                lastRecordingPath, truncated ? ", first 2min only" : "",
                (long)peak, peakDbfs, rms, rmsDbfs);
}

void stopRecording() {
  if (recFile) {
    showWritingMessage(); // immediate feedback - analysis below can take a while on long files
    uint32_t dataSize = samplesWritten * sizeof(int16_t);
    writeWavHeader(recFile, dataSize);
    fclose(recFile);
    recFile = nullptr;
    Serial.printf("Stopped recording: %lu samples, %lu bytes\n",
                  (unsigned long)samplesWritten, (unsigned long)dataSize);
    analyzeLastRecording();
    lastRecordingDataSize = dataSize;
    showRecordingStats(dataSize); // reverts to "-Ready-" after 10s idle - see the background tick
    showingStats = true;
    statsShownAtMs = millis();
  } else {
    showReadyMessage();
  }
}

// ---------------------------------------------------------------------
// Recording metadata
// ---------------------------------------------------------------------

// Recording filenames are "rec_YYYYMMDD_HHMMSS.wav" in *local* time (see
// getTimestampForFilename()), not UTC - reversed here with mktime() rather
// than trusting the FAT filesystem's own file timestamps, since
// esp_vfs_fat only stamps real timestamps if a custom get_fattime() is
// wired up, which this project doesn't do.
static time_t parseRecordingTimestamp(const char *filename) {
  struct tm tmStruct = {};
  int year, month, day, hour, minute, second;
  if (sscanf(filename, "rec_%4d%2d%2d_%2d%2d%2d.wav", &year, &month, &day, &hour, &minute, &second) != 6) {
    return 0;
  }
  tmStruct.tm_year = year - 1900;
  tmStruct.tm_mon = month - 1;
  tmStruct.tm_mday = day;
  tmStruct.tm_hour = hour;
  tmStruct.tm_min = minute;
  tmStruct.tm_sec = second;
  tmStruct.tm_isdst = -1; // let mktime figure out DST, matching localtime_r's own behavior when the name was generated
  return mktime(&tmStruct);
}

bool getRecordingInfo(const char *filename, RecordingInfo *out) {
  if (strncmp(filename, "rec_", 4) != 0 || !strstr(filename, ".wav")) return false;

  char path[64];
  snprintf(path, sizeof(path), "/sdcard/%s", filename);
  struct stat st;
  if (stat(path, &st) != 0) return false;

  const uint32_t byteRate = SAMPLE_RATE * sizeof(int16_t); // fixed format - 16-bit mono, see WavHeader
  uint32_t fileSize = (uint32_t)st.st_size;
  uint32_t dataSize = fileSize > sizeof(WavHeader) ? fileSize - sizeof(WavHeader) : 0;

  strncpy(out->name, filename, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  out->size = fileSize;
  out->duration = byteRate > 0 ? dataSize / byteRate : 0;
  out->timestamp = (uint32_t)parseRecordingTimestamp(filename);
  return true;
}

RecordingInfo recordingsList[MAX_RECORDINGS_SHOWN];
int recordingsListCount = 0;

// Bounded top-N selection (replace the currently-oldest kept entry if a
// newer one turns up), not a full sort of every file on the card - kept
// cheap even with hundreds of recordings on the card.
void loadRecordingsList() {
  recordingsListCount = 0;
  DIR *dir = opendir("/sdcard");
  if (!dir) return;

  struct dirent *entry;
  RecordingInfo info;
  while ((entry = readdir(dir)) != nullptr) {
    if (!getRecordingInfo(entry->d_name, &info)) continue;

    if (recordingsListCount < MAX_RECORDINGS_SHOWN) {
      recordingsList[recordingsListCount++] = info;
    } else {
      int oldestIdx = 0;
      for (int i = 1; i < MAX_RECORDINGS_SHOWN; i++) {
        if (recordingsList[i].timestamp < recordingsList[oldestIdx].timestamp) oldestIdx = i;
      }
      if (info.timestamp > recordingsList[oldestIdx].timestamp) {
        recordingsList[oldestIdx] = info;
      }
    }
  }
  closedir(dir);

  // Newest first - simple selection sort, fine at this size (<= 10).
  for (int i = 0; i < recordingsListCount - 1; i++) {
    int maxIdx = i;
    for (int j = i + 1; j < recordingsListCount; j++) {
      if (recordingsList[j].timestamp > recordingsList[maxIdx].timestamp) maxIdx = j;
    }
    if (maxIdx != i) {
      RecordingInfo tmp = recordingsList[i];
      recordingsList[i] = recordingsList[maxIdx];
      recordingsList[maxIdx] = tmp;
    }
  }
}

void formatFileSize(uint32_t bytes, char *buf, size_t len) {
  if (bytes >= 1024 * 1024) {
    snprintf(buf, len, "%.1f MB", bytes / (1024.0 * 1024.0));
  } else {
    snprintf(buf, len, "%.1f KB", bytes / 1024.0);
  }
}

void formatDuration(uint32_t totalSeconds, char *buf, size_t len) {
  snprintf(buf, len, "%lu:%02lu", (unsigned long)(totalSeconds / 60), (unsigned long)(totalSeconds % 60));
}
