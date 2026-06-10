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
#define DOUBLE_TAP_MS 500
#define PAGE_MARGIN 8
#define PAGE_DOTS_W 20
#define CARD_GAP 8

typedef enum {
  TidePageOverview = 0,
  TidePageNowNext = 1,
  TidePageThen = 2,
  TidePageLater = 3,
  TidePageCount = 4,
} TidePage;

typedef enum {
  EventCardLayoutLarge,
  EventCardLayoutNext,
  EventCardLayoutSmall,
} EventCardLayout;

static Window *s_window;
static TextLayer *s_location_layer;
static TextLayer *s_time_layer;
static Layer *s_content_layer;

static int16_t s_tide_values[TIDE_POINT_COUNT];
static int16_t s_tide_count;
static int16_t s_current_minutes;
static char s_location[LOCATION_MAX_LEN] = "TidePebble";
static char s_status[STATUS_MAX_LEN] = "Waiting for phone...";

static TidePage s_page = TidePageNowNext;
static int16_t s_event_indices[TIDE_EVENT_COUNT];
static bool s_event_highs[TIDE_EVENT_COUNT];
static int16_t s_event_count;
static bool s_rising = true;
static int16_t s_current_value = 0;
static int16_t s_wave_height = 0;
static int16_t s_sea_temp = 0;
static char s_time_display[6] = "--:--";
static GFont s_text_font;
static GFont s_label_font;
static GFont s_detail_font;
static GFont s_large_detail_font;
static GFont s_chart_font;
static GFont s_overview_label_font;
static GFont s_hero_font;
static GFont s_large_time_font;
static GFont s_compact_time_font;
static AppTimer *s_double_tap_timer;
static bool s_waiting_for_double_tap;

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

static GPoint s_arrow_up_pts[] = {{ARROW_W / 2, 0}, {0, ARROW_H}, {ARROW_W, ARROW_H}};
static GPathInfo s_arrow_up_info = {3, s_arrow_up_pts};
static GPath *s_arrow_up_path;
static GPoint s_arrow_down_pts[] = {{0, 0}, {ARROW_W, 0}, {ARROW_W / 2, ARROW_H}};
static GPathInfo s_arrow_down_info = {3, s_arrow_down_pts};
static GPath *s_arrow_down_path;

static int8_t prv_tide_value_digit(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '-') return 62;
  if (value == '_') return 63;
  return -1;
}

static int16_t prv_parse_values(const char *csv, int16_t offset) {
  int16_t count = 0;
  size_t length = strlen(csv);
  for (size_t chunk_offset = 0;
       chunk_offset + 1 < length && count < TIDE_POINT_COUNT;
       chunk_offset += 2) {
    int8_t high = prv_tide_value_digit(csv[chunk_offset]);
    int8_t low = prv_tide_value_digit(csv[chunk_offset + 1]);
    if (high < 0 || low < 0) continue;
    if (offset + count < TIDE_POINT_COUNT) {
      s_tide_values[offset + count] = ((high << 6) | low) - 2048;
    }
    count += 1;
  }
  return count;
}

static bool prv_is_tide_event(int16_t index, bool high) {
  if (high) {
    return s_tide_values[index] > s_tide_values[index - 1] &&
      s_tide_values[index] >= s_tide_values[index + 1];
  }
  return s_tide_values[index] < s_tide_values[index - 1] &&
    s_tide_values[index] <= s_tide_values[index + 1];
}

static bool prv_use_metric_units(void) {
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
    snprintf(buffer, buffer_size, "~%d.%dm", tenths / 10, tenths % 10);
  } else {
    int32_t feet_tenths = ((int32_t)value_cm * 328084 + 500000) / 1000000;
    snprintf(buffer, buffer_size, "~%ld.%ld'", labs(feet_tenths) / 10, labs(feet_tenths) % 10);
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
  const int16_t label_w = 48;
  int16_t x = center_x - label_w / 2;

  prv_draw_text(ctx, text, s_overview_label_font,
    GRect(x, y, label_w, 24), color, GTextAlignmentCenter);
}

