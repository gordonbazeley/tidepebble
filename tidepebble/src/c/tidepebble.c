#include <pebble.h>
#include "message_keys.auto.h"
#include <stdlib.h>
#include <string.h>

#define TIDE_POINT_COUNT 24
#define LOCATION_MAX_LEN 48
#define STATUS_MAX_LEN 48
#define ARROW_W 8
#define ARROW_H 7
#define DOUBLE_TAP_MS 500

static Window *s_window;
static TextLayer *s_location_layer;
static TextLayer *s_time_layer;
static Layer *s_event_label_layer;
static TextLayer *s_hero_layer;
static Layer *s_countdown_layer;
static Layer *s_chart_layer;
static Layer *s_now_row_layer;

static int16_t s_tide_values[TIDE_POINT_COUNT];
static int16_t s_tide_count;
static int16_t s_current_minutes;
static char s_location[LOCATION_MAX_LEN] = "TidePebble";
static char s_status[STATUS_MAX_LEN] = "Waiting for phone...";

static int16_t s_next_index = -1;
static bool s_next_high = true;
static int16_t s_after_next_index = -1;
static bool s_after_next_high = false;
static bool s_rising = true;
static int16_t s_current_value = 0;
static char s_time_display[6] = "--:--";
static char s_hero_text[8] = "--:--";
static char s_countdown_prefix[24] = "";
static char s_countdown_suffix[16] = "";
static char s_now_prefix[28] = "";
static char s_now_suffix[12] = "";
static GFont s_text_font;
static GFont s_label_font;
static GFont s_chart_font;
static GFont s_now_font;
static AppTimer *s_double_tap_timer;
static bool s_waiting_for_double_tap;

// Colors
#define COLOR_HIGH    PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack)
#define COLOR_LOW     PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define COLOR_NOW     PBL_IF_COLOR_ELSE(GColorTiffanyBlue, GColorBlack)
#define COLOR_NOW_HALO PBL_IF_COLOR_ELSE(GColorCeleste, GColorLightGray)

// Filled triangle paths (tip coordinates: up=top-centre, down=bottom-centre)
static GPoint s_arrow_up_pts[]   = {{ARROW_W / 2, 0}, {0, ARROW_H}, {ARROW_W, ARROW_H}};
static GPathInfo s_arrow_up_info  = {3, s_arrow_up_pts};
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
    int8_t low  = prv_tide_value_digit(csv[chunk_offset + 1]);
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

static void prv_format_height(char *buffer, size_t buffer_size, int16_t value) {
  snprintf(buffer, buffer_size, "%s%d.%02d",
    value < 0 ? "-" : "", abs(value) / 100, abs(value) % 100);
}

static void prv_format_time_for_index(char *buffer, size_t buffer_size, int16_t index) {
  time_t now = time(NULL);
  time_t base_time = now - ((time_t)s_current_minutes * 60);
  time_t sample_time = base_time + ((time_t)index * 60 * 60);
  struct tm *sample_tm = localtime(&sample_time);
  strftime(buffer, buffer_size, clock_is_24h_style() ? "%H:%M" : "%I:%M", sample_tm);
}

static void prv_compute_state(void) {
  s_next_index = -1;
  s_after_next_index = -1;
  if (s_tide_count < 2) return;

  int16_t frac = s_current_minutes < 0 ? 0 : (s_current_minutes > 59 ? 59 : s_current_minutes);
  s_current_value = s_tide_values[0] +
    (int16_t)((int32_t)(s_tide_values[1] - s_tide_values[0]) * frac / 60);
  s_rising = s_tide_values[1] > s_tide_values[0];

  for (int16_t i = 1; i < s_tide_count - 1; i++) {
    if (i * 60 <= s_current_minutes) continue;
    bool is_high = prv_is_tide_event(i, true);
    bool is_low  = prv_is_tide_event(i, false);
    if (is_high || is_low) {
      if (s_next_index < 0) {
        s_next_index = i;
        s_next_high  = is_high;
      } else {
        s_after_next_index = i;
        s_after_next_high  = is_high;
        break;
      }
    }
  }
}

