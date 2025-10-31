#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define LED0_SLEEP_TIME_MS 1000
#define LED1_SLEEP_TIME_MS 350

// Define os LEDs usando Device Tree
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

// Verifica se estão definidos no Device Tree
#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED1_NODE, okay)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);


void main(void) {
    int ret;

    // Verifica se os devices estão prontos
    if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1)) {
        printk("Error: LED devices is not ready\n");
        return;
    }

    // Configuras os pinos como saída
    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error %d: failed to configure LED0 pin\n", ret);
        return;
    }

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        printk("Error %d: failed to configure LED1 pin\n", ret);
        return;
    }

    printk("Blinking dois LEDs: LED0=%s pin %d, LED1=%s pin %d\n", led0.port->name, led0.pin, led1.port->name, led1.pin);

	//Contadores para controlar o tempo de cada LED
	int led0_timer = 0;
	int led1_timer = 0;
	const int step_ms = 50; //resolução do loop

    while (1) {
		//Incrementa o tempo de cada LED
		led0_timer += step_ms;
		led1_timer += step_ms;

		if(led0_timer >= LED0_SLEEP_TIME_MS) {
        	// Toggle do LED usando a nova API
        	gpio_pin_toggle_dt(&led0);
        	led0_timer = 0;
		}

		if(led1_timer >= LED1_SLEEP_TIME_MS) {
        	// Toggle do LED usando a nova API
        	gpio_pin_toggle_dt(&led1);
        	led1_timer = 0;
		}
		k_msleep(step_ms);
	}
}