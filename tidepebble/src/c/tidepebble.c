#include <pebble.h>
#include "message_keys.auto.h"
#include <stdlib.h>
#include <string.h>

#define TIDE_POINT_COUNT 24
#define TIDE_EVENT_COUNT 4
#define LOCATION_MAX_LEN 48
#define STATUS_MAX_LEN 48
#define ARROW_W 8
#define ARROW_H 7
#define ARROW_BIG_W 22
#define ARROW_BIG_H 12
#define ARROW_BIG_TAIL_LEN 8
#define ARROW_BIG_TAIL_WIDTH 5
#define DOUBLE_TAP_MS 500
#define PAGE_MARGIN PBL_IF_ROUND_ELSE(20, 8)
// Content whose bottom edge sits at PAGE_MARGIN from the true bottom of a
// round screen still clips — the chord is far narrower there than the mid-
// screen width PAGE_MARGIN was sized for. Extra clearance for anything
// anchored to the bottom edge specifically (cards, the tide bar).
#define ROUND_BOTTOM_EXTRA PBL_IF_ROUND_ELSE(30, 0)
// Then/Later's cards are already inset by their own internal text padding,
// so they don't need as much extra clearance as the tide bar's full-width
// dot pattern does — and with two cards stacked, every pixel is scarce.
#define ROUND_CARD_BOTTOM_EXTRA PBL_IF_ROUND_ELSE(14, 0)
#define HEADER_SIDE_INSET PBL_IF_ROUND_ELSE(42, 4)
#define PAGE_DOTS_W 20
#define CARD_GAP 8
#define STALE_ICON_W 14
#define STALE_ICON_GAP 4
#define TIDE_BAR_SEGMENTS 6
#define TIDE_BAR_GAP 4
#define WAKEUP_INTERVAL_SECONDS (60 * 60)
#define STALE_THRESHOLD_SECONDS (80 * 60)
#define AUTO_CLOSE_DELAY_MS 5000

#define PERSIST_KEY_TIDE_VALUES 1
#define PERSIST_KEY_TIDE_COUNT 2
#define PERSIST_KEY_LOCATION 3
#define PERSIST_KEY_WAVE_HEIGHT 4
#define PERSIST_KEY_SEA_TEMP 5
#define PERSIST_KEY_CURRENT_MINUTES_AT_SYNC 6
#define PERSIST_KEY_LAST_SYNC_EPOCH 7
#define PERSIST_KEY_BACKGROUND_REFRESH 8
#define PERSIST_KEY_UNITS_OVERRIDE 9

#define UNITS_OVERRIDE_AUTO 0
#define UNITS_OVERRIDE_METRIC 1
#define UNITS_OVERRIDE_IMPERIAL 2

typedef enum {
  TidePageOverview = 0,
  TidePageNow = 1,
  TidePageThen = 2,
  TidePageLater = 3,
  TidePageCount = 4,
} TidePage;

typedef enum {
  EventCardLayoutLarge,
  EventCardLayoutNext,
} EventCardLayout;

static Window *s_window;
static TextLayer *s_location_layer;
static TextLayer *s_time_layer;
static Layer *s_content_layer;
static Layer *s_stale_icon_layer;

static int16_t s_tide_values[TIDE_POINT_COUNT];
static int16_t s_tide_count;
static int16_t s_swell_values[TIDE_POINT_COUNT];
static int16_t s_swell_count;
static int16_t s_current_minutes;
static char s_location[LOCATION_MAX_LEN] = "TidePebble";
static char s_status[STATUS_MAX_LEN] = "Waiting for phone...";

static TidePage s_page = TidePageNow;
static int16_t s_event_indices[TIDE_EVENT_COUNT];
static bool s_event_highs[TIDE_EVENT_COUNT];
static int16_t s_event_count;
static bool s_rising = true;
static int16_t s_current_value = 0;
static int16_t s_local_min = 0;
static int16_t s_local_max = 0;
static int16_t s_wave_height = 0;
static int16_t s_sea_temp = 0;
static char s_time_display[6] = "--:--";
static GFont s_text_font;
static GFont s_header_font;
static GFont s_label_font;
static GFont s_large_detail_font;
static GFont s_large_label_font;
static GFont s_large_time_font;
static GFont s_compact_time_font;
static AppTimer *s_double_tap_timer;
static bool s_waiting_for_double_tap;
static AppTimer *s_close_timer;
static bool s_is_stale;
static bool s_background_refresh_enabled = true;
static uint8_t s_units_override = UNITS_OVERRIDE_AUTO;
static time_t s_last_sync_epoch;
static int16_t s_current_minutes_at_sync;

#define COLOR_HIGH      PBL_IF_COLOR_ELSE(GColorBrightGreen, GColorWhite)
#define COLOR_LOW       PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite)
#define COLOR_NOW       PBL_IF_COLOR_ELSE(GColorCyan, GColorWhite)
#define COLOR_NOW_HALO  PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorDarkGray)
#define COLOR_MUTED     PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COLOR_DIM       PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
#define COLOR_BLUE_LINE PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)
#define COLOR_NOW_CARD  PBL_IF_COLOR_ELSE(GColorMidnightGreen, GColorBlack)
#define COLOR_HIGH_CARD PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorBlack)
#define COLOR_LOW_CARD  PBL_IF_COLOR_ELSE(GColorBulgarianRose, GColorBlack)
#define COLOR_SAND      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite)

static GPoint s_arrow_up_pts[] = {{ARROW_W / 2, 0}, {0, ARROW_H}, {ARROW_W, ARROW_H}};
static GPathInfo s_arrow_up_info = {3, s_arrow_up_pts};
static GPath *s_arrow_up_path;
static GPoint s_arrow_down_pts[] = {{0, 0}, {ARROW_W, 0}, {ARROW_W / 2, ARROW_H}};
static GPathInfo s_arrow_down_info = {3, s_arrow_down_pts};
static GPath *s_arrow_down_path;
static GPoint s_arrow_big_up_pts[] =
  {{ARROW_BIG_W / 2, 0}, {0, ARROW_BIG_H}, {ARROW_BIG_W, ARROW_BIG_H}};
static GPathInfo s_arrow_big_up_info = {3, s_arrow_big_up_pts};
static GPath *s_arrow_big_up_path;
static GPoint s_arrow_big_down_pts[] =
  {{0, 0}, {ARROW_BIG_W, 0}, {ARROW_BIG_W / 2, ARROW_BIG_H}};
static GPathInfo s_arrow_big_down_info = {3, s_arrow_big_down_pts};
static GPath *s_arrow_big_down_path;

static int8_t prv_tide_value_digit(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '-') return 62;
  if (value == '_') return 63;
  return -1;
}

static bool prv_copy_cstring_tuple(Tuple *tuple, char *dest, size_t dest_size) {
  if (!tuple || tuple->type != TUPLE_CSTRING || dest_size == 0) return false;
  const char *source = (const char *)tuple->value;
  size_t copy_len = tuple->length;
  if (copy_len >= dest_size) copy_len = dest_size - 1;
  if (copy_len > 0 && source[copy_len - 1] == '\0') copy_len -= 1;
  memcpy(dest, source, copy_len);
  dest[copy_len] = '\0';
  return true;
}

