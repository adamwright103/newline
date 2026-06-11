#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DISPLAY";

// --- E-INK DEFINES ---
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 23 // SDI
#define PIN_NUM_CLK 18  // SCK
#define PIN_NUM_CS 5
#define PIN_NUM_DC 17
#define PIN_NUM_RST 16
#define PIN_NUM_BUSY 4

// Declare the embedded image assembly symbols
extern const uint8_t static_ui_start[] asm("_binary_static_ui_bin_start");
extern const uint8_t static_ui_end[] asm("_binary_static_ui_bin_end");

static spi_device_handle_t spi;

// --- INTERNAL HELPER FUNCTIONS ---

static void eink_gpio_init(void)
{
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_BUSY, GPIO_MODE_INPUT);
    // Note: Adjust pullup if your hardware requires it, matches Arduino INPUT
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

// Memory-friendly helper to fill EPD RAM with a specific byte (0xFF or 0x00)
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

// Exactly matches Arduino lcd_chkstatus()
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
            vTaskDelay(pdMS_TO_TICKS(10)); // Yield to prevent Watchdog timeout
        }
    } while (busy);

    // Mandatory delay from Arduino driver_delay_xms(200)
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

// --- PUBLIC FUNCTIONS ---

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing SPI and E-ink Display...");
    eink_gpio_init();
    eink_spi_init();
}

void display_draw_static_ui(void)
{
    size_t image_size = static_ui_end - static_ui_start;
    ESP_LOGI(TAG, "Drawing E-ink UI. Image size: %zu bytes", image_size);

    // ==========================================
    // 1. HARDWARE RESET & EPD_init() equivalents
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
    // 2. DATA TRANSFER (PIC_display1 equivalent)
    // ==========================================

    // Transfer old data (Fill 48000 bytes with 0xFF)
    eink_send_cmd(0x10);
    eink_send_fill(0xFF, 48000);

    // Transfer new data (Image file + zero-padding up to 48000)
    eink_send_cmd(0x13);

    // Cap at screen maximum just in case the bin is slightly larger
    size_t transfer_size = (image_size > 48000) ? 48000 : image_size;
    eink_send_data(static_ui_start, transfer_size);

    // If the image is smaller than 48000, pad the rest with 0x00
    if (transfer_size < 48000)
    {
        eink_send_fill(0x00, 48000 - transfer_size);
    }

    // ==========================================
    // 3. REFRESH (EPD_refresh equivalent)
    // ==========================================
    eink_send_cmd(0x12);
    vTaskDelay(pdMS_TO_TICKS(100)); // Mandatory 100ms delay from Arduino logic
    ESP_LOGI(TAG, "Waiting for E-ink refresh to complete...");
    wait_until_idle();
    ESP_LOGI(TAG, "E-ink refresh complete!");
}

void display_deinit(void)
{
    // ==========================================
    // 4. SLEEP (EPD_sleep equivalent)
    // ==========================================
    eink_send_cmd(0x50);
    const uint8_t sleep_vcom[] = {0xf7};
    eink_send_data(sleep_vcom, 1);

    eink_send_cmd(0x02); // power off
    wait_until_idle();

    eink_send_cmd(0x07); // deep sleep
    const uint8_t deep_sleep[] = {0xA5};
    eink_send_data(deep_sleep, 1);

    // Free up the SPI bus before sleeping the ESP32
    spi_bus_remove_device(spi);
    spi_bus_free(SPI3_HOST);
}