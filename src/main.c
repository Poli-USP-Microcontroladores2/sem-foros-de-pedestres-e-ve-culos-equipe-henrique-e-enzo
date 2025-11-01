#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define LED_GREEN_NODE_SLEEP_TIME_MS 4000
#define LED_RED_NODE_SLEEP_TIME_MS 2000

// Define os LEDs usando Device Tree
#define LED_RED_NODE DT_NODELABEL(red_led)
#define LED_GREEN_NODE DT_NODELABEL(green_led)

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

void main(void) {

    // Verifica se os devices estão prontos
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        printk("Error: LED devices is not ready\n");
        return;
    }

    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);

    while (1) {
        // LED vermelho
        gpio_pin_set_dt(&led_red, 1);
        k_msleep(LED_RED_NODE_SLEEP_TIME_MS);
		gpio_pin_set_dt(&led_red, 0);

		// LED verde
		gpio_pin_set_dt(&led_green, 1);
        k_msleep(LED_GREEN_NODE_SLEEP_TIME_MS);
		gpio_pin_set_dt(&led_green, 0);
	}
}