static int16_t prv_parse_values(const char *csv, int16_t offset, int16_t *dest) {
  if (offset < 0 || offset >= TIDE_POINT_COUNT) return 0;
  int16_t count = 0;
  size_t length = strlen(csv);
  for (size_t chunk_offset = 0;
       chunk_offset + 1 < length && offset + count < TIDE_POINT_COUNT;
       chunk_offset += 2) {
    int8_t high = prv_tide_value_digit(csv[chunk_offset]);
    int8_t low = prv_tide_value_digit(csv[chunk_offset + 1]);
    if (high < 0 || low < 0) continue;
    dest[offset + count] = ((high << 6) | low) - 2048;
    count += 1;
  }
  return count;
}

static void prv_tide_min_max(int16_t *min_out, int16_t *max_out);

static bool prv_is_tide_event(int16_t index, bool high) {
  if (high) {
    return s_tide_values[index] > s_tide_values[index - 1] &&
      s_tide_values[index] >= s_tide_values[index + 1];
  }
  return s_tide_values[index] < s_tide_values[index - 1] &&
    s_tide_values[index] <= s_tide_values[index + 1];
}

static bool prv_use_metric_units(void) {
  if (s_units_override == UNITS_OVERRIDE_METRIC) return true;
  if (s_units_override == UNITS_OVERRIDE_IMPERIAL) return false;
  return PBL_IF_HEALTH_ELSE(
    health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters) !=
      MeasurementSystemImperial,
    true);
}

static void prv_format_height(char *buffer, size_t buffer_size, int16_t value) {
  if (prv_use_metric_units()) {
    int16_t tenths = (value + (value < 0 ? -5 : 5)) / 10;
    snprintf(buffer, buffer_size, "%s%d.%dm",
      tenths < 0 ? "-" : "", abs(tenths) / 10, abs(tenths) % 10);
    return;
  }

  int32_t feet_tenths = ((int32_t)value * 328084 + (value < 0 ? -500000 : 500000)) / 1000000;
  snprintf(buffer, buffer_size, "%s%ld.%ld'",
    feet_tenths < 0 ? "-" : "", labs(feet_tenths) / 10, labs(feet_tenths) % 10);
}

static void prv_format_wave_height(char *buffer, size_t buffer_size, int16_t value_cm) {
  if (prv_use_metric_units()) {
    int16_t tenths = (value_cm + 5) / 10;
    snprintf(buffer, buffer_size, "%d.%dm", tenths / 10, tenths % 10);
  } else {
    int32_t feet_tenths = ((int32_t)value_cm * 328084 + 500000) / 1000000;
    snprintf(buffer, buffer_size, "%ld.%ld'", labs(feet_tenths) / 10, labs(feet_tenths) % 10);
  }
}

static void prv_format_sea_temp(char *buffer, size_t buffer_size, int16_t value_tenths_c) {
  if (prv_use_metric_units()) {
    int16_t c = (value_tenths_c + (value_tenths_c < 0 ? -5 : 5)) / 10;
    snprintf(buffer, buffer_size, "%d\xc2\xb0", (int)c);
  } else {
    int32_t f_tenths = (int32_t)value_tenths_c * 9 / 5 + 320;
    int16_t f = (int16_t)((f_tenths + 5) / 10);
    snprintf(buffer, buffer_size, "%d\xc2\xb0", (int)f);
  }
}

static const char *prv_clock_format(void) {
  return clock_is_24h_style() ? "%H:%M" : "%I:%M";
}

static void prv_strip_leading_zero(char *buffer) {
  if (!clock_is_24h_style() && buffer[0] == '0') {
    memmove(buffer, buffer + 1, strlen(buffer));
  }
}

static void prv_format_time_for_index(char *buffer, size_t buffer_size, int16_t index) {
  time_t now = time(NULL);
  time_t base_time = now - ((time_t)s_current_minutes * 60);
  time_t sample_time = base_time + ((time_t)index * 60 * 60);
  struct tm *sample_tm = localtime(&sample_time);
  strftime(buffer, buffer_size, prv_clock_format(), sample_tm);
  prv_strip_leading_zero(buffer);
}

static void prv_format_minutes_to(char *buffer, size_t buffer_size, int16_t index) {
  int16_t minutes_to = index * 60 - s_current_minutes;
  if (minutes_to < 0) minutes_to = 0;
  int16_t h = minutes_to / 60;
  int16_t m = minutes_to % 60;
  if (m > 30) h += 1;
  if (h > 0) {
    snprintf(buffer, buffer_size, "In %dh", h);
  } else {
    snprintf(buffer, buffer_size, "In <1h");
  }
}

static int16_t prv_current_sample_x(int16_t width) {
  if (s_tide_count < 2) return 0;
  int32_t max_minutes = (int32_t)(s_tide_count - 1) * 60;
  int32_t minutes = s_current_minutes;
  if (minutes < 0) minutes = 0;
  if (minutes > max_minutes) minutes = max_minutes;
  return (int16_t)((int32_t)width * minutes / max_minutes);
}

static void prv_compute_state(void) {
  s_event_count = 0;
  for (int16_t i = 0; i < TIDE_EVENT_COUNT; i++) {
    s_event_indices[i] = -1;
    s_event_highs[i] = true;
  }
  if (s_tide_count < 2) return;

  int16_t sample = s_current_minutes / 60;
  int16_t frac = s_current_minutes % 60;
  if (sample < 0) {
    sample = 0;
    frac = 0;
  }
  if (sample >= s_tide_count - 1) {
    sample = s_tide_count - 2;
    frac = 60;
  }
  s_current_value = s_tide_values[sample] +
    (int16_t)((int32_t)(s_tide_values[sample + 1] - s_tide_values[sample]) * frac / 60);
  s_rising = s_tide_values[sample + 1] > s_tide_values[sample];

  for (int16_t i = 1; i < s_tide_count - 1 && s_event_count < TIDE_EVENT_COUNT; i++) {
    if (i * 60 <= s_current_minutes) continue;
    bool is_high = prv_is_tide_event(i, true);
    bool is_low = prv_is_tide_event(i, false);
    if (is_high || is_low) {
      s_event_indices[s_event_count] = i;
      s_event_highs[s_event_count] = is_high;
      s_event_count += 1;
    }
  }

  int16_t prev_value = s_tide_values[0];
  for (int16_t i = sample; i >= 1; i--) {
    if (prv_is_tide_event(i, true) || prv_is_tide_event(i, false)) {
      prev_value = s_tide_values[i];
      break;
    }
  }
  int16_t next_value = (s_event_count > 0)
    ? s_tide_values[s_event_indices[0]]
    : s_tide_values[s_tide_count - 1];

  s_local_min = prev_value < next_value ? prev_value : next_value;
  s_local_max = prev_value > next_value ? prev_value : next_value;
  if (s_local_min == s_local_max) {
    prv_tide_min_max(&s_local_min, &s_local_max);
  }
}

static void prv_draw_arrow(GContext *ctx, bool up, int16_t x, int16_t y) {
  GPath *path = up ? s_arrow_up_path : s_arrow_down_path;
  gpath_move_to(path, GPoint(x, y));
  gpath_draw_filled(ctx, path);
}

