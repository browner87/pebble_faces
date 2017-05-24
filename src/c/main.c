#include <pebble.h>

static void zap_timer_handler(void *context);
static void main_window_unload(Window *window);
static void deinit();

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_day_layer;
static Layer *s_topline_layer;
static Layer *s_battery_layer;
//static Layer *s_charging_layer;
static TextLayer *s_battery_text_layer;
static int s_today;
static int s_battery_level;
static bool s_last_charge;

const short bbar_w = 30;
const short bbar_h = 15;

// Helper function to make strings upper case because
//  stupid strftime on Pebble doesn't support modifiers
static void make_up(char *s, int n){
    for(char *i = &s[0]; i <= &s[n] ; i++){
        *i = (*i>='a' && *i<='z')?*i&0xdf:*i;
    }
}

////////////////////////////////////////////////////////////////////////////////
// UPDATE functions / drawing
static void update_time(struct tm *tick_time, TimeUnits units_changed) {
    // Write the current hours and minutes into a buffer
    static char s_time_buffer[8];
    strftime(
        s_time_buffer, 
        sizeof(s_time_buffer), 
        clock_is_24h_style() ? "%H:%M" : "%I:%M", 
        tick_time
    );
    // Display this time on the TextLayer
    if(rand()%500){
        text_layer_set_text(s_time_layer, s_time_buffer);
    }else{
        text_layer_set_text(s_time_layer, "NARF");
    }
    
    //////////////////////////////////////////////////
    // Date section - only necessary once a day
    if(tick_time->tm_mday != s_today){
        s_today = tick_time->tm_mday;
        static char s_day_buffer[21]; // WEDNESDAY, SEPTEMBER\0
        static char s_date_buffer[11]; // YYYY-MM-DD\0
        
        strftime(s_date_buffer, sizeof(s_date_buffer), "%Y-%m-%d", tick_time);  // %F aka ISO-8601 date
        make_up(s_date_buffer, 5); // strftime upper case modifier doesn't work
        text_layer_set_text(s_date_layer, s_date_buffer);
        // Update day of week too
        //strcpy(s_day_buffer, "Wednesday, September");
        strftime(s_day_buffer, sizeof(s_day_buffer), "%A-%B", tick_time);
        text_layer_set_text(s_day_layer, s_day_buffer);
    }
}

static void update_topbar(Layer *layer, GContext *ctx) {
    static char s_num_buffer[4]; // 0-100
    GRect layer_bounds = layer_get_bounds(layer);
    
    //////////////////////////////////////////////////
    // Battery Bar    
    GColor8 color = (  // Changing battery/text color based on charge level
        s_battery_level>20?(
            s_battery_level>30?
                 GColorGreen:
                 GColorYellow
        ):GColorRed
    );
    
    // Find the width of the bar (total width = 114px)
    int width = (s_battery_level * bbar_w) / 100;
    int bx = layer_bounds.size.w-bbar_w-5;
    int by = 5;
    int bw = bbar_w;
    int bh = bbar_h;
    // Draw the icon background
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(bx,by,bw,bh), 0, GCornerNone);
    // Draw the icon fill
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(
        ctx, 
        GRect(bx+bw-width, by, width, bh), 
        0, GCornerNone
    );
    // Black squares to make battery shape
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(bx+bw-bbar_w, by, 4, bbar_h/4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(bx+bw-bbar_w, by+bbar_h-(bbar_h/4), 4, bbar_h/4), 0, GCornerNone);
    // Write the battery level
    snprintf(s_num_buffer, 4, "%d", s_battery_level);
    text_layer_set_text(s_battery_text_layer, s_num_buffer);
    text_layer_set_text_color(s_battery_text_layer, color);
    
    //////////////////////////////////////////////////
    // Icon for silent mode
    if( quiet_time_is_active() ){
        // Draw the speaker
        int sx = 5; // Move over when we get a small weather
        int sy = 5;
        int sw = 20;
        int sh = 15;
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_context_set_stroke_color(ctx, GColorRed);
        
        // Magnet
        graphics_fill_rect(ctx, GRect(sx, sy+(sh/5)+1, sw/5, sh-2*(sh/5)-1), 0, GCornerNone);
        // Speaker
        GPathInfo spkrpts = {
            4, 
            (GPoint[]) {
                {sx+sw/5  , sy+(sh/5)}, // Top right point of magnet
                {sx+2*sw/5, sy},        // Top corner
                {sx+2*sw/5, sy+sh},     // Bottom corner
                {sx+sw/5  , sy+sh-sh/5} // Bottom right point of magnet
            }
        };
        GPath *spkrpts_ptr = gpath_create(&spkrpts);
        gpath_draw_outline(ctx, spkrpts_ptr); // Apparently filled doesn't do an outline too...
        gpath_draw_filled(ctx, spkrpts_ptr);
        gpath_destroy(spkrpts_ptr);
        // X
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, GPoint(sx+sw/2+4,sy+sh-4), GPoint(sx+sw, sy+4));
        graphics_draw_line(ctx, GPoint(sx+sw/2+4,sy+4), GPoint(sx+sw, sy+sh-4));
    }
    
    //////////////////////////////////////////////////
    // Icon for weather
    if( false ){ // TODO: Do
        layer_mark_dirty(s_battery_layer); // Redraw the top bar with the graphics
    }
}

