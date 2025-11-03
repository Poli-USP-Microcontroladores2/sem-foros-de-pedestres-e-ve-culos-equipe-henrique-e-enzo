#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Botao na porta PTA16
#define PORTA_NODE DT_NODELABEL(gpioa)
static const struct gpio_dt_spec button = {
    .port = DEVICE_DT_GET(PORTA_NODE),
    .pin = 16,
    .dt_flags = GPIO_ACTIVE_LOW,
};
static struct gpio_callback button_cb_data;

// Mutex para controle de LEDs e semaforo para o botao de travessia
struct k_mutex leds_mutex;
struct k_sem   ped_request_sem;
struct k_sem   cycle_sem;

// Threads
k_tid_t tid_red, tid_green;
K_THREAD_STACK_DEFINE(red_stack, 1024);
K_THREAD_STACK_DEFINE(green_stack, 1024);
static struct k_thread red_thread;
static struct k_thread green_thread;

// LEDs
#define TEMPO_DO_LED_VERDE_MS    		4000
#define TEMPO_DO_LED_VERMELHO_MS 		2000
#define TEMPO_DO_PISCA_LED_VERMELHO_MS  1000

#define LED_RED_NODE DT_NODELABEL(red_led)
#define LED_GREEN_NODE DT_NODELABEL(green_led)

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

// Estado do semáforo
typedef enum {
    VERMELHO,
    VERDE
} estado_semaforo_t;

volatile estado_semaforo_t estado_atual = VERMELHO;
volatile bool pedestre_esperando = false;

// ISR - botão
void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    static int64_t last_press_time = 0;
    int64_t now = k_uptime_get();

    if (now - last_press_time < 200) return;
    last_press_time = now;
    
    printk("Botão pressionado\n");
    if (estado_atual == VERMELHO) {
        pedestre_esperando = true;
        k_sem_give(&ped_request_sem);
    }
}

// Função para garantir que apenas um LED está aceso
void set_leds(bool red_on, bool green_on) {
    k_mutex_lock(&leds_mutex, K_FOREVER);
    gpio_pin_set_dt(&led_red, red_on ? 1 : 0);
    gpio_pin_set_dt(&led_green, green_on ? 1 : 0);
    k_mutex_unlock(&leds_mutex);
}

// THREAD - LED verde
void fn_thread_led_verde(void *p1, void *p2, void *p3) {
    while(1) {
        // Espera pelo sinal para iniciar ciclo verde
        k_sem_take(&cycle_sem, K_FOREVER);
        
        estado_atual = VERDE;
        printk(">>> VERDE LIGADO - 4 segundos\n");
        set_leds(false, true);  // Vermelho OFF, Verde ON
        
        // Tempo fixo de verde
        k_msleep(TEMPO_DO_LED_VERDE_MS);
        
        printk(">>> VERDE DESLIGADO\n");
        set_leds(false, false); // Ambos OFF
        
        estado_atual = VERMELHO;
        
        // Sinaliza que terminou o verde
        k_sem_give(&cycle_sem);
    }
}

// THREAD - LED vermelho
void fn_thread_led_vermelho(void *p1, void *p2, void *p3) {
    while(1) {
        // Estado vermelho
        estado_atual = VERMELHO;
        pedestre_esperando = false;
        
        printk(">>> VERMELHO LIGADO\n");
        set_leds(true, false);  // Vermelho ON, Verde OFF
        
        int espera = 0;
        bool travessia_antecipada = false;
        
        // Espera 2 segundos OU pedido antecipado
        while (espera < TEMPO_DO_LED_VERMELHO_MS) {
            // Verifica se há pedido de travessia a cada 100ms
            if (pedestre_esperando) {
                printk("Transição antecipada para verde\n");
                travessia_antecipada = true;
                break;
            }
            k_msleep(100);
            espera += 100;
        }
        
        if (!travessia_antecipada) {
            printk("Tempo vermelho completo - transição normal\n");
        }
        
        // Desliga vermelho
        set_leds(false, false);
        k_msleep(200); // Pequena pausa entre transições
        
        // Inicia ciclo verde
        k_sem_give(&cycle_sem);
        
        // Espera ciclo verde terminar
        k_sem_take(&cycle_sem, K_FOREVER);
        
        // Pequena pausa antes de recomeçar
        k_msleep(300);
    }
}

// Modo noturno - pisca vermelho
void fn_thread_led_noturno(void *p1, void *p2, void *p3) {
    while(1) {
        set_leds(true, false);
        k_msleep(TEMPO_DO_PISCA_LED_VERMELHO_MS);
        set_leds(false, false);
        k_msleep(TEMPO_DO_PISCA_LED_VERMELHO_MS);
    }
}

void main(void) {
    int noturno = 0; // Mude para 1 para modo noturno

    printk("Iniciando semáforo com ciclo automático...\n");

    // Verifica devices
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        printk("Erro: Dispositivos LED não estão prontos\n");
        return;
    }
    
    if (!device_is_ready(button.port)) {
        printk("Erro: GPIOA não está pronto\n");
        return;
    }

    // Configura botão
    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
        printk("Erro configurando botão: %d\n", ret);
        return;
    }
    
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Erro configurando interrupção: %d\n", ret);
        return;
    }
    
    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    // Configura LEDs
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        printk("Erro configurando LED vermelho: %d\n", ret);
        return;
    }
    
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        printk("Erro configurando LED verde: %d\n", ret);
        return;
    }

    // Inicializa sincronizadores
    k_mutex_init(&leds_mutex);
    k_sem_init(&ped_request_sem, 0, 10); 
    k_sem_init(&cycle_sem, 0, 1);  		 // Binário para controle de ciclo

    // Garante LEDs inicialmente desligados
    set_leds(false, false);

    if (noturno == 1) {
        printk("Modo noturno ativado\n");
        tid_red = k_thread_create(&red_thread, red_stack, 
            K_THREAD_STACK_SIZEOF(red_stack), fn_thread_led_noturno, 
            NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    } else {
        printk("Modo normal ativado - Ciclo: 2s Vermelho -> 4s Verde\n");
        
        // Cria threads (vermelho inicia primeiro)
        tid_red = k_thread_create(&red_thread, red_stack, 
            K_THREAD_STACK_SIZEOF(red_stack), fn_thread_led_vermelho, 
            NULL, NULL, NULL, 5, 0, K_NO_WAIT);
            
        k_msleep(100); // Pequeno delay para garantir ordem
        
        tid_green = k_thread_create(&green_thread, green_stack, 
            K_THREAD_STACK_SIZEOF(green_stack), fn_thread_led_verde, 
            NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    }

    printk("Sistema iniciado\n");

    // Loop principal
    while (1) {
        k_msleep(1000);
    }
}