static void prv_draw_text(GContext *ctx, const char *text, GFont font, GRect rect,
                          GColor color, GTextAlignment alignment) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

static void prv_draw_page_dots(GContext *ctx, GRect bounds) {
  int16_t x = bounds.size.w - 6;
  int16_t y = bounds.size.h / 2 - 25;
  for (int16_t i = 0; i < TidePageCount; i++) {
    graphics_context_set_fill_color(ctx, i == s_page ? GColorWhite : COLOR_DIM);
    graphics_fill_circle(ctx, GPoint(x, y + i * 14), 3);
  }
}

static void prv_draw_chart_event(GContext *ctx, bool high, int16_t px, int16_t py) {
  graphics_context_set_fill_color(ctx, high ? COLOR_HIGH : COLOR_LOW);
  prv_draw_arrow(ctx, high, px - ARROW_W / 2, high ? py + 4 : py - ARROW_H - 4);
}

static void prv_draw_chart_event_label(GContext *ctx, const char *text,
                                       int16_t center_x, int16_t y, GColor color,
                                       GRect frame) {
  const int16_t label_w = 56;
  int16_t x = center_x - label_w / 2;

  prv_draw_text(ctx, text, s_large_detail_font,
    GRect(x, y, label_w, 30), color, GTextAlignmentCenter);
}

static void prv_tide_min_max(int16_t *min_out, int16_t *max_out) {
  int16_t min_v = s_tide_values[0], max_v = s_tide_values[0];
  for (int16_t i = 1; i < s_tide_count; i++) {
    if (s_tide_values[i] < min_v) min_v = s_tide_values[i];
    if (s_tide_values[i] > max_v) max_v = s_tide_values[i];
  }
  if (max_v == min_v) max_v += 1;
  *min_out = min_v;
  *max_out = max_v;
}

static void prv_draw_chart(GContext *ctx, GRect frame, bool labels) {
  const int16_t mx_left = (labels ? 16 : 4) + 2;
  const int16_t mx_right = labels ? 6 : 4;
  const int16_t my = 2;
  const int16_t w = frame.size.w - mx_left - mx_right;
  const int16_t h = frame.size.h - my * 2;
  const int16_t label_h = labels ? 32 : 0;
  const int16_t plot_y = frame.origin.y + my + label_h;
  int16_t plot_h = h - label_h * 2;
  if (plot_h < 10) plot_h = 10;

  if (s_tide_count < 2) {
    graphics_context_set_stroke_color(ctx, COLOR_DIM);
    graphics_draw_rect(ctx, GRect(frame.origin.x + mx_left, frame.origin.y + my, w, h));
    return;
  }

  int16_t min_v, max_v;
  prv_tide_min_max(&min_v, &max_v);

  int16_t axis_y = plot_y + plot_h / 2;
  graphics_context_set_stroke_color(ctx, COLOR_DIM);
  graphics_draw_line(ctx, GPoint(frame.origin.x + mx_left, axis_y),
    GPoint(frame.origin.x + mx_left + w, axis_y));

  graphics_context_set_stroke_color(ctx, COLOR_BLUE_LINE);
  GPoint prev = GPoint(frame.origin.x + mx_left, plot_y + plot_h);
  for (int16_t i = 0; i < s_tide_count; i++) {
    int16_t x = frame.origin.x + mx_left + (w * i / (s_tide_count - 1));
    int16_t y = plot_y + plot_h -
      ((s_tide_values[i] - min_v) * plot_h / (max_v - min_v));
    GPoint pt = GPoint(x, y);
    if (i > 0) {
      graphics_draw_line(ctx, GPoint(prev.x, prev.y - 1), GPoint(pt.x, pt.y - 1));
      graphics_draw_line(ctx, prev, pt);
      graphics_draw_line(ctx, GPoint(prev.x, prev.y + 1), GPoint(pt.x, pt.y + 1));
    }
    prev = pt;
  }

  int16_t cx = frame.origin.x + mx_left + prv_current_sample_x(w);
  int16_t cy = plot_y + plot_h -
    ((s_current_value - min_v) * plot_h / (max_v - min_v));
  graphics_context_set_fill_color(ctx, COLOR_NOW_HALO);
  graphics_fill_circle(ctx, GPoint(cx, cy), 6);
  graphics_context_set_fill_color(ctx, COLOR_NOW);
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);

  for (int16_t e = 0; e < s_event_count; e++) {
    int16_t index = s_event_indices[e];
    int16_t ex = frame.origin.x + mx_left + (w * index / (s_tide_count - 1));
    int16_t ey = plot_y + plot_h -
      ((s_tide_values[index] - min_v) * plot_h / (max_v - min_v));
    prv_draw_chart_event(ctx, s_event_highs[e], ex, ey);

    if (!labels) continue;
    char event_time[6];
    prv_format_time_for_index(event_time, sizeof(event_time), index);
    GColor label_color = s_event_highs[e] ? COLOR_HIGH : COLOR_LOW;
    int16_t ly = s_event_highs[e] ? frame.origin.y + my : plot_y + plot_h + 3;
    prv_draw_chart_event_label(ctx, event_time, ex, ly, label_color, frame);
  }
}

#define TIDE_BAR_PADDING_X -2
#define TIDE_BAR_PADDING_Y 4
#define TIDE_BAR_WAVE_PERIOD 10
#define TIDE_BAR_WAVE_AMP 3
#define TIDE_BAR_WAVE_ROW_H 7
#define TIDE_BAR_SAND_ROW_H 6
#define TIDE_BAR_SAND_SPACING 7
#define TIDE_BAR_SAND_SHIFT 3
#define TIDE_BAR_SAND_RADIUS 1

static void prv_draw_line_clipped_x(GContext *ctx, GPoint p0, GPoint p1, int16_t min_x,
                                    int16_t max_x) {
  if ((p0.x > max_x && p1.x > max_x) || (p0.x < min_x && p1.x < min_x)) return;
  if (p1.x > max_x) {
    int16_t dx = p1.x - p0.x;
    if (dx != 0) p1.y = p0.y + (int16_t)((int32_t)(p1.y - p0.y) * (max_x - p0.x) / dx);
    p1.x = max_x;
  } else if (p0.x > max_x) {
    int16_t dx = p1.x - p0.x;
    if (dx != 0) p0.y = p1.y + (int16_t)((int32_t)(p0.y - p1.y) * (max_x - p1.x) / dx);
    p0.x = max_x;
  }
  if (p1.x < min_x) {
    int16_t dx = p1.x - p0.x;
    if (dx != 0) p1.y = p0.y + (int16_t)((int32_t)(p1.y - p0.y) * (min_x - p0.x) / dx);
    p1.x = min_x;
  } else if (p0.x < min_x) {
    int16_t dx = p1.x - p0.x;
    if (dx != 0) p0.y = p1.y + (int16_t)((int32_t)(p0.y - p1.y) * (min_x - p1.x) / dx);
    p0.x = min_x;
  }
  graphics_draw_line(ctx, p0, p1);
}

