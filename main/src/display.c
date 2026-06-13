#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

// --- APP INCLUDES ---
#include "payload_store.h"
#include "../assets/fonts/azeret41/azeret_41.h"
#include "../assets/fonts/sono_21/sono_21.h"

static const char *TAG = "DISPLAY";

// --- E-INK DEFINES ---
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 23 // SDI
#define PIN_NUM_CLK 18  // SCK
#define PIN_NUM_CS 5
#define PIN_NUM_DC 17
#define PIN_NUM_RST 16
#define PIN_NUM_BUSY 4

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#define BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

static spi_device_handle_t spi;

// --- INTERNAL HELPER FUNCTIONS ---

static void eink_gpio_init(void)
{
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_BUSY, GPIO_PULLUP_ONLY);
}

static void eink_send_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_NUM_DC, 0);
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    spi_device_polling_transmit(spi, &t);
}

static void eink_send_data(const uint8_t *data, size_t len)
{
    if (len == 0)
        return;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data};
    spi_device_polling_transmit(spi, &t);
}

static void eink_send_fill(uint8_t val, size_t length)
{
    const size_t chunk_size = 4000;
    uint8_t *chunk = malloc(chunk_size);
    if (!chunk)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for frame buffer chunk");
        return;
    }
    memset(chunk, val, chunk_size);

    size_t remaining = length;
    while (remaining > 0)
    {
        size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;
        eink_send_data(chunk, to_send);
        remaining -= to_send;
    }
    free(chunk);
}

static void wait_until_idle(void)
{
    uint8_t busy;
    do
    {
        eink_send_cmd(0x71);
        busy = gpio_get_level(PIN_NUM_BUSY);
        busy = !(busy & 0x01);
        if (busy)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } while (busy);

    vTaskDelay(pdMS_TO_TICKS(200));
}

static void eink_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 48000};

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
    };

    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
}

// --- RENDERING HELPERS ---

static int measure_string(const char *str, const struct font_char *lookup)
{
    int width = 0;
    while (*str)
    {
        char c = *str++;
        if (c >= 32 && c <= 126)
        {
            width += lookup[c - 32].advance;
        }
    }
    return width;
}

static void draw_char(uint8_t *fb, char c, int *cursor_x, int cursor_y,
                      const struct font_char *lookup, const uint8_t *pixels)
{
    if (c < 32 || c > 126)
        return;

    struct font_char fc = lookup[c - 32];

    for (int r = 0; r < fc.h; r++)
    {
        for (int c_idx = 0; c_idx < fc.w; c_idx++)
        {
            int px_idx = fc.offset + (r * fc.w) + c_idx;
            uint8_t alpha = pixels[px_idx];

            // Thresholding: >127 is considered dark/black
            if (alpha > 127)
            {
                int draw_x = *cursor_x + fc.left + c_idx;
                int draw_y = cursor_y + fc.top + r;

                // Bounds checking
                if (draw_x >= 0 && draw_x < DISPLAY_WIDTH && draw_y >= 0 && draw_y < DISPLAY_HEIGHT)
                {
                    int byte_idx = (draw_y * DISPLAY_WIDTH + draw_x) / 8;
                    int bit_idx = 7 - (draw_x % 8);
                    fb[byte_idx] &= ~(1 << bit_idx); // Clear bit to 0 (Black)
                }
            }
        }
    }
    // Advance the cursor for the next character
    *cursor_x += fc.advance;
}

// --- PUBLIC FUNCTIONS ---

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing SPI and E-ink Display...");
    eink_gpio_init();
    eink_spi_init();
}

void display_draw_ui(void)
{
    ESP_LOGI(TAG, "Generating Dynamic E-ink UI...");

    // 1. Allocate and clear frame buffer to white (0xFF)
    uint8_t *fb = malloc(BUFFER_SIZE);
    if (!fb)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for frame buffer!");
        return;
    }
    memset(fb, 0xFF, BUFFER_SIZE);

    // 2. Fetch data and render to buffer
    date_data_t date;
    if (payload_store_get_date(&date))
    {
        // Define bounding box for the date region
        const int box_width = 240;

        // Measure strings to center them
        int day_width = measure_string(date.day, b_azeret_41_font_lookup);
        int formatted_width = measure_string(date.formatted, b_sono_21_font_lookup);

        int day_x = (box_width - day_width) / 2;
        int formatted_x = (box_width - formatted_width) / 2;

        // Prevent negative cursor start if string exceeds bounding box
        if (day_x < 0)
            day_x = 0;
        if (formatted_x < 0)
            formatted_x = 0;

        // Draw day text (azeret_41) near the top
        int cursor_x = day_x;
        for (int i = 0; i < strlen(date.day); i++)
        {
            draw_char(fb, date.day[i], &cursor_x, 2, b_azeret_41_font_lookup, b_azeret_41_font_pixels);
        }

        // Draw formatted date (sono_21) beneath it
        cursor_x = formatted_x;
        for (int i = 0; i < strlen(date.formatted); i++)
        {
            draw_char(fb, date.formatted[i], &cursor_x, 45, b_sono_21_font_lookup, b_sono_21_font_pixels);
        }
    }

    // ==========================================
    // 3. HARDWARE RESET & EPD Init
    // ==========================================
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    eink_send_cmd(0x01); // POWER SETTING
    const uint8_t pwr_data[] = {0x07, 0x07, 0x3f, 0x3f};
    eink_send_data(pwr_data, 4);

    eink_send_cmd(0x04); // Power on
    wait_until_idle();

    eink_send_cmd(0x00); // PANNEL SETTING
    const uint8_t panel_data[] = {0x1F};
    eink_send_data(panel_data, 1);

    eink_send_cmd(0x61); // Resolution (800x480)
    const uint8_t res_data[] = {0x03, 0x20, 0x01, 0xE0};
    eink_send_data(res_data, 4);

    eink_send_cmd(0x15);
    const uint8_t data_15[] = {0x00};
    eink_send_data(data_15, 1);

    eink_send_cmd(0x50); // VCOM AND DATA INTERVAL SETTING
    const uint8_t vcom_data[] = {0x10, 0x07};
    eink_send_data(vcom_data, 2);

    eink_send_cmd(0x60); // TCON SETTING
    const uint8_t tcon_data[] = {0x22};
    eink_send_data(tcon_data, 1);

    // ==========================================
    // 4. DATA TRANSFER
    // ==========================================
    // Clear old data memory footprint
    eink_send_cmd(0x10);
    eink_send_fill(0xFF, BUFFER_SIZE);

    // Send newly generated frame buffer
    eink_send_cmd(0x13);
    eink_send_data(fb, BUFFER_SIZE);

    // ==========================================
    // 5. REFRESH
    // ==========================================
    eink_send_cmd(0x12);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Waiting for E-ink refresh to complete...");
    wait_until_idle();
    ESP_LOGI(TAG, "E-ink refresh complete!");

    // Free the frame buffer memory
    free(fb);
}

void display_deinit(void)
{
    eink_send_cmd(0x50);
    const uint8_t sleep_vcom[] = {0xf7};
    eink_send_data(sleep_vcom, 1);

    eink_send_cmd(0x02); // power off
    wait_until_idle();

    eink_send_cmd(0x07); // deep sleep
    const uint8_t deep_sleep[] = {0xA5};
    eink_send_data(deep_sleep, 1);

    spi_bus_remove_device(spi);
    spi_bus_free(SPI3_HOST);
}