static void prv_set_text(void) {
  time_t now = time(NULL);
  strftime(s_time_display, sizeof(s_time_display),
    clock_is_24h_style() ? "%H:%M" : "%I:%M", localtime(&now));
  text_layer_set_text(s_time_layer, s_time_display);
  text_layer_set_text(s_location_layer, s_location);

  if (s_tide_count < 2) {
    text_layer_set_text(s_hero_layer, "--:--");
    s_countdown_prefix[0] = '\0';
    s_now_prefix[0] = '\0';
    layer_mark_dirty(s_event_label_layer);
    layer_mark_dirty(s_countdown_layer);
    layer_mark_dirty(s_now_row_layer);
    layer_mark_dirty(s_chart_layer);
    return;
  }

  if (s_next_index >= 0) {
    prv_format_time_for_index(s_hero_text, sizeof(s_hero_text), s_next_index);
    text_layer_set_text(s_hero_layer, s_hero_text);

    int16_t minutes_to = s_next_index * 60 - s_current_minutes;
    int16_t h = minutes_to / 60;
    int16_t m = minutes_to % 60;
    char ht[12];
    prv_format_height(ht, sizeof(ht), s_tide_values[s_next_index]);
    if (h > 0) {
      snprintf(s_countdown_prefix, sizeof(s_countdown_prefix), "In %dh %02dm  ", h, m);
    } else {
      snprintf(s_countdown_prefix, sizeof(s_countdown_prefix), "In %dm  ", m);
    }
    snprintf(s_countdown_suffix, sizeof(s_countdown_suffix), "%sm", ht);
  } else {
    text_layer_set_text(s_hero_layer, "--:--");
    s_countdown_prefix[0] = '\0';
  }

  char ht[12];
  prv_format_height(ht, sizeof(ht), s_current_value);
  snprintf(s_now_prefix, sizeof(s_now_prefix), "Now %sm  ", ht);
  strncpy(s_now_suffix, s_rising ? "Rising" : "Falling", sizeof(s_now_suffix) - 1);

  layer_mark_dirty(s_event_label_layer);
  layer_mark_dirty(s_countdown_layer);
  layer_mark_dirty(s_now_row_layer);
  layer_mark_dirty(s_chart_layer);
}