static void prv_draw_tide_bar_wave_row(GContext *ctx, GRect cell, int16_t row_y,
                                       int16_t row_index) {
  int16_t x_offset = (row_index % 2 == 1) ? TIDE_BAR_WAVE_PERIOD / 2 : 0;
  graphics_context_set_stroke_color(ctx, COLOR_BLUE_LINE);
  graphics_context_set_stroke_width(ctx, 1);
  int16_t left = cell.origin.x;
  int16_t right = cell.origin.x + cell.size.w - 1;
  int16_t start_x = cell.origin.x - TIDE_BAR_WAVE_PERIOD + x_offset;
  for (int16_t x = start_x; x < right; x += TIDE_BAR_WAVE_PERIOD) {
    GPoint p0 = GPoint(x, row_y);
    GPoint p1 = GPoint(x + TIDE_BAR_WAVE_PERIOD / 2, row_y - TIDE_BAR_WAVE_AMP);
    GPoint p2 = GPoint(x + TIDE_BAR_WAVE_PERIOD, row_y);
    prv_draw_line_clipped_x(ctx, p0, p1, left, right);
    prv_draw_line_clipped_x(ctx, p1, p2, left, right);
  }
}

static void prv_draw_tide_bar_sand_row(GContext *ctx, GRect cell, int16_t row_y,
                                       int16_t row_index) {
  graphics_context_set_fill_color(ctx, COLOR_SAND);
  int16_t shift = (row_index * TIDE_BAR_SAND_SHIFT) % TIDE_BAR_SAND_SPACING;
  int16_t left = cell.origin.x;
  int16_t right = cell.origin.x + cell.size.w;
  int16_t start_x = cell.origin.x - TIDE_BAR_SAND_SPACING + shift;
  for (int16_t x = start_x; x < right; x += TIDE_BAR_SAND_SPACING) {
    if (x - TIDE_BAR_SAND_RADIUS < left || x + TIDE_BAR_SAND_RADIUS > right - 1) continue;
    graphics_fill_circle(ctx, GPoint(x, row_y), TIDE_BAR_SAND_RADIUS);
  }
}

static void prv_draw_tide_bar(GContext *ctx, GRect frame) {
  GRect padded = GRect(frame.origin.x + TIDE_BAR_PADDING_X, frame.origin.y + TIDE_BAR_PADDING_Y,
    frame.size.w - TIDE_BAR_PADDING_X * 2, frame.size.h - TIDE_BAR_PADDING_Y * 2);

  if (s_tide_count < 2) {
    graphics_context_set_stroke_color(ctx, COLOR_DIM);
    graphics_draw_rect(ctx, padded);
    return;
  }

  // Snapshot of "now": sea fills from the top down by how full the local tidal
  // range currently is, quantized to TIDE_BAR_SEGMENTS cells. Bracketed by the
  // nearest past/future high-low events, not the whole fetched series, so a
  // small local range on one day doesn't get swamped by a bigger range on another.
  int16_t local_range = s_local_max - s_local_min;
  int16_t sea_cells;
  if (local_range > 0) {
    sea_cells = (int16_t)(((int32_t)(s_current_value - s_local_min) * TIDE_BAR_SEGMENTS +
      local_range / 2) / local_range);
  } else {
    sea_cells = (s_current_value >= s_local_max) ? TIDE_BAR_SEGMENTS : 0;
  }
  if (sea_cells < 0) sea_cells = 0;
  if (sea_cells > TIDE_BAR_SEGMENTS) sea_cells = TIDE_BAR_SEGMENTS;

  bool is_sea_seg[TIDE_BAR_SEGMENTS];
  for (int16_t i = 0; i < TIDE_BAR_SEGMENTS; i++) {
    is_sea_seg[i] = i < sea_cells;
  }

  int16_t cell_h = padded.size.h / TIDE_BAR_SEGMENTS;
  int16_t y = padded.origin.y;
  int16_t wave_row = 0;
  int16_t sand_row = 0;

  for (int16_t i = 0; i < TIDE_BAR_SEGMENTS; i++) {
    bool is_last = (i == TIDE_BAR_SEGMENTS - 1);
    int16_t h = is_last ? (padded.origin.y + padded.size.h - y) : cell_h;
    GRect cell = GRect(padded.origin.x, y, padded.size.w, h);
    bool is_sea = is_sea_seg[i];

    if (is_sea) {
      for (int16_t ry = cell.origin.y + TIDE_BAR_WAVE_AMP + 2;
           ry < cell.origin.y + cell.size.h; ry += TIDE_BAR_WAVE_ROW_H) {
        prv_draw_tide_bar_wave_row(ctx, cell, ry, wave_row);
        wave_row++;
      }
    } else {
      for (int16_t ry = cell.origin.y + TIDE_BAR_SAND_RADIUS + 2;
           ry < cell.origin.y + cell.size.h; ry += TIDE_BAR_SAND_ROW_H) {
        prv_draw_tide_bar_sand_row(ctx, cell, ry, sand_row);
        sand_row++;
      }
    }

    y += h;
  }

  {
    int16_t boundary_i = TIDE_BAR_SEGMENTS;
    for (int16_t i = 1; i < TIDE_BAR_SEGMENTS; i++) {
      if (is_sea_seg[i] != is_sea_seg[0]) {
        boundary_i = i;
        break;
      }
    }
    int16_t boundary_y = (boundary_i >= TIDE_BAR_SEGMENTS)
      ? (is_sea_seg[0] ? padded.origin.y + padded.size.h : padded.origin.y)
      : padded.origin.y + boundary_i * cell_h;
    GPath *arrow = s_rising ? s_arrow_big_up_path : s_arrow_big_down_path;
    GColor arrow_color = s_rising ? COLOR_HIGH : COLOR_LOW;
    int16_t arrow_x = padded.origin.x + padded.size.w / 2 - ARROW_BIG_W / 2;
    int16_t arrow_y = boundary_y - ARROW_BIG_H / 2;
    int16_t min_arrow_y = padded.origin.y;
    int16_t max_arrow_y = padded.origin.y + padded.size.h - ARROW_BIG_H;
    if (arrow_y < min_arrow_y) arrow_y = min_arrow_y;
    if (arrow_y > max_arrow_y) arrow_y = max_arrow_y;
    int16_t center_x = arrow_x + ARROW_BIG_W / 2;

    graphics_context_set_fill_color(ctx, arrow_color);
    gpath_move_to(arrow, GPoint(arrow_x, arrow_y));
    gpath_draw_filled(ctx, arrow);

    graphics_context_set_stroke_color(ctx, arrow_color);
    graphics_context_set_stroke_width(ctx, ARROW_BIG_TAIL_WIDTH);
    if (s_rising) {
      GPoint tail_start = GPoint(center_x, arrow_y + ARROW_BIG_H);
      GPoint tail_end = GPoint(center_x, arrow_y + ARROW_BIG_H + ARROW_BIG_TAIL_LEN);
      graphics_draw_line(ctx, tail_start, tail_end);
    } else {
      GPoint tail_start = GPoint(center_x, arrow_y);
      GPoint tail_end = GPoint(center_x, arrow_y - ARROW_BIG_TAIL_LEN);
      graphics_draw_line(ctx, tail_start, tail_end);
    }
    graphics_context_set_stroke_width(ctx, 1);
  }
}

