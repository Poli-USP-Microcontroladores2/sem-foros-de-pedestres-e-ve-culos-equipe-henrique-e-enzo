#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

//Mutex para controle dos LEDs
struct k_mutex semaforo_mutex;

k_tid_t tid_red, tid_green;

//Configurações e definições dos LEDs
#define LED_GREEN_NODE_SLEEP_TIME_MS 4000
#define LED_RED_NODE_SLEEP_TIME_MS 2000

#define LED_RED_NODE DT_NODELABEL(red_led)
#define LED_GREEN_NODE DT_NODELABEL(green_led)

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

void fn_thread_led_verde(){
	while(1){

		//Inicio da região crítica protegida
		k_mutex_lock(&semaforo_mutex, K_FOREVER);

		//k_uptime_get() para testar o tempo do LED
		int64_t inicio = k_uptime_get();

		// LED verde
		gpio_pin_set_dt(&led_green, 1);
        k_msleep(LED_GREEN_NODE_SLEEP_TIME_MS);
		gpio_pin_set_dt(&led_green, 0);

		int64_t fim = k_uptime_get();


		k_mutex_unlock(&semaforo_mutex);


		printk("LED verde - %lld ms\n", fim - inicio);
	}
}

void fn_thread_led_vermelho(){
	while(1){

		//Inicio da região crítica protegida
		k_mutex_lock(&semaforo_mutex, K_FOREVER);


		//k_uptime_get() para testar o tempo do LED
		int64_t inicio = k_uptime_get();

		// LED vermelho
		gpio_pin_set_dt(&led_red, 1);
        k_msleep(LED_RED_NODE_SLEEP_TIME_MS);
		gpio_pin_set_dt(&led_red, 0);
		
		int64_t fim = k_uptime_get();

		k_mutex_unlock(&semaforo_mutex);


		printk("LED vermelho - %lld ms\n", fim - inicio);
	}
}

//Definições das threads dos LEDs
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(red_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(green_stack, STACK_SIZE);

static struct k_thread red_thread;
static struct k_thread green_thread;

void main(void) {

    // Verifica se os devices estão prontos
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        printk("Error: LED devices is not ready\n");
        return;
    }

    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);

	k_mutex_init(&semaforo_mutex);
	tid_green = k_thread_create(&green_thread, green_stack, K_THREAD_STACK_SIZEOF(green_stack),fn_thread_led_verde, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	tid_red = k_thread_create(&red_thread, red_stack, K_THREAD_STACK_SIZEOF(red_stack),fn_thread_led_vermelho, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	k_thread_join(tid_green, K_FOREVER);
	k_thread_join(tid_red, K_FOREVER);
}