#line 1 "/media/nvme_raid0/mPC/cyberdeck-scout/Firmware/esp32-bringup/tiny_debug_service.cpp"
#include "tiny_debug_service.h"

#include "config.h"

static const uint8_t EVENT_COUNT = 24;
static const uint8_t EVENT_TEXT_LEN = 56;
static const uint8_t LINE_BUF_MAX = 48;

struct TinyDbgEvent {
  uint32_t seq;    // 0 = slot unused
  uint32_t atMs;
  char text[EVENT_TEXT_LEN];
};

static TinyDbgEvent g_events[EVENT_COUNT];
static uint32_t g_nextSeq = 1;
static uint8_t g_head = 0;

static uint32_t g_lastLineMs = 0;
static bool g_anyLineSeen = false;

// Live snapshot from the 1 Hz 'S' status records.
static bool g_statusSeen = false;
static uint32_t g_lastStatusMs = 0;
static bool g_bqValid = false;
static bool g_balancing = false;
static bool g_powerOn = false;
static uint16_t g_cell1Mv = 0;
static uint16_t g_cell2Mv = 0;
static uint8_t g_i2cErr = 0;

static char g_line[LINE_BUF_MAX];
static uint8_t g_lineLen = 0;

static void pushEvent(const char *msg) {
  TinyDbgEvent &e = g_events[g_head];
  e.seq = g_nextSeq++;
  e.atMs = millis();
  // Sanitize so the text can be embedded in JSON without escaping.
  uint8_t i = 0;
  for (; msg[i] != '\0' && i < EVENT_TEXT_LEN - 1; i++) {
    const char c = msg[i];
    e.text[i] = (c < 0x20 || c == '"' || c == '\\' || c > 0x7E) ? '.' : c;
  }
  e.text[i] = '\0';
  g_head = (uint8_t)((g_head + 1) % EVENT_COUNT);
}

static void pushEventf(const char *fmt, const char *arg) {
  char msg[EVENT_TEXT_LEN];
  snprintf(msg, sizeof(msg), fmt, arg);
  pushEvent(msg);
}

static void parseStatus(const char *s) {
  // Format after 'S': "<v> <c1>,<c2> <b><p> <err>"  e.g. "S1 3701,3695 -P 0"
  unsigned c1 = 0;
  unsigned c2 = 0;
  unsigned err = 0;
  char v = '0';
  char bal = '-';
  char pwr = 'p';
  if (sscanf(s, "%c %u,%u %c%c %u", &v, &c1, &c2, &bal, &pwr, &err) != 6) {
    pushEventf("Bad status record: S%s", s);
    return;
  }

  const bool valid = v == '1';
  const bool balancing = bal == 'B';
  const bool powerOn = pwr == 'P';

  if (g_statusSeen) {
    if (valid != g_bqValid) {
      pushEvent(valid ? "BQ reads valid" : "BQ reads failing");
    }
    if (balancing != g_balancing) {
      pushEvent(balancing ? "Cell balancing started" : "Cell balancing stopped");
    }
  } else {
    pushEvent("Status stream online");
  }

  g_statusSeen = true;
  g_lastStatusMs = millis();
  g_bqValid = valid;
  g_balancing = balancing;
  g_powerOn = powerOn;
  g_cell1Mv = (uint16_t)c1;
  g_cell2Mv = (uint16_t)c2;
  g_i2cErr = (uint8_t)err;
}

