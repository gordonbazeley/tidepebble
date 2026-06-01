#include <pebble.h>
#include "message_keys.auto.h"
#include <stdlib.h>
#include <string.h>

#define TIDE_POINT_COUNT 24
#define TIDE_EVENT_COUNT 4
#define MIN_TIDE_EVENT_GAP_HOURS 4
#define LOCATION_MAX_LEN 48
#define STATUS_MAX_LEN 48

static Window *s_window;
static Layer *s_chart_layer;
static TextLayer *s_time_layer;
static TextLayer *s_status_layer;
static int16_t s_tide_values[TIDE_POINT_COUNT];
static int16_t s_tide_count;
static int16_t s_current_minutes;
static char s_location[LOCATION_MAX_LEN] = "Finding nearest tide data";
static char s_status[STATUS_MAX_LEN] = "Waiting for phone location...";
static char s_header[LOCATION_MAX_LEN + 8] = "";

static int8_t prv_tide_value_digit(char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '-') {
    return 62;
  }
  if (value == '_') {
    return 63;
  }
  return -1;
}

static void prv_set_text(void) {
  time_t now = time(NULL);
  char time_text[6];
  strftime(time_text, sizeof(time_text), clock_is_24h_style() ? "%H:%M" : "%I:%M", localtime(&now));
  snprintf(s_header, sizeof(s_header), "%s %s", time_text, s_location);
  text_layer_set_text(s_time_layer, s_header);
  text_layer_set_text(s_status_layer, s_status);
}

