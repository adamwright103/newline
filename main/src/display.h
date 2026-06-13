#ifndef DISPLAY_H
#define DISPLAY_H

/**
 * @brief Initialize GPIO and SPI for the E-ink display
 */
void display_init(void);

/**
 * @brief Dynamically generate and draw the UI, then refresh the screen
 */
void display_draw_ui(void);

/**
 * @brief Send the sleep command and free the SPI bus
 */
void display_deinit(void);

#endif // DISPLAY_H