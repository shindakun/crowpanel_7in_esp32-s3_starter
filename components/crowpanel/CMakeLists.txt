idf_component_register(SRCS "crowpanel.c" "crowpanel_lvgl.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_lvgl_port)