static void update_topline(Layer *layer, GContext *ctx) { // Divider line
    GRect bounds = layer_get_bounds(layer);
    GColor8 color = (  // Changing battery/text color based on charge level
        s_battery_level>20?(
            s_battery_level>30?
                 GColorGreen:
                 GColorYellow
        ):GColorRed
    );
    Layer *root_layer = window_get_root_layer(s_main_window);
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(0, bounds.size.h-1), GPoint(bounds.size.w, bounds.size.h-1));
    /*graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(
        ctx, 
        GRect(0, 0, bounds.size.w, bounds.size.h), 
        0, GCornerNone
    );*/
}

////////////////////////////////////////////////////////////////////////////////
// SYSTEM CALLBACK functions
static void battery_callback(BatteryChargeState state) {
    // Record the new battery level
    s_battery_level = state.charge_percent;
    
    // Update meter
    layer_mark_dirty(s_battery_layer);
}

static void bluetooth_callback(bool connected) {
    if(!connected) {
        text_layer_set_text_color(s_time_layer, GColorVividCerulean);
        vibes_long_pulse();
    }else{
        text_layer_set_text_color(s_time_layer, GColorWhite);
    }
}


////////////////////////////////////////////////////////////////////////////////
// INITIALIZATION
static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GFont cfont = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_EXO_BLACK_49));
    GRect bounds = layer_get_bounds(window_layer);
    window_set_background_color(s_main_window, GColorBlack);
    s_last_charge = false;
  
    //////////////////////////////////////////////////
    // TIME
    s_time_layer = text_layer_create( 
        GRect(0, 55, bounds.size.w, 55) 
    );

    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_text(s_time_layer, "00:00");
    text_layer_set_font(s_time_layer, cfont); // Custom number font
    //text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    // Add it as a child layer to the Window's root layer
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  
    //////////////////////////////////////////////////
    // DATE
    s_date_layer = text_layer_create( 
        GRect(0, bounds.size.h-40, bounds.size.w, 25) 
    );
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_text(s_date_layer, "1970-01-01");
    s_today = 40; // No month has 40 days, so force redraw on first run
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    // Add to window
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  
    //////////////////////////////////////////////////
    // DAY OF WEEK
    s_day_layer = text_layer_create( 
        GRect(0, bounds.size.h-40-11, bounds.size.w, 18) 
    );
    text_layer_set_background_color(s_day_layer, GColorClear);
    text_layer_set_text_color(s_day_layer, GColorWhite);
    text_layer_set_text(s_day_layer, "FRAPTIOUS");
    text_layer_set_font(s_day_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_day_layer, GTextAlignmentCenter);
    // Add to window
    layer_add_child(window_layer, text_layer_get_layer(s_day_layer));    
    
    //////////////////////////////////////////////////
    // BATTERY
    s_battery_layer = layer_create(
        GRect(0, 0, bounds.size.w, bounds.size.h)
    );
    layer_set_update_proc(s_battery_layer, update_topbar);
    layer_add_child(window_layer, s_battery_layer);
    // BATTERY text
    s_battery_text_layer = text_layer_create( 
        GRect(bounds.size.w-bbar_w-7-20, 3, 20, bbar_h) 
    );
    text_layer_set_background_color(s_battery_text_layer, GColorClear);
    text_layer_set_text_color(s_battery_text_layer, GColorGreen);
    text_layer_set_text(s_battery_text_layer, "100");
    text_layer_set_font(s_battery_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_battery_text_layer, GTextAlignmentRight);
    // Add to window
    layer_add_child(window_layer, text_layer_get_layer(s_battery_text_layer));
  
    //////////////////////////////////////////////////
    // A LINE
    s_topline_layer = layer_create(
        GRect(0, 0, bounds.size.w, bbar_h+7+5)
    );
    layer_set_update_proc(s_topline_layer, update_topline);
    layer_add_child(window_layer, s_topline_layer);
    layer_mark_dirty(s_topline_layer);
    
    
}






////////////////////////////////////////////////////////////////////////////////
// STARTUP code
static void init() {
    // Create main Window element and assign to pointer
    s_main_window = window_create();

    // Set handlers to manage the elements inside the Window
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });

    // Show the Window on the watch, with animated=true
    window_stack_push(s_main_window, true);

    // Make sure the date/time is displayed from the start
    time_t temp = time(NULL);
    update_time(localtime(&temp), 1);
    // Ensure battery level is displayed from the start
    battery_callback(battery_state_service_peek());

    // Register with TickTimerService
    tick_timer_service_subscribe(MINUTE_UNIT, update_time);
    
    // Get battery change notifications
    battery_state_service_subscribe(battery_callback);
  
    // Register for Bluetooth connection updates
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bluetooth_callback
    });
    bluetooth_callback(connection_service_peek_pebble_app_connection());
}


int main(void) {
    init();
    app_event_loop();
    deinit();
}

////////////////////////////////////////////////////////////////////////////////
// CLEANUP code
static void main_window_unload(Window *window) {
    // Destroy TextLayer
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_day_layer);
    text_layer_destroy(s_battery_text_layer);
    layer_destroy(s_battery_layer);
    
}

static void deinit() {
    // Destroy Window
    window_destroy(s_main_window);
}