static GRect prv_draw_tide_bar_for_content(GContext *ctx, GRect content) {
  int16_t bar_w = content.size.w / 10;
  GRect bar_frame = GRect(content.origin.x, content.origin.y, bar_w, content.size.h);
  prv_draw_tide_bar(ctx, bar_frame);
  return bar_frame;
}

static void prv_draw_event_heading(GContext *ctx, GRect frame, const char *prefix,
                                   bool high, GFont font) {
  GColor color = high ? COLOR_HIGH : COLOR_LOW;
  char label[24];
  snprintf(label, sizeof(label), "%s %s", prefix, high ? "HIGH" : "LOW");
  graphics_context_set_fill_color(ctx, color);
  prv_draw_arrow(ctx, high, frame.origin.x, frame.origin.y + 8);
  prv_draw_text(ctx, label, font,
    GRect(frame.origin.x + ARROW_W + 4, frame.origin.y, frame.size.w - ARROW_W - 4, 26),
    color, GTextAlignmentLeft);
}

#define WAVE_ICON_W 16
#define WAVE_ICON_H 10
static void prv_draw_wave_icon(GContext *ctx, GPoint origin, GColor color) {
  static const GPoint pts[] = {
    {0, 5}, {2, 2}, {4, 0}, {6, 2}, {8, 5}, {10, 8}, {12, 10}, {14, 8}, {16, 5}
  };
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 8; i++) {
    graphics_draw_line(ctx,
      GPoint(origin.x + pts[i].x, origin.y + pts[i].y),
      GPoint(origin.x + pts[i + 1].x, origin.y + pts[i + 1].y));
  }
}

#define THERM_ICON_W 10
#define THERM_ICON_H 16
static void prv_draw_thermometer_icon(GContext *ctx, GPoint origin, GColor color) {
  GRect stem = GRect(origin.x + 3, origin.y, 4, THERM_ICON_H - 6);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, stem, 2);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(origin.x + 5, origin.y + THERM_ICON_H - 5), 5);
  graphics_fill_rect(ctx, GRect(origin.x + 4, origin.y + 3, 2, THERM_ICON_H - 8), 0, GCornerNone);
}

#define TAPE_ICON_W 16
#define TAPE_ICON_H 10
static void prv_draw_tape_icon(GContext *ctx, GPoint origin, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  int16_t base_y = origin.y + TAPE_ICON_H - 2;
  graphics_draw_line(ctx, GPoint(origin.x, base_y), GPoint(origin.x + TAPE_ICON_W, base_y));
  for (int16_t x = 0; x <= TAPE_ICON_W; x += 4) {
    int16_t tick_h = (x % 8 == 0) ? 6 : 3;
    graphics_draw_line(ctx, GPoint(origin.x + x, base_y), GPoint(origin.x + x, base_y - tick_h));
  }
}

#define COLOR_STALE PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite)
#define CLOCK_ICON_RADIUS 5
static void prv_draw_clock_icon(GContext *ctx, GPoint center, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, center, CLOCK_ICON_RADIUS);
  graphics_draw_line(ctx, center, GPoint(center.x, center.y - CLOCK_ICON_RADIUS + 2));
  graphics_draw_line(ctx, center, GPoint(center.x + CLOCK_ICON_RADIUS - 2, center.y));
}

static void prv_stale_icon_update_proc(Layer *layer, GContext *ctx) {
  if (!s_is_stale) return;
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.origin.x + CLOCK_ICON_RADIUS, bounds.origin.y + bounds.size.h / 2);
  prv_draw_clock_icon(ctx, center, COLOR_STALE);
}

static void prv_draw_card_background(GContext *ctx, GRect frame, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, frame, 8, GCornersAll);
}