static void prv_draw_countdown(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_countdown_prefix[0] == '\0') {
    // No tide data — show status centred in regular gray
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    graphics_draw_text(ctx, s_status, s_text_font,
      GRect(0, 0, bounds.size.w, bounds.size.h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  GRect mbox = GRect(0, 0, 200, bounds.size.h);
  GSize ps = graphics_text_layout_get_content_size(s_countdown_prefix, s_label_font, mbox,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize ss = graphics_text_layout_get_content_size(s_countdown_suffix, s_now_font, mbox,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t x = (bounds.size.w - ps.w - ss.w) / 2;
  if (x < 2) x = 2;
  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_text(ctx, s_countdown_prefix, s_label_font,
    GRect(x, 0, ps.w + 4, bounds.size.h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_countdown_suffix, s_now_font,
    GRect(x + ps.w, 0, ss.w + 8, bounds.size.h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Draw a filled triangle. For "up": tip at (x + ARROW_W/2, y). For "down": tip at (x + ARROW_W/2, y + ARROW_H).
static void prv_draw_arrow(GContext *ctx, bool up, int16_t x, int16_t y) {
  GPath *path = up ? s_arrow_up_path : s_arrow_down_path;
  gpath_move_to(path, GPoint(x, y));
  gpath_draw_filled(ctx, path);
}

// Draw a chart marker: green circle for HIGH, red downward triangle (tip at px,py) for LOW.
static void prv_draw_chart_event(GContext *ctx, bool high, int16_t px, int16_t py) {
  if (high) {
    graphics_context_set_fill_color(ctx, COLOR_HIGH);
    graphics_fill_circle(ctx, GPoint(px, py), 3);
  } else {
    graphics_context_set_fill_color(ctx, COLOR_LOW);
    // Triangle tip at (px, py): origin is tip_x - ARROW_W/2, tip_y - ARROW_H
    prv_draw_arrow(ctx, false, px - ARROW_W / 2, py - ARROW_H);
  }
}

static void prv_draw_event_row(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_tide_count < 2 || s_next_index < 0) return;

  const char *label = s_next_high ? " NEXT HIGH" : " NEXT LOW";
  GColor color = s_next_high ? COLOR_HIGH : COLOR_LOW;

  GSize text_size = graphics_text_layout_get_content_size(label, s_label_font,
    GRect(0, 0, 200, bounds.size.h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t total_w = ARROW_W + text_size.w;
  int16_t x = (bounds.size.w - total_w) / 2;
  int16_t arrow_y = (bounds.size.h - ARROW_H) / 2;

  graphics_context_set_fill_color(ctx, color);
  prv_draw_arrow(ctx, s_next_high, x, arrow_y);

  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, label, s_label_font,
    GRect(x + ARROW_W, 0, text_size.w + 4, bounds.size.h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_chart(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int16_t mx = 2, my = 2;
  const int16_t w = bounds.size.w - mx * 2;
  const int16_t h = bounds.size.h - my * 2;

  if (s_tide_count < 2) {
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_rect(ctx, GRect(mx, my, w, h));
    return;
  }

  int16_t min_v = s_tide_values[0], max_v = s_tide_values[0];
  for (int16_t i = 1; i < s_tide_count; i++) {
    if (s_tide_values[i] < min_v) min_v = s_tide_values[i];
    if (s_tide_values[i] > max_v) max_v = s_tide_values[i];
  }
  if (max_v == min_v) max_v += 1;

  // Tide sparkline (3px thick)
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorBlue, GColorBlack));
  GPoint prev = GPoint(mx, my + h);
  for (int16_t i = 0; i < s_tide_count; i++) {
    int16_t x = mx + (w * i / (s_tide_count - 1));
    int16_t y = my + h - ((s_tide_values[i] - min_v) * h / (max_v - min_v));
    GPoint pt = GPoint(x, y);
    if (i > 0) {
      graphics_draw_line(ctx, GPoint(prev.x, prev.y - 1), GPoint(pt.x, pt.y - 1));
      graphics_draw_line(ctx, prev, pt);
      graphics_draw_line(ctx, GPoint(prev.x, prev.y + 1), GPoint(pt.x, pt.y + 1));
    }
    prev = pt;
  }

  // Current position
  int16_t frac = s_current_minutes < 0 ? 0 : (s_current_minutes > 59 ? 59 : s_current_minutes);
  int16_t cx = mx + (w * frac / (60 * (s_tide_count - 1)));
  int16_t cy = my + h - ((s_current_value - min_v) * h / (max_v - min_v));

  // Vertical "now" line from dot up to top of chart
  graphics_context_set_stroke_color(ctx, COLOR_NOW);
  if (cy > my + 5) {
    graphics_draw_line(ctx, GPoint(cx, my), GPoint(cx, cy - 5));
  }

  // Teal halo + teal dot
  graphics_context_set_fill_color(ctx, COLOR_NOW_HALO);
  graphics_fill_circle(ctx, GPoint(cx, cy), 5);
  graphics_context_set_fill_color(ctx, COLOR_NOW);
  graphics_fill_circle(ctx, GPoint(cx, cy), 3);

  // Next event marker (green circle HIGH, red ▼ tip-at-curve for LOW)
  if (s_next_index >= 0) {
    int16_t ex = mx + (w * s_next_index / (s_tide_count - 1));
    int16_t ey = my + h - ((s_tide_values[s_next_index] - min_v) * h / (max_v - min_v));
    prv_draw_chart_event(ctx, s_next_high, ex, ey);
  }

  // Tide-after-next marker + time label
  if (s_after_next_index >= 0) {
    int16_t ax = mx + (w * s_after_next_index / (s_tide_count - 1));
    int16_t ay = my + h - ((s_tide_values[s_after_next_index] - min_v) * h / (max_v - min_v));
    prv_draw_chart_event(ctx, s_after_next_high, ax, ay);

    // Time label above/below the marker, clamped to chart
    char after_time[6];
    prv_format_time_for_index(after_time, sizeof(after_time), s_after_next_index);
    GColor label_color = s_after_next_high ? COLOR_HIGH : COLOR_LOW;
    graphics_context_set_text_color(ctx, label_color);
    // Use bold label font; size the box to fit it on each platform
    const int16_t label_w = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
      52, 52, 52, 52, 68, 68, 52);
    const int16_t label_h = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
      22, 22, 22, 22, 28, 28, 22);
    int16_t lx = ax - label_w / 2;
    if (lx < mx) lx = mx;
    if (lx > mx + w - label_w) lx = mx + w - label_w;
    int16_t ly = s_after_next_high ? my + h - label_h - 2 : my + 2;
    GRect label_box = GRect(lx, ly, label_w, label_h);
    graphics_context_set_text_color(ctx, label_color);
    graphics_draw_text(ctx, after_time, s_label_font,
      label_box,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void prv_draw_now_row(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (s_now_prefix[0] == '\0') return;

  GRect measure_box = GRect(0, 0, 200, bounds.size.h);
  GSize ps = graphics_text_layout_get_content_size(s_now_prefix, s_now_font, measure_box,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  GSize ss = graphics_text_layout_get_content_size(s_now_suffix, s_now_font, measure_box,
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

  int16_t total_w = ps.w + ARROW_W + 4 + ss.w;
  int16_t x = (bounds.size.w - total_w) / 2;
  if (x < 2) x = 2;
  int16_t arrow_y = (bounds.size.h - ARROW_H) / 2;

  GColor trend_color = s_rising ? COLOR_NOW : COLOR_LOW;

  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_text(ctx, s_now_prefix, s_now_font,
    GRect(x, 0, ps.w + 4, bounds.size.h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  graphics_context_set_fill_color(ctx, trend_color);
  prv_draw_arrow(ctx, s_rising, x + ps.w, arrow_y);

  graphics_context_set_text_color(ctx, trend_color);
  graphics_draw_text(ctx, s_now_suffix, s_now_font,
    GRect(x + ps.w + ARROW_W + 4, 0, ss.w + 8, bounds.size.h),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *location       = dict_find(iterator, MESSAGE_KEY_tide_location);
  Tuple *status         = dict_find(iterator, MESSAGE_KEY_tide_status);
  Tuple *sample_offset  = dict_find(iterator, MESSAGE_KEY_tide_sample_offset);
  Tuple *values         = dict_find(iterator, MESSAGE_KEY_tide_values);
  Tuple *current_minutes = dict_find(iterator, MESSAGE_KEY_tide_current_minutes);

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
    char value_csv[160];
    strncpy(value_csv, values->value->cstring, sizeof(value_csv) - 1);
    value_csv[sizeof(value_csv) - 1] = '\0';
    int16_t parsed = prv_parse_values(value_csv, offset);
    if (offset + parsed > s_tide_count) s_tide_count = offset + parsed;
  }
  if (current_minutes) s_current_minutes = current_minutes->value->int16;

  prv_compute_state();
  prv_set_text();
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_set_text();
}

static void prv_double_tap_timeout(void *context) {
  s_double_tap_timer = NULL;
  s_waiting_for_double_tap = false;
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
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
  text_layer_set_text_color(layer, GColorBlack);
  text_layer_set_font(layer, font);
  text_layer_set_text_alignment(layer, alignment);
  return layer;
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
  GFont hero_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  // Countdown text (lighter weight matches the design's gray "in X · ")
  s_text_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18,
    FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_18));
  // Bold font for event label row ("▲ NEXT HIGH") and now row
  s_label_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));
  // Small font for in-chart time labels only
  s_chart_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_14));
  // Bold font for now row
  s_now_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_18_BOLD));

  const int16_t header_h    = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    22, 22, 22, 22, 30, 30, 22);
  // event_h must fit s_label_font (GOTHIC_18_BOLD ~20px, GOTHIC_24_BOLD ~26px)
  const int16_t event_h     = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    22, 22, 22, 22, 28, 28, 22);
  // BITHAM_42_BOLD renders ~52px on every platform — keep hero_h fixed
  const int16_t hero_h      = 52;
  const int16_t countdown_h = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    22, 22, 22, 22, 28, 28, 22);
  // now_h must fit s_now_font (GOTHIC_18_BOLD ~20px, GOTHIC_24_BOLD ~26px)
  const int16_t now_h       = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    22, 22, 22, 22, 28, 28, 22);
  const int16_t top_pad = PBL_IF_ROUND_ELSE(8, 0);

  int16_t chart_h_raw = sh - top_pad - header_h - event_h - hero_h - countdown_h - now_h;
  int16_t chart_h = chart_h_raw > 10 ? chart_h_raw : 10;

  int16_t y = top_pad;

  s_location_layer = prv_make_text_layer(
    GRect(4, y, sw / 2 - 4, header_h), header_font, GTextAlignmentLeft);
  s_time_layer = prv_make_text_layer(
    GRect(sw / 2, y, sw / 2 - 4, header_h), header_font, GTextAlignmentRight);
  y += header_h;

  s_event_label_layer = layer_create(GRect(0, y, sw, event_h + 2));
  layer_set_update_proc(s_event_label_layer, prv_draw_event_row);
  y += event_h;

  s_hero_layer = prv_make_text_layer(
    GRect(0, y, sw, hero_h + 6), hero_font, GTextAlignmentCenter);
  y += hero_h;

  s_countdown_layer = layer_create(GRect(0, y, sw, countdown_h));
  layer_set_update_proc(s_countdown_layer, prv_draw_countdown);
  y += countdown_h;

  s_chart_layer = layer_create(GRect(0, y, sw, chart_h));
  layer_set_update_proc(s_chart_layer, prv_draw_chart);
  y += chart_h;

  s_now_row_layer = layer_create(GRect(0, y, sw, now_h));
  layer_set_update_proc(s_now_row_layer, prv_draw_now_row);

  layer_add_child(root, text_layer_get_layer(s_location_layer));
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, s_event_label_layer);
  layer_add_child(root, text_layer_get_layer(s_hero_layer));
  layer_add_child(root, s_countdown_layer);
  layer_add_child(root, s_chart_layer);
  layer_add_child(root, s_now_row_layer);

  prv_compute_state();
  prv_set_text();
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_location_layer);
  text_layer_destroy(s_time_layer);
  layer_destroy(s_event_label_layer);
  text_layer_destroy(s_hero_layer);
  layer_destroy(s_countdown_layer);
  layer_destroy(s_chart_layer);
  layer_destroy(s_now_row_layer);
}

static void prv_init(void) {
  s_arrow_up_path   = gpath_create(&s_arrow_up_info);
  s_arrow_down_path = gpath_create(&s_arrow_down_info);

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = prv_window_load,
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