static void handleLine(const char *l) {
  g_lastLineMs = millis();
  g_anyLineSeen = true;

  switch (l[0]) {
    case 'B': pushEvent("ATtiny boot"); break;
    case 'N': {
      // "N<err> <pinlevels>" — pinlevels bit0=SDA, bit1=SCL as seen by the TWI.
      unsigned err = 0;
      unsigned lvl = 3;
      if (sscanf(l + 1, "%u %u", &err, &lvl) >= 1) {
        static const char *const busText[4] = {
            "SDA+SCL stuck low", "SCL stuck low", "SDA stuck low", "bus idle OK"};
        char msg[EVENT_TEXT_LEN];
        snprintf(msg, sizeof(msg), "BQ not responding (I2C err %u, %s)", err,
                 busText[lvl & 0x03]);
        pushEvent(msg);
      } else {
        pushEventf("Bad BQ error record: N%s", l + 1);
      }
      break;
    }
    case 'O': pushEvent("BQ configured OK"); break;
    case 'E': pushEvent("BQ configuration write failed"); break;
    case 'P': pushEvent(l[1] == '1' ? "Device power ON" : "Device power OFF"); break;
    case 'S': parseStatus(l + 1); break;
    default:  pushEventf("Unknown record: %s", l); break;
  }
}

void tiny_debug_begin() {
  Serial1.begin(TINY_DEBUG_BAUD, SERIAL_8N1, PIN_TINY_DEBUG_RX, /*tx*/ -1);
  Serial.printf("Tiny debug listener on GPIO%d @ %lu baud\n", PIN_TINY_DEBUG_RX,
                (unsigned long)TINY_DEBUG_BAUD);
}

void tiny_debug_poll() {
  while (Serial1.available() > 0) {
    const char c = (char)Serial1.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (g_lineLen > 0) {
        g_line[g_lineLen] = '\0';
        handleLine(g_line);
      }
      g_lineLen = 0;
      continue;
    }
    if (g_lineLen < LINE_BUF_MAX - 1) {
      g_line[g_lineLen++] = c;
    } else {
      // Oversized garbage (e.g. floating RX line) — drop it.
      g_lineLen = 0;
    }
  }
}

bool tiny_debug_json(char *buf, size_t len, uint32_t sinceSeq) {
  const uint32_t now = millis();
  const uint32_t lineAge = g_anyLineSeen ? (uint32_t)(now - g_lastLineMs) : 0xFFFFFFFFUL;
  const uint32_t statusAge = g_statusSeen ? (uint32_t)(now - g_lastStatusMs) : 0xFFFFFFFFUL;

  int n = snprintf(
      buf, len,
      "{\"line_seen\":%s,\"last_line_age_ms\":%lu,"
      "\"status_seen\":%s,\"status_age_ms\":%lu,"
      "\"bq_valid\":%s,\"c1_mV\":%u,\"c2_mV\":%u,"
      "\"balancing\":%s,\"power_on\":%s,\"i2c_err\":%u,"
      "\"last_seq\":%lu,\"events\":[",
      g_anyLineSeen ? "true" : "false", (unsigned long)lineAge,
      g_statusSeen ? "true" : "false", (unsigned long)statusAge,
      g_bqValid ? "true" : "false", g_cell1Mv, g_cell2Mv,
      g_balancing ? "true" : "false", g_powerOn ? "true" : "false", g_i2cErr,
      (unsigned long)(g_nextSeq - 1));
  if (n < 0 || (size_t)n >= len) {
    return false;
  }

  // Emit ring entries oldest-first, filtered to seq > sinceSeq.
  bool first = true;
  for (uint8_t i = 0; i < EVENT_COUNT; i++) {
    const TinyDbgEvent &e = g_events[(uint8_t)((g_head + i) % EVENT_COUNT)];
    if (e.seq == 0 || e.seq <= sinceSeq) {
      continue;
    }
    const int m = snprintf(buf + n, len - (size_t)n,
                           "%s{\"seq\":%lu,\"age_ms\":%lu,\"text\":\"%s\"}",
                           first ? "" : ",", (unsigned long)e.seq,
                           (unsigned long)(now - e.atMs), e.text);
    if (m < 0 || (size_t)(n + m) >= len) {
      return false;
    }
    n += m;
    first = false;
  }

  const int m = snprintf(buf + n, len - (size_t)n, "]}");
  return m >= 0 && (size_t)(n + m) < len;
}