static void prv_draw_event_card(GContext *ctx, GRect frame, int16_t event_number,
                                const char *prefix, EventCardLayout layout) {
  if (event_number >= s_event_count) {
    prv_draw_text(ctx, s_status, s_text_font, frame, COLOR_MUTED, GTextAlignmentCenter);
    return;
  }

  int16_t index = s_event_indices[event_number];
  bool high = s_event_highs[event_number];
  GColor color = high ? COLOR_HIGH : COLOR_LOW;
  char time_text[8], height_text[16], countdown_text[20];

  prv_format_time_for_index(time_text, sizeof(time_text), index);
  prv_format_height(height_text, sizeof(height_text), s_tide_values[index]);
  prv_format_minutes_to(countdown_text, sizeof(countdown_text), index);

  prv_draw_card_background(ctx, frame, GColorBlack);
  int16_t x = frame.origin.x + 12;
  int16_t y = frame.origin.y + (layout == EventCardLayoutLarge ? 8 : 0);
  prv_draw_event_heading(ctx, GRect(x, y, frame.size.w - 18, 28), prefix, high, s_label_font);

  GFont time_font = layout == EventCardLayoutLarge ? s_large_time_font : s_compact_time_font;
  int16_t time_h = layout == EventCardLayoutLarge ? 72 : 34;
  int16_t time_y;
  int16_t detail_h = PBL_IF_ROUND_ELSE(22, 30);
  int16_t detail_y = frame.origin.y + frame.size.h - 34;
  if (layout == EventCardLayoutLarge) {
    time_y = frame.origin.y + 46;
  } else if (layout == EventCardLayoutNext) {
#if defined(PBL_ROUND)
    // Round's Then page has much less vertical budget per card than rect
    // (bigger top/bottom bezel insets eat into it) — the rect formula below
    // centers time between heading and a bottom-anchored detail row, which
    // goes negative and overlaps once frame.size.h drops much below ~90px.
    // Stack top-down with small fixed gaps instead, so a short card just
    // sits tighter rather than overlapping.
    time_y = y + 18;
    detail_y = time_y + time_h - 14;
#else
    int16_t heading_center = frame.origin.y + 14;
    int16_t detail_center = detail_y + detail_h / 2;
    int16_t time_center = heading_center + (detail_center - heading_center) / 2;
    time_y = time_center - time_h / 2;
#endif
  } else {
    time_y = frame.origin.y + 20;
  }
  prv_draw_text(ctx, time_text, time_font,
    GRect(x, time_y, frame.size.w - 18, time_h), GColorWhite, GTextAlignmentLeft);
  int16_t avail_w = frame.size.w - 18;
  GSize dw1 = graphics_text_layout_get_content_size(countdown_text, s_large_detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize dw2 = graphics_text_layout_get_content_size("\xe2\x80\xa2", s_large_detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize dw3 = graphics_text_layout_get_content_size(height_text, s_large_detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t dgap = (avail_w - dw1.w - dw2.w - dw3.w) / 2;
  if (dgap < 0) dgap = 0;
  prv_draw_text(ctx, countdown_text, s_large_detail_font,
    GRect(x, detail_y, dw1.w, detail_h), GColorWhite, GTextAlignmentLeft);
  prv_draw_text(ctx, "\xe2\x80\xa2", s_large_detail_font,
    GRect(x + dw1.w + dgap, detail_y, dw2.w, detail_h), color, GTextAlignmentLeft);
  prv_draw_text(ctx, height_text, s_large_detail_font,
    GRect(x + dw1.w + dgap + dw2.w + dgap, detail_y, dw3.w, detail_h),
    GColorWhite, GTextAlignmentLeft);
}

static void prv_draw_now_row(GContext *ctx, GRect frame, const char *label, int16_t icon_x,
                             void (*icon_fn)(GContext *, GPoint, GColor), int16_t icon_h,
                             const char *value) {
  prv_draw_text(ctx, label, s_large_label_font,
    GRect(frame.origin.x, frame.origin.y, frame.size.w, frame.size.h), COLOR_MUTED,
    GTextAlignmentLeft);

  icon_fn(ctx, GPoint(icon_x, frame.origin.y + (frame.size.h - icon_h) / 2), GColorWhite);

  prv_draw_text(ctx, value, s_large_detail_font, frame, GColorWhite, GTextAlignmentRight);
}

static void prv_draw_now_card(GContext *ctx, GRect frame) {
  char height_text[16], wave_text[16], temp_text[16];
  prv_format_height(height_text, sizeof(height_text), s_current_value);
  prv_format_wave_height(wave_text, sizeof(wave_text), s_wave_height);
  prv_format_sea_temp(temp_text, sizeof(temp_text), s_sea_temp);

  prv_draw_card_background(ctx, frame, GColorBlack);
  int16_t x = frame.origin.x + 6;
  int16_t header_y = frame.origin.y + PAGE_MARGIN;
  int16_t header_h = 28;
  prv_draw_text(ctx, "NOW", s_label_font, GRect(x, header_y, 52, header_h),
    COLOR_NOW, GTextAlignmentLeft);

  GColor trend_color = s_rising ? COLOR_HIGH : COLOR_LOW;
  graphics_context_set_fill_color(ctx, trend_color);
  prv_draw_arrow(ctx, s_rising, x + 58, header_y + 9);
  prv_draw_text(ctx, s_rising ? "Rising" : "Falling", s_label_font,
    GRect(x + 70, header_y, frame.size.w - 88, header_h), trend_color, GTextAlignmentLeft);

  int16_t rows_y = header_y + header_h;
  int16_t rows_h = frame.origin.y + frame.size.h - PAGE_MARGIN - rows_y;
  int16_t row_h = rows_h / 3;
  int16_t row_w = frame.size.w - 18;

  GSize widest_label = graphics_text_layout_get_content_size("Waves", s_large_label_font,
    GRect(0, 0, row_w, row_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t icon_x = x + widest_label.w + 4;

  prv_draw_now_row(ctx, GRect(x, rows_y, row_w, row_h), "Water", icon_x,
    prv_draw_thermometer_icon, THERM_ICON_H, temp_text);
  prv_draw_now_row(ctx, GRect(x, rows_y + row_h, row_w, row_h), "Waves", icon_x,
    prv_draw_wave_icon, WAVE_ICON_H, wave_text);
  prv_draw_now_row(ctx, GRect(x, rows_y + row_h * 2, row_w, row_h), "Tide", icon_x,
    prv_draw_tape_icon, TAPE_ICON_H, height_text);
}

static void prv_draw_now_page(GContext *ctx, GRect bounds) {
  int16_t top_margin = 3;
  int16_t bottom_margin = PAGE_MARGIN + ROUND_BOTTOM_EXTRA;
  GRect content = GRect(PAGE_MARGIN, top_margin, bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W,
    bounds.size.h - top_margin - bottom_margin);
  GRect bar_content = GRect(content.origin.x, content.origin.y, content.size.w,
    bounds.size.h - top_margin - ROUND_BOTTOM_EXTRA);
  GRect bg_frame = GRect(0, bar_content.origin.y, bar_content.origin.x + bar_content.size.w,
    bar_content.size.h);
  prv_draw_card_background(ctx, bg_frame, GColorBlack);
  GRect bar_frame = prv_draw_tide_bar_for_content(ctx, bar_content);
  GRect now_card = GRect(content.origin.x + bar_frame.size.w + TIDE_BAR_GAP, content.origin.y,
    content.size.w - bar_frame.size.w - TIDE_BAR_GAP, content.size.h);
  prv_draw_now_card(ctx, now_card);
}

static void prv_draw_then_page(GContext *ctx, GRect bounds) {
  int16_t content_w = bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W;
  int16_t top_margin = 3;
  int16_t bottom_margin = PAGE_MARGIN + ROUND_CARD_BOTTOM_EXTRA;
  // Round has much less total vertical budget for two stacked cards than
  // rect (bigger top/bottom insets eat into it) — shrink the first card so
  // the second one isn't left with almost nothing.
  int16_t next_h = PBL_IF_ROUND_ELSE(66, 84);
  GRect next_card = GRect(PAGE_MARGIN, top_margin, content_w, next_h);
  GRect then_card = GRect(PAGE_MARGIN, top_margin + next_h + CARD_GAP, content_w,
    bounds.size.h - top_margin - bottom_margin - next_h - CARD_GAP);
  prv_draw_event_card(ctx, next_card, 0, "NEXT", EventCardLayoutNext);
  prv_draw_event_card(ctx, then_card, 1, "THEN", EventCardLayoutNext);
}

static void prv_draw_later_page(GContext *ctx, GRect bounds) {
  GRect card = GRect(PAGE_MARGIN, PAGE_MARGIN, bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W,
    bounds.size.h - PAGE_MARGIN * 2 - ROUND_BOTTOM_EXTRA);
  prv_draw_event_card(ctx, card, 2, "LATER", EventCardLayoutLarge);
}

static void prv_draw_overview_page(GContext *ctx, GRect bounds) {
  prv_draw_text(ctx, "OVERVIEW", s_label_font,
    GRect(HEADER_SIDE_INSET, 0, bounds.size.w / 2, 28), COLOR_MUTED, GTextAlignmentLeft);
  prv_draw_text(ctx, "NEXT 24H", s_label_font,
    GRect(bounds.size.w / 2 - 2, 0, bounds.size.w / 2 - HEADER_SIDE_INSET - 14, 28), COLOR_DIM,
    GTextAlignmentRight);

  GRect content = GRect(PAGE_MARGIN, 28, bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W,
    bounds.size.h - 30 - PBL_IF_ROUND_ELSE(28, 0));
  GRect bar_frame = prv_draw_tide_bar_for_content(ctx, content);
  GRect chart_frame = GRect(content.origin.x + bar_frame.size.w + TIDE_BAR_GAP, content.origin.y,
    content.size.w - bar_frame.size.w - TIDE_BAR_GAP, content.size.h);

  prv_draw_chart(ctx, chart_frame, true);
}

static void prv_draw_empty_page(GContext *ctx, GRect bounds) {
  prv_draw_text(ctx, s_status, s_text_font,
    GRect(10, bounds.size.h / 2 - 24, bounds.size.w - 20, 48), COLOR_MUTED,
    GTextAlignmentCenter);
}

static GColor prv_page_background_color(void) {
  if (s_page == TidePageNow) {
    return COLOR_NOW_CARD;
  }
  if (s_page == TidePageThen && s_event_count > 1) {
    return s_event_highs[1] ? COLOR_HIGH_CARD : COLOR_LOW_CARD;
  }
  if (s_page == TidePageLater && s_event_count > 2) {
    return s_event_highs[2] ? COLOR_HIGH_CARD : COLOR_LOW_CARD;
  }
  return GColorBlack;
}

static void prv_apply_page_background(void) {
  if (s_window) {
    window_set_background_color(s_window, prv_page_background_color());
  }
}

static void prv_content_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, prv_page_background_color());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_tide_count < 2) {
    prv_draw_empty_page(ctx, bounds);
    prv_draw_page_dots(ctx, bounds);
    return;
  }

  switch (s_page) {
    case TidePageOverview:
      prv_draw_overview_page(ctx, bounds);
      break;
    case TidePageNow:
      prv_draw_now_page(ctx, bounds);
      break;
    case TidePageThen:
      prv_draw_then_page(ctx, bounds);
      break;
    case TidePageLater:
      prv_draw_later_page(ctx, bounds);
      break;
    default:
      break;
  }
  prv_draw_page_dots(ctx, bounds);
}

static void prv_set_text(void) {
  time_t now = time(NULL);
  strftime(s_time_display, sizeof(s_time_display), prv_clock_format(), localtime(&now));
  prv_strip_leading_zero(s_time_display);
  text_layer_set_text(s_time_layer, s_time_display);
  text_layer_set_text_color(s_time_layer, s_is_stale ? COLOR_STALE : GColorWhite);
  text_layer_set_text(s_location_layer, s_location);

  GRect time_frame = layer_get_frame(text_layer_get_layer(s_time_layer));
  GSize time_size = graphics_text_layout_get_content_size(s_time_display, s_header_font,
    time_frame, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight);
  GRect icon_frame = layer_get_frame(s_stale_icon_layer);
  icon_frame.origin.x = time_frame.origin.x + time_frame.size.w - time_size.w -
    STALE_ICON_GAP - STALE_ICON_W;
  layer_set_frame(s_stale_icon_layer, icon_frame);

  prv_apply_page_background();
  layer_mark_dirty(s_content_layer);
  layer_mark_dirty(s_stale_icon_layer);
}

static void prv_persist_save(void) {
  persist_write_data(PERSIST_KEY_TIDE_VALUES, s_tide_values, sizeof(s_tide_values));
  persist_write_int(PERSIST_KEY_TIDE_COUNT, s_tide_count);
  persist_write_string(PERSIST_KEY_LOCATION, s_location);
  persist_write_int(PERSIST_KEY_WAVE_HEIGHT, s_wave_height);
  persist_write_int(PERSIST_KEY_SEA_TEMP, s_sea_temp);
  persist_write_int(PERSIST_KEY_CURRENT_MINUTES_AT_SYNC, s_current_minutes_at_sync);
  persist_write_int(PERSIST_KEY_LAST_SYNC_EPOCH, (int32_t)s_last_sync_epoch);
}

static void prv_persist_load(void) {
  if (!persist_exists(PERSIST_KEY_TIDE_VALUES)) return;
  persist_read_data(PERSIST_KEY_TIDE_VALUES, s_tide_values, sizeof(s_tide_values));
  s_tide_count = (int16_t)persist_read_int(PERSIST_KEY_TIDE_COUNT);
  persist_read_string(PERSIST_KEY_LOCATION, s_location, sizeof(s_location));
  s_wave_height = (int16_t)persist_read_int(PERSIST_KEY_WAVE_HEIGHT);
  s_sea_temp = (int16_t)persist_read_int(PERSIST_KEY_SEA_TEMP);
  s_current_minutes_at_sync = (int16_t)persist_read_int(PERSIST_KEY_CURRENT_MINUTES_AT_SYNC);
  s_last_sync_epoch = (time_t)persist_read_int(PERSIST_KEY_LAST_SYNC_EPOCH);
  s_current_minutes = s_current_minutes_at_sync;
}

static void prv_reschedule_wakeup(void) {
  wakeup_cancel_all();
  if (s_background_refresh_enabled) {
    wakeup_schedule(time(NULL) + WAKEUP_INTERVAL_SECONDS, 0, true);
  }
}

static void prv_send_refresh_request(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_tide_refresh_request, 1);
  app_message_outbox_send();
}

static void prv_auto_close_callback(void *context) {
  s_close_timer = NULL;
  window_stack_pop_all(true);
}

static void prv_wakeup_handler(WakeupId wakeup_id, int32_t cookie) {
  if (!quiet_time_is_active()) {
    prv_send_refresh_request();
  }
  prv_reschedule_wakeup();
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *location = dict_find(iterator, MESSAGE_KEY_tide_location);
  Tuple *status = dict_find(iterator, MESSAGE_KEY_tide_status);
  Tuple *sample_offset = dict_find(iterator, MESSAGE_KEY_tide_sample_offset);
  Tuple *values = dict_find(iterator, MESSAGE_KEY_tide_values);
  Tuple *current_minutes = dict_find(iterator, MESSAGE_KEY_tide_current_minutes);
  Tuple *wave_height = dict_find(iterator, MESSAGE_KEY_tide_wave_height);
  Tuple *sea_temp = dict_find(iterator, MESSAGE_KEY_tide_sea_temp);
  Tuple *wave_values = dict_find(iterator, MESSAGE_KEY_tide_wave_values);
  Tuple *sync_complete = dict_find(iterator, MESSAGE_KEY_tide_sync_complete);
  Tuple *background_refresh = dict_find(iterator, MESSAGE_KEY_tide_background_refresh);
  Tuple *units_override = dict_find(iterator, MESSAGE_KEY_tide_units_override);

  prv_copy_cstring_tuple(location, s_location, sizeof(s_location));
  prv_copy_cstring_tuple(status, s_status, sizeof(s_status));
  int16_t offset = 0;
  if (sample_offset) offset = sample_offset->value->int16;
  if (values) {
    if (offset == 0) {
      s_tide_count = 0;
    }
    char value_csv[160];
    if (prv_copy_cstring_tuple(values, value_csv, sizeof(value_csv))) {
      int16_t parsed = prv_parse_values(value_csv, offset, s_tide_values);
      if (parsed > 0 && offset + parsed > s_tide_count) s_tide_count = offset + parsed;
    }
  }
  if (wave_values) {
    if (offset == 0) {
      s_swell_count = 0;
    }
    char wave_csv[160];
    if (prv_copy_cstring_tuple(wave_values, wave_csv, sizeof(wave_csv))) {
      int16_t parsed = prv_parse_values(wave_csv, offset, s_swell_values);
      if (parsed > 0 && offset + parsed > s_swell_count) s_swell_count = offset + parsed;
    }
  }
  if (current_minutes) s_current_minutes = current_minutes->value->int16;
  if (wave_height) s_wave_height = wave_height->value->int16;
  if (sea_temp) s_sea_temp = sea_temp->value->int16;

  if (background_refresh) {
    bool enabled = background_refresh->value->int32 != 0;
    if (enabled != s_background_refresh_enabled) {
      s_background_refresh_enabled = enabled;
      persist_write_bool(PERSIST_KEY_BACKGROUND_REFRESH, s_background_refresh_enabled);
      prv_reschedule_wakeup();
    }
  }

  if (units_override) {
    uint8_t override_value = (uint8_t)units_override->value->uint8;
    if (override_value != s_units_override) {
      s_units_override = override_value;
      persist_write_int(PERSIST_KEY_UNITS_OVERRIDE, s_units_override);
    }
  }

  if (sync_complete) {
    s_current_minutes_at_sync = s_current_minutes;
    s_last_sync_epoch = time(NULL);
    s_is_stale = false;
    prv_persist_save();
  }

  prv_compute_state();
  prv_set_text();
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_last_sync_epoch > 0) {
    time_t elapsed = time(NULL) - s_last_sync_epoch;
    s_current_minutes = s_current_minutes_at_sync + (int16_t)(elapsed / 60);
    s_is_stale = elapsed > STALE_THRESHOLD_SECONDS;
  }
  prv_compute_state();
  prv_set_text();
}