static int16_t prv_parse_values(const char *csv, int16_t offset) {
  int16_t count = 0;
  size_t length = strlen(csv);
  for (size_t chunk_offset = 0; chunk_offset + 1 < length && count < TIDE_POINT_COUNT; chunk_offset += 2) {
    int8_t high = prv_tide_value_digit(csv[chunk_offset]);
    int8_t low = prv_tide_value_digit(csv[chunk_offset + 1]);
    if (high < 0 || low < 0) {
      continue;
    }
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

static int16_t prv_find_strongest_event(bool high, int16_t except) {
  int16_t strongest = -1;
  for (int16_t i = 1; i < s_tide_count - 1; i += 1) {
    if (!prv_is_tide_event(i, high) ||
        (except >= 0 && abs(i - except) < MIN_TIDE_EVENT_GAP_HOURS)) {
      continue;
    }
    if (strongest < 0 ||
        (high && s_tide_values[i] > s_tide_values[strongest]) ||
        (!high && s_tide_values[i] < s_tide_values[strongest])) {
      strongest = i;
    }
  }
  return strongest;
}

static int16_t prv_find_tide_events(int16_t *event_indexes) {
  int16_t count = 0;
  for (int16_t kind = 0; kind < 2; kind += 1) {
    bool high = kind == 0;
    int16_t strongest = prv_find_strongest_event(high, -1);
    if (strongest >= 0) {
      event_indexes[count++] = strongest;
    }
    int16_t second = prv_find_strongest_event(high, strongest);
    if (second >= 0) {
      event_indexes[count++] = second;
    }
  }
  for (int16_t i = 1; i < count; i += 1) {
    int16_t event = event_indexes[i];
    int16_t position = i;
    while (position > 0 && event_indexes[position - 1] > event) {
      event_indexes[position] = event_indexes[position - 1];
      position -= 1;
    }
    event_indexes[position] = event;
  }
  return count;
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
  strftime(buffer, buffer_size, "%H:%M", sample_tm);
}

static bool prv_rects_overlap(GRect first, GRect second) {
  return first.origin.x < second.origin.x + second.size.w &&
    first.origin.x + first.size.w > second.origin.x &&
    first.origin.y < second.origin.y + second.size.h &&
    first.origin.y + first.size.h > second.origin.y;
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *location = dict_find(iterator, MESSAGE_KEY_tide_location);
  Tuple *status = dict_find(iterator, MESSAGE_KEY_tide_status);
  Tuple *sample_offset = dict_find(iterator, MESSAGE_KEY_tide_sample_offset);
  Tuple *values = dict_find(iterator, MESSAGE_KEY_tide_values);
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
  if (sample_offset) {
    offset = sample_offset->value->int16;
  }
  if (values) {
    char value_csv[160];
    strncpy(value_csv, values->value->cstring, sizeof(value_csv) - 1);
    value_csv[sizeof(value_csv) - 1] = '\0';
    int16_t parsed = prv_parse_values(value_csv, offset);
    if (offset + parsed > s_tide_count) {
      s_tide_count = offset + parsed;
    }
  }
  if (current_minutes) {
    s_current_minutes = current_minutes->value->int16;
  }

  prv_set_text();
  layer_mark_dirty(s_chart_layer);
}

static void prv_draw_chart(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));

  if (s_tide_count < 2) {
    graphics_draw_rect(ctx, GRect(8, 12, bounds.size.w - 16, bounds.size.h - 24));
    return;
  }

  int16_t min_value = s_tide_values[0];
  int16_t max_value = s_tide_values[0];
  for (int16_t i = 1; i < s_tide_count; i += 1) {
    if (s_tide_values[i] < min_value) {
      min_value = s_tide_values[i];
    }
    if (s_tide_values[i] > max_value) {
      max_value = s_tide_values[i];
    }
  }
  if (max_value == min_value) {
    max_value += 1;
  }

  const int16_t margin = 4;
  const int16_t left = margin;
  const int16_t top = margin;
  const int16_t width = bounds.size.w - (margin * 2);
  const int16_t height = bounds.size.h - (margin * 2);
  char max_label[12];
  char min_label[12];
  prv_format_height(max_label, sizeof(max_label), max_value);
  prv_format_height(min_label, sizeof(min_label), min_value);
  GFont axis_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18,
    FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18));
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, max_label, axis_font,
    GRect(left, top - 1, 56, 18),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, min_label, axis_font,
    GRect(left, top + height - 17, 56, 18),
    GTextOverflowModeTrailingEllipsis,
    GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, GColorBlack);

  graphics_context_set_stroke_color(ctx, GColorLightGray);
  for (int16_t i = 0; i <= 2; i += 1) {
    int16_t y = top + (height * i / 2);
    graphics_draw_line(ctx, GPoint(left, y), GPoint(left + width, y));
  }

  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorBlue, GColorBlack));
  GPoint previous = GPoint(left, top + height);
  for (int16_t i = 0; i < s_tide_count; i += 1) {
    int16_t x = left + (width * i / (s_tide_count - 1));
    int16_t y = top + height -
      ((s_tide_values[i] - min_value) * height / (max_value - min_value));
    GPoint point = GPoint(x, y);
    if (i > 0) {
      graphics_draw_line(ctx, previous, point);
      graphics_draw_line(ctx, GPoint(previous.x, previous.y + 1), GPoint(point.x, point.y + 1));
    }
    previous = point;
  }

  int16_t current_fraction = s_current_minutes < 0 ? 0 :
    s_current_minutes > 60 ? 60 : s_current_minutes;
  int16_t current_value = s_tide_values[0] +
    ((s_tide_values[1] - s_tide_values[0]) * current_fraction / 60);
  int16_t current_x = left + (width * current_fraction / (60 * (s_tide_count - 1)));
  int16_t current_y = top + height -
    ((current_value - min_value) * height / (max_value - min_value));
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorGreen, GColorBlack));
  graphics_fill_circle(ctx, GPoint(current_x, current_y), 3);

  int16_t event_indexes[TIDE_EVENT_COUNT];
  int16_t event_count = prv_find_tide_events(event_indexes);
  GRect label_rects[TIDE_EVENT_COUNT];
  for (int16_t event = 0; event < event_count; event += 1) {
    int16_t i = event_indexes[event];
    int16_t x = left + (width * i / (s_tide_count - 1));
    int16_t y = top + height -
      ((s_tide_values[i] - min_value) * height / (max_value - min_value));
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlue, GColorBlack));
    graphics_fill_circle(ctx, GPoint(x, y), 2);
    const int16_t label_width = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
      50, 50, 50, 50, 62, 62, 62);
    const int16_t label_height = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
      23, 23, 23, 23, 29, 29, 29);
    const int16_t vertical_offsets[] = { -14, -43, 14, -72, 43 };
    GRect label_rect = GRect(0, 0, label_width, label_height);
    bool positioned = false;
    for (int16_t side = 0; side < 2 && !positioned; side += 1) {
      for (int16_t offset = 0; offset < (int16_t)ARRAY_LENGTH(vertical_offsets) && !positioned; offset += 1) {
        bool place_left = side == 1;
        int16_t label_x = place_left ? x - label_width - 4 : x + 4;
        int16_t label_y = y + vertical_offsets[offset];
        if (label_x < 0 || label_x > bounds.size.w - label_width) {
          continue;
        }
        label_y = label_y < 0 ? 0 : label_y;
        label_y = label_y > bounds.size.h - label_height ? bounds.size.h - label_height : label_y;
        label_rect = GRect(label_x, label_y, label_width, label_height);
        bool overlaps = false;
        for (int16_t previous_label = 0; previous_label < event; previous_label += 1) {
          if (prv_rects_overlap(label_rect, label_rects[previous_label])) {
            overlaps = true;
            break;
          }
        }
        positioned = !overlaps;
      }
    }
    label_rects[event] = label_rect;
    graphics_context_set_text_color(ctx, GColorBlack);
    GFont event_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
      FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
      FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
      FONT_KEY_GOTHIC_24_BOLD));
    char event_time[6];
    prv_format_time_for_index(event_time, sizeof(event_time), i);
    graphics_draw_text(ctx, event_time, event_font,
      GRect(label_rect.origin.x, label_rect.origin.y - 1, label_width, label_height + 1),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentCenter, NULL);
  }

}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_set_text();
}

static TextLayer *prv_text_layer(GRect frame, GFont font, GTextAlignment alignment) {
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
  int16_t chart_bottom = bounds.size.h;
  int16_t header_height = PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    28, 28, 28, 28, 34, 34, 34);
  GFont header_font = fonts_get_system_font(PBL_PLATFORM_SWITCH(PBL_PLATFORM_TYPE_CURRENT,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD,
    FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_24_BOLD,
    FONT_KEY_GOTHIC_24_BOLD));

  s_time_layer = prv_text_layer(GRect(2, 0, bounds.size.w - 4, header_height),
    header_font, GTextAlignmentCenter);
  s_chart_layer = layer_create(GRect(0, header_height, bounds.size.w, chart_bottom - header_height));
  layer_set_update_proc(s_chart_layer, prv_draw_chart);
  s_status_layer = prv_text_layer(GRect(5, chart_bottom, bounds.size.w - 10, 0),
    fonts_get_system_font(FONT_KEY_GOTHIC_14), GTextAlignmentCenter);

  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, s_chart_layer);
  layer_add_child(root, text_layer_get_layer(s_status_layer));
  prv_set_text();

  time_t now = time(NULL);
  prv_tick_handler(localtime(&now), MINUTE_UNIT);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  layer_destroy(s_chart_layer);
  text_layer_destroy(s_status_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(512, 128);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