static void prv_draw_chart(GContext *ctx, GRect frame, bool labels) {
  const int16_t mx = labels ? 26 : 6;
  const int16_t my = 6;
  const int16_t w = frame.size.w - mx * 2;
  const int16_t h = frame.size.h - my * 2;
  const int16_t label_h = labels ? 27 : 0;
  const int16_t plot_y = frame.origin.y + my + label_h;
  int16_t plot_h = h - label_h * 2;
  if (plot_h < 10) plot_h = 10;

  if (s_tide_count < 2) {
    graphics_context_set_stroke_color(ctx, COLOR_DIM);
    graphics_draw_rect(ctx, GRect(frame.origin.x + mx, frame.origin.y + my, w, h));
    return;
  }

  int16_t min_v = s_tide_values[0], max_v = s_tide_values[0];
  for (int16_t i = 1; i < s_tide_count; i++) {
    if (s_tide_values[i] < min_v) min_v = s_tide_values[i];
    if (s_tide_values[i] > max_v) max_v = s_tide_values[i];
  }
  if (max_v == min_v) max_v += 1;

  int16_t axis_y = plot_y + plot_h / 2;
  graphics_context_set_stroke_color(ctx, COLOR_DIM);
  graphics_draw_line(ctx, GPoint(frame.origin.x + mx, axis_y),
    GPoint(frame.origin.x + mx + w, axis_y));

  graphics_context_set_stroke_color(ctx, COLOR_BLUE_LINE);
  GPoint prev = GPoint(frame.origin.x + mx, plot_y + plot_h);
  for (int16_t i = 0; i < s_tide_count; i++) {
    int16_t x = frame.origin.x + mx + (w * i / (s_tide_count - 1));
    int16_t y = plot_y + plot_h -
      ((s_tide_values[i] - min_v) * plot_h / (max_v - min_v));
    GPoint pt = GPoint(x, y);
    if (i > 0) {
      graphics_draw_line(ctx, GPoint(prev.x, prev.y - 2), GPoint(pt.x, pt.y - 2));
      graphics_draw_line(ctx, GPoint(prev.x, prev.y - 1), GPoint(pt.x, pt.y - 1));
      graphics_draw_line(ctx, prev, pt);
      graphics_draw_line(ctx, GPoint(prev.x, prev.y + 1), GPoint(pt.x, pt.y + 1));
      graphics_draw_line(ctx, GPoint(prev.x, prev.y + 2), GPoint(pt.x, pt.y + 2));
    }
    prev = pt;
  }

  int16_t cx = frame.origin.x + mx + prv_current_sample_x(w);
  int16_t cy = plot_y + plot_h -
    ((s_current_value - min_v) * plot_h / (max_v - min_v));
  graphics_context_set_fill_color(ctx, COLOR_NOW_HALO);
  graphics_fill_circle(ctx, GPoint(cx, cy), 6);
  graphics_context_set_fill_color(ctx, COLOR_NOW);
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);

  for (int16_t e = 0; e < s_event_count; e++) {
    int16_t index = s_event_indices[e];
    int16_t ex = frame.origin.x + mx + (w * index / (s_tide_count - 1));
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
  GFont detail_font = layout == EventCardLayoutSmall ? s_detail_font : s_large_detail_font;
  int16_t time_h = layout == EventCardLayoutLarge ? 72 : 34;
  int16_t time_y;
  int16_t detail_h = layout == EventCardLayoutSmall ? 22 : 30;
  int16_t detail_y = frame.origin.y + frame.size.h -
    (layout == EventCardLayoutSmall ? 24 : 34);
  if (layout == EventCardLayoutLarge) {
    time_y = frame.origin.y + 46;
  } else if (layout == EventCardLayoutNext) {
    int16_t heading_center = frame.origin.y + 14;
    int16_t detail_center = detail_y + detail_h / 2;
    int16_t time_center = heading_center + (detail_center - heading_center) / 2;
    time_y = time_center - time_h / 2;
  } else {
    time_y = frame.origin.y + 20;
  }
  prv_draw_text(ctx, time_text, time_font,
    GRect(x, time_y, frame.size.w - 18, time_h), GColorWhite, GTextAlignmentLeft);
  int16_t avail_w = frame.size.w - 18;
  GSize dw1 = graphics_text_layout_get_content_size(countdown_text, detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize dw2 = graphics_text_layout_get_content_size("\xe2\x80\xa2", detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize dw3 = graphics_text_layout_get_content_size(height_text, detail_font,
    GRect(0, 0, avail_w, detail_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t dgap = (avail_w - dw1.w - dw2.w - dw3.w) / 2;
  if (dgap < 0) dgap = 0;
  prv_draw_text(ctx, countdown_text, detail_font,
    GRect(x, detail_y, dw1.w, detail_h), GColorWhite, GTextAlignmentLeft);
  prv_draw_text(ctx, "\xe2\x80\xa2", detail_font,
    GRect(x + dw1.w + dgap, detail_y, dw2.w, detail_h), color, GTextAlignmentLeft);
  prv_draw_text(ctx, height_text, detail_font,
    GRect(x + dw1.w + dgap + dw2.w + dgap, detail_y, dw3.w, detail_h),
    GColorWhite, GTextAlignmentLeft);
}

static void prv_draw_now_card(GContext *ctx, GRect frame) {
  char height_text[16], wave_text[16], temp_text[16];
  prv_format_height(height_text, sizeof(height_text), s_current_value);
  prv_format_wave_height(wave_text, sizeof(wave_text), s_wave_height);
  prv_format_sea_temp(temp_text, sizeof(temp_text), s_sea_temp);

  prv_draw_card_background(ctx, frame, GColorBlack);
  int16_t x = frame.origin.x + 12;
  int16_t header_y = frame.origin.y + PAGE_MARGIN;
  int16_t header_h = 28;
  prv_draw_text(ctx, "NOW", s_label_font, GRect(x, header_y, 52, header_h),
    COLOR_NOW, GTextAlignmentLeft);

  GColor trend_color = s_rising ? COLOR_HIGH : COLOR_LOW;
  graphics_context_set_fill_color(ctx, trend_color);
  prv_draw_arrow(ctx, s_rising, x + 58, header_y + 9);
  prv_draw_text(ctx, s_rising ? "Rising" : "Falling", s_label_font,
    GRect(x + 70, header_y, frame.size.w - 88, header_h), trend_color, GTextAlignmentLeft);

  int16_t value_y = header_y + header_h;
  int16_t value_h = frame.size.h - PAGE_MARGIN - (value_y - frame.origin.y);
  int16_t row_h = 30;
  int16_t row_y = value_y + (value_h - row_h) / 2;
  int16_t avail_w = frame.size.w - 18;
  GSize w1 = graphics_text_layout_get_content_size(temp_text, s_large_detail_font,
    GRect(0, 0, avail_w, row_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize w2 = graphics_text_layout_get_content_size(wave_text, s_large_detail_font,
    GRect(0, 0, avail_w, row_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize w3 = graphics_text_layout_get_content_size(height_text, s_large_detail_font,
    GRect(0, 0, avail_w, row_h + 8), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t gap = (avail_w - w1.w - w2.w - w3.w) / 2;
  if (gap < 0) gap = 0;
  prv_draw_text(ctx, temp_text, s_large_detail_font,
    GRect(x, row_y, w1.w, row_h), COLOR_MUTED, GTextAlignmentLeft);
  prv_draw_text(ctx, wave_text, s_large_detail_font,
    GRect(x + w1.w + gap, row_y, w2.w, row_h), COLOR_MUTED, GTextAlignmentLeft);
  prv_draw_text(ctx, height_text, s_large_detail_font,
    GRect(x + w1.w + gap + w2.w + gap, row_y, w3.w, row_h), GColorWhite, GTextAlignmentLeft);
}

static void prv_draw_now_next_page(GContext *ctx, GRect bounds) {
  int16_t content_w = bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W;
  int16_t top_margin = 3;
  int16_t bottom_margin = PAGE_MARGIN;
  int16_t now_h = 84;
  GRect now_card = GRect(PAGE_MARGIN, top_margin, content_w, now_h);
  GRect next_card = GRect(PAGE_MARGIN, top_margin + now_h + CARD_GAP, content_w,
    bounds.size.h - top_margin - bottom_margin - now_h - CARD_GAP);
  prv_draw_now_card(ctx, now_card);
  prv_draw_event_card(ctx, next_card, 0, "NEXT", EventCardLayoutNext);
}

static void prv_draw_then_page(GContext *ctx, GRect bounds) {
  GRect card = GRect(PAGE_MARGIN, PAGE_MARGIN, bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W,
    bounds.size.h - PAGE_MARGIN * 2);
  prv_draw_event_card(ctx, card, 1, "THEN", EventCardLayoutLarge);
}

static void prv_draw_later_page(GContext *ctx, GRect bounds) {
  GRect card = GRect(PAGE_MARGIN, PAGE_MARGIN, bounds.size.w - PAGE_MARGIN - PAGE_DOTS_W,
    bounds.size.h - PAGE_MARGIN * 2);
  prv_draw_event_card(ctx, card, 2, "LATER", EventCardLayoutLarge);
}

static void prv_draw_overview_page(GContext *ctx, GRect bounds) {
  prv_draw_text(ctx, "OVERVIEW", s_label_font,
    GRect(6, 0, bounds.size.w / 2, 28), COLOR_MUTED, GTextAlignmentLeft);
  prv_draw_text(ctx, "NEXT 24H", s_label_font,
    GRect(bounds.size.w / 2 - 2, 0, bounds.size.w / 2 - 18, 28), COLOR_DIM,
    GTextAlignmentRight);
  prv_draw_chart(ctx, GRect(2, 30, bounds.size.w - 18, bounds.size.h - 36), true);
}

static void prv_draw_empty_page(GContext *ctx, GRect bounds) {
  prv_draw_text(ctx, s_status, s_text_font,
    GRect(10, bounds.size.h / 2 - 24, bounds.size.w - 20, 48), COLOR_MUTED,
    GTextAlignmentCenter);
}

static GColor prv_page_background_color(void) {
  if (s_page == TidePageNowNext) {
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
    case TidePageNowNext:
      prv_draw_now_next_page(ctx, bounds);
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
  text_layer_set_text(s_location_layer, s_location);
  prv_apply_page_background();
  layer_mark_dirty(s_content_layer);
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *location = dict_find(iterator, MESSAGE_KEY_tide_location);
  Tuple *status = dict_find(iterator, MESSAGE_KEY_tide_status);
  Tuple *sample_offset = dict_find(iterator, MESSAGE_KEY_tide_sample_offset);
  Tuple *values = dict_find(iterator, MESSAGE_KEY_tide_values);
  Tuple *current_minutes = dict_find(iterator, MESSAGE_KEY_tide_current_minutes);
  Tuple *wave_height = dict_find(iterator, MESSAGE_KEY_tide_wave_height);
  Tuple *sea_temp = dict_find(iterator, MESSAGE_KEY_tide_sea_temp);

  if (location) {
    strncpy(s_location, location->value->cstring, sizeof(s_location) - 1);
    s_location[sizeof(s_location) - 1] = '\0';
  }
  if (status) {
    strncpy(s_status, status->value->cstring, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
  }
  int16_t offset = 0;
  if (sample_offset) offset = sample_offset->value->int16;
  if (values) {
    if (offset == 0) {
      s_tide_count = 0;
    }
    char value_csv[160];
    strncpy(value_csv, values->value->cstring, sizeof(value_csv) - 1);
    value_csv[sizeof(value_csv) - 1] = '\0';
    int16_t parsed = prv_parse_values(value_csv, offset);
    if (offset + parsed > s_tide_count) s_tide_count = offset + parsed;
  }
  if (current_minutes) s_current_minutes = current_minutes->value->int16;
  if (wave_height) s_wave_height = wave_height->value->int16;
  if (sea_temp) s_sea_temp = sea_temp->value->int16;

  prv_compute_state();
  prv_set_text();
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
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
  prv_change_page(TidePageNowNext);
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
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
  s_hero_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  s_large_time_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  s_compact_time_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_text_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18,
    FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_18));
  s_label_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  s_detail_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  s_large_detail_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_28_BOLD, FONT_KEY_GOTHIC_28_BOLD,
    FONT_KEY_GOTHIC_24_BOLD));
  s_overview_label_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  s_chart_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_14));

  const int16_t top_pad = PBL_IF_ROUND_ELSE(8, 0);
  const int16_t header_h = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    24, 24, 24, 24, 32, 32, 24);

  s_location_layer = prv_make_text_layer(
    GRect(4, top_pad, sw / 2 - 4, header_h), header_font, GTextAlignmentLeft);
  s_time_layer = prv_make_text_layer(
    GRect(sw / 2, top_pad, sw / 2 - 4, header_h), header_font, GTextAlignmentRight);
  s_content_layer = layer_create(GRect(0, top_pad + header_h, sw, sh - top_pad - header_h));
  layer_set_update_proc(s_content_layer, prv_content_update_proc);

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
}

static void prv_init(void) {
  s_arrow_up_path = gpath_create(&s_arrow_up_info);
  s_arrow_down_path = gpath_create(&s_arrow_down_info);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(512, 128);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  accel_tap_service_subscribe(prv_tap_handler);
}

static void prv_deinit(void) {
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  if (s_double_tap_timer) {
    app_timer_cancel(s_double_tap_timer);
  }
  window_destroy(s_window);
  gpath_destroy(s_arrow_up_path);
  gpath_destroy(s_arrow_down_path);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