static void prv_double_tap_timeout(void *context) {
  s_double_tap_timer = NULL;
  s_waiting_for_double_tap = false;
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  light_enable_interaction();

  if (s_waiting_for_double_tap) {
    if (s_double_tap_timer) {
      app_timer_cancel(s_double_tap_timer);
      s_double_tap_timer = NULL;
    }
    s_waiting_for_double_tap = false;
    light_enable_interaction();
    return;
  }

  s_waiting_for_double_tap = true;
  s_double_tap_timer = app_timer_register(DOUBLE_TAP_MS, prv_double_tap_timeout, NULL);
}

static TextLayer *prv_make_text_layer(GRect frame, GFont font, GTextAlignment alignment) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_font(layer, font);
  text_layer_set_text_alignment(layer, alignment);
  // Default overflow is word-wrap, which would hard-clip a second line
  // against this single-line header's fixed height. Only used for the
  // location/time header layers, which are exactly where round's narrower
  // top-of-circle width means text needs to ellipsize gracefully, not wrap.
  text_layer_set_overflow_mode(layer, GTextOverflowModeTrailingEllipsis);
  return layer;
}

static void prv_change_page(TidePage page) {
  s_page = page;
  prv_apply_page_background();
  layer_mark_dirty(s_content_layer);
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_page > TidePageOverview) {
    prv_change_page((TidePage)(s_page - 1));
  }
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_page < TidePageLater) {
    prv_change_page((TidePage)(s_page + 1));
  }
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_change_page(TidePageNow);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
}

