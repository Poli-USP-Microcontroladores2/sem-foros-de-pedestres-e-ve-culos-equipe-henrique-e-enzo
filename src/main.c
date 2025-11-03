#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

K_SEM_DEFINE(sem_verde, 1, 1);   
K_SEM_DEFINE(sem_amarelo, 0, 1);  
K_SEM_DEFINE(sem_vermelho, 0, 1);

K_MUTEX_DEFINE(mutex_leds);

K_SEM_DEFINE(sem_modo_noturno, 0, 1);
bool modo_noturno = false;

// Define o LED usando Device Tree
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

// Verifica se o LED está definido no Device Tree
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#else
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#else
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED2_NODE, okay)
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
#else
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

void thread_led_verde(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_verde, K_FOREVER);

        k_mutex_lock(&mutex_leds, K_FOREVER);

        gpio_pin_set_dt(&led2, 0);
        gpio_pin_set_dt(&led0, 1);

        k_mutex_unlock(&mutex_leds);

        k_msleep(3000);
        
        k_sem_give(&sem_amarelo);
    }
}

void thread_led_amarelo(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_amarelo, K_FOREVER);
        
        k_mutex_lock(&mutex_leds, K_FOREVER);

        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led2, 1);

        k_mutex_unlock(&mutex_leds);

        k_msleep(1000);

        k_sem_give(&sem_vermelho);
    }
}

void thread_led_vermelho(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_vermelho, K_FOREVER);

        k_mutex_lock(&mutex_leds, K_FOREVER);
        
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led2, 1);

        k_mutex_unlock(&mutex_leds);

        k_msleep(4000);

        k_sem_give(&sem_verde);
    }
}

void thread_modo_noturno(void *arg1, void *arg2, void *arg3) {
    k_sem_take(&sem_modo_noturno, K_FOREVER);
    
    k_mutex_lock(&mutex_leds, K_FOREVER);
     
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led2, 0);

    k_mutex_unlock(&mutex_leds);

    while (modo_noturno) {
        k_mutex_lock(&mutex_leds, K_FOREVER);
        
        gpio_pin_toggle_dt(&led0);
        gpio_pin_toggle_dt(&led2);

        k_mutex_unlock(&mutex_leds);
        k_msleep(1000);
    }
}

K_THREAD_DEFINE(t_led_verde, 512, thread_led_verde,
                NULL, NULL, NULL,
                7, 0, 0);
K_THREAD_DEFINE(t_led_amarelo, 512, thread_led_amarelo,
                NULL, NULL, NULL,
                7, 0, 0);
K_THREAD_DEFINE(t_led_vermelho, 512, thread_led_vermelho,
                NULL, NULL, NULL,
                7, 0, 0);
K_THREAD_DEFINE(t_modo_noturno, 512, thread_modo_noturno,
                NULL, NULL, NULL,
                7, 0, 0);

void main(void)
{
    int ret0, ret1, ret2; 

    // Verifica se o device está pronto
    if (!gpio_is_ready_dt(&led0)) {
        printk("Error: LED device %s is not ready\n", led0.port->name);
        return;
    }
    if (!gpio_is_ready_dt(&led1)) {
        printk("Error: LED device %s is not ready\n", led1.port->name);
        return;
    }
    if (!gpio_is_ready_dt(&led2)) {
        printk("Error: LED device %s is not ready\n", led2.port->name);
        return;
    }

    // Configura o pino como saída
    ret0 = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret0 < 0) {
        printk("Error %d: failed to configure LED pin\n", ret0);
        return;
    }
    ret1 = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (ret1 < 0) {
        printk("Error %d: failed to configure LED pin\n", ret1);
        return;
    }
    ret2 = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    if (ret2 < 0) {
        printk("Error %d: failed to configure LED pin\n", ret2);
        return;
    }


    while (1) {
        k_sleep(K_FOREVER);
    }
}