static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (event->type != TouchEvent_Touchdown) return;
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int16_t third = bounds.size.h / 3;
  if (event->y < third) {
    prv_up_click_handler(NULL, NULL);
  } else if (event->y > third * 2) {
    prv_down_click_handler(NULL, NULL);
  } else {
    prv_select_click_handler(NULL, NULL);
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int16_t sw = bounds.size.w;
  int16_t sh = bounds.size.h;

  GFont header_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  s_header_font = header_font;
  s_large_time_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  s_compact_time_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_text_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18,
    FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_18));
  s_label_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  s_large_detail_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_28_BOLD, FONT_KEY_GOTHIC_28_BOLD,
    FONT_KEY_GOTHIC_24_BOLD));
  s_large_label_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24,
    FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_28, FONT_KEY_GOTHIC_28,
    FONT_KEY_GOTHIC_24));
  // Near the very top of a round display the circle's chord is much
  // narrower than the screen width. Rather than pushing the header far down
  // the circle to get a wide-enough chord for full edge-to-edge text
  // (wastes a big chunk of the screen), pull the text in toward center via
  // HEADER_SIDE_INSET (with ellipsis handling the overflow) and only push
  // down as far as that narrower width actually needs.
  const int16_t top_pad = PBL_IF_ROUND_ELSE(36, 0);
  const int16_t header_h = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    24, 24, 24, 24, 32, 32, 24);

  s_stale_icon_layer = layer_create(GRect(sw / 2, top_pad, STALE_ICON_W, header_h));
  layer_set_update_proc(s_stale_icon_layer, prv_stale_icon_update_proc);
  s_location_layer = prv_make_text_layer(
    GRect(HEADER_SIDE_INSET, top_pad, sw / 2 - HEADER_SIDE_INSET, header_h),
    header_font, GTextAlignmentLeft);
  s_time_layer = prv_make_text_layer(
    GRect(sw / 2, top_pad, sw / 2 - HEADER_SIDE_INSET, header_h),
    header_font, GTextAlignmentRight);
  s_content_layer = layer_create(GRect(0, top_pad + header_h, sw, sh - top_pad - header_h));
  layer_set_update_proc(s_content_layer, prv_content_update_proc);

  layer_add_child(root, s_stale_icon_layer);
  layer_add_child(root, text_layer_get_layer(s_location_layer));
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, s_content_layer);

  prv_compute_state();
  prv_set_text();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_location_layer);
  text_layer_destroy(s_time_layer);
  layer_destroy(s_content_layer);
  layer_destroy(s_stale_icon_layer);
}

static void prv_init(void) {
  s_arrow_up_path = gpath_create(&s_arrow_up_info);
  s_arrow_down_path = gpath_create(&s_arrow_down_info);
  s_arrow_big_up_path = gpath_create(&s_arrow_big_up_info);
  s_arrow_big_down_path = gpath_create(&s_arrow_big_down_info);

  s_background_refresh_enabled = persist_exists(PERSIST_KEY_BACKGROUND_REFRESH)
    ? persist_read_bool(PERSIST_KEY_BACKGROUND_REFRESH) : true;
  s_units_override = persist_exists(PERSIST_KEY_UNITS_OVERRIDE)
    ? (uint8_t)persist_read_int(PERSIST_KEY_UNITS_OVERRIDE) : UNITS_OVERRIDE_AUTO;
  prv_persist_load();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  wakeup_service_subscribe(prv_wakeup_handler);
  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(512, 128);

  bool launched_by_wakeup = launch_reason() == APP_LAUNCH_WAKEUP;
  if (launched_by_wakeup && quiet_time_is_active()) {
    prv_reschedule_wakeup();
    s_close_timer = app_timer_register(10, prv_auto_close_callback, NULL);
  } else {
    prv_send_refresh_request();
    prv_reschedule_wakeup();
    if (launched_by_wakeup) {
      s_close_timer = app_timer_register(AUTO_CLOSE_DELAY_MS, prv_auto_close_callback, NULL);
    }
  }

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  accel_tap_service_subscribe(prv_tap_handler);
  touch_service_subscribe(prv_touch_handler, NULL);
}

static void prv_deinit(void) {
  touch_service_unsubscribe();
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  if (s_double_tap_timer) {
    app_timer_cancel(s_double_tap_timer);
  }
  if (s_close_timer) {
    app_timer_cancel(s_close_timer);
  }
  window_destroy(s_window);
  gpath_destroy(s_arrow_up_path);
  gpath_destroy(s_arrow_down_path);
  gpath_destroy(s_arrow_big_up_path);
  gpath_destroy(s_arrow_big_down_path);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
