#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestres, LOG_LEVEL_INF);

// Botão na porta PTA16
#define PORTA_NODE DT_NODELABEL(gpioa)
static const struct gpio_dt_spec button = {
    .port = DEVICE_DT_GET(PORTA_NODE),
    .pin = 16,
    .dt_flags = GPIO_ACTIVE_LOW,
};
static struct gpio_callback button_cb_data;

// GPIO para sinal de sincronização - PTB1
#define PORTB_NODE DT_NODELABEL(gpiob)
static const struct gpio_dt_spec sync_signal = {
    .port = DEVICE_DT_GET(PORTB_NODE),
    .pin = 1,
};

// Mutex e semáforos
struct k_mutex leds_mutex;
struct k_sem ped_request_sem;
struct k_sem cycle_sem;

// Threads
k_tid_t tid_red, tid_green, tid_noturno;
K_THREAD_STACK_DEFINE(red_stack, 1024);
K_THREAD_STACK_DEFINE(green_stack, 1024);
K_THREAD_STACK_DEFINE(noturno_stack, 1024);
static struct k_thread red_thread;
static struct k_thread green_thread;
static struct k_thread noturno_thread;

// LEDs
#define TEMPO_DO_LED_VERDE_MS            4000
#define TEMPO_DO_LED_VERMELHO_MS         4000
#define TEMPO_DO_PISCA_LED_VERMELHO_MS   1000

#define LED_RED_NODE DT_NODELABEL(red_led)
#define LED_GREEN_NODE DT_NODELABEL(green_led)

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

// Estado
typedef enum {
    VERMELHO,
    VERDE
} estado_semaforo_t;

volatile estado_semaforo_t estado_atual = VERMELHO;
volatile bool pedestre_esperando = false;
volatile bool modo_noturno_ativo = false;

/* Timestamp quando ped_request foi gerado (usado para prioridade/ordem) */
volatile int64_t ped_request_ts = 0;

// --- Funções de sincronização ---
void enviar_sinal_sincronizacao(void) {
    if (modo_noturno_ativo) return;
    gpio_pin_set_dt(&sync_signal, 1);
    k_msleep(100);   // pulso curto = sincronização
    gpio_pin_set_dt(&sync_signal, 0);
    LOG_INF(">> Sinal de sincronizacao enviado\n");
}

void enviar_sinal_travessia(void) {
    if (modo_noturno_ativo) return;
    gpio_pin_set_dt(&sync_signal, 1);
    k_msleep(500);   // pulso longo = travessia
    gpio_pin_set_dt(&sync_signal, 0);
    LOG_INF(">> Sinal de travessia enviado\n");
}

// --- ISR DO BOTÃO ---
void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    static int64_t last_press_time = 0;
    int64_t now = k_uptime_get();

    if (now - last_press_time < 200) return; // debounce
    last_press_time = now;

    if (modo_noturno_ativo) return;

    pedestre_esperando = true;
    /* marca timestamp local — útil se quiser depurar ordem no outro equipamento */
    ped_request_ts = k_uptime_get();
    k_sem_give(&ped_request_sem);  // acorda thread vermelha
}

// --- Controle dos LEDs ---
void set_leds(bool red_on, bool green_on) {
    k_mutex_lock(&leds_mutex, K_FOREVER);
    gpio_pin_set_dt(&led_red, red_on ? 1 : 0);
    gpio_pin_set_dt(&led_green, green_on ? 1 : 0);
    k_mutex_unlock(&leds_mutex);
}

// --- THREAD: LED VERDE ---
void fn_thread_led_verde(void *p1, void *p2, void *p3) {
    while (1) {
        if (modo_noturno_ativo) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&cycle_sem, K_FOREVER);  // espera ciclo verde
        estado_atual = VERDE;
        LOG_INF(">>> VERDE LIGADO\n");
        set_leds(false, true);

        /* registra início do verde para comparar com ped_request_ts do botão */
        int64_t green_start = k_uptime_get();

        int waited = 0;
        while (waited < TEMPO_DO_LED_VERDE_MS) {
            /* se pedestre pressionou DURANTE este verde (timestamp posterior ao green start) -> interrompe */
            if (pedestre_esperando && (ped_request_ts >= green_start)) {
                LOG_INF(">>> VERDE interrompido por pedido de travessia (durante verde)\n");
                break;
            }
            k_msleep(100);
            waited += 100;
        }

        LOG_INF(">>> VERDE DESLIGADO\n");
        set_leds(false, false);
        estado_atual = VERMELHO;

        k_sem_give(&cycle_sem); // libera vermelho
    }
}

// --- THREAD: LED VERMELHO ---
void fn_thread_led_vermelho(void *p1, void *p2, void *p3) {
    while (1) {
        if (modo_noturno_ativo) {
            k_msleep(100);
            continue;
        }

        estado_atual = VERMELHO;

        LOG_INF(">>> VERMELHO LIGADO\n");
        set_leds(true, false);

        int espera = 0;
        bool pedido_detectado_durante_vermelho = false;

        /* Monitora se pedestre pediu durante o vermelho */
        while (espera < TEMPO_DO_LED_VERMELHO_MS) {
            if (modo_noturno_ativo) break;
            if (pedestre_esperando) {
                /* marca que o pedido aconteceu DURANTE este vermelho */
                pedido_detectado_durante_vermelho = true;
                /* NÃO zera pedestre_esperando aqui — quem consome será o final deste fluxo */
                break;
            }
            k_msleep(100);
            espera += 100;
        }

        if (pedido_detectado_durante_vermelho) {
            LOG_INF(">>> Pedido de travessia recebido durante VERMELHO - enviando travessia (se necessário)\n");
            /* envia pulso de travessia (caso pedestre local precise avisar) */
            enviar_sinal_travessia();
            /* aguarda o tempo de travessia do pedestre terminar; o pedestre envia um pulso curto ao final
               (o semáforo de pedestres já faz isso). Aqui, aguardamos o curto (sincronização) na outra placa. */
            /* Para garantir interoperabilidade, apenas aguardamos um pequeno intervalo extra (a outra ponta enviará o curto). */
            k_msleep(1000); // mantém vermelho enquanto travessia acontece
        }

        LOG_INF(">>> VERMELHO DESLIGADO\n");
        set_leds(false, false);

        /* envia sinal curto de sincronização para a placa veicular indicando fim do ciclo pedestre */
        enviar_sinal_sincronizacao();

        /* agora libera ciclo verde (a placa veicular decidirá se avança ou não, é responsável por prioridade) */
        k_sem_give(&cycle_sem);       // ativa ciclo verde
        k_sem_take(&cycle_sem, K_FOREVER);
    }
}

// --- THREAD: MODO NOTURNO (pisca vermelho) ---
void fn_thread_led_noturno(void *p1, void *p2, void *p3) {
    while (1) {
        if (!modo_noturno_ativo) {
            k_msleep(100);
            continue;
        }
        set_leds(true, false);
        LOG_INF("LED VERMELHO LIGADO");
        k_msleep(TEMPO_DO_PISCA_LED_VERMELHO_MS);
        set_leds(false, false);
        LOG_INF("LED VERMELHO DESLIGADO");
        k_msleep(TEMPO_DO_PISCA_LED_VERMELHO_MS);
    }
}

// --- Alternar modo noturno ---
void toggle_modo_noturno(void) {
    modo_noturno_ativo = !modo_noturno_ativo;

    if (modo_noturno_ativo) {
        LOG_INF("=== MODO NOTURNO ATIVADO ===\n");
        set_leds(false, false);
        if (tid_noturno == NULL) {
            tid_noturno = k_thread_create(&noturno_thread, noturno_stack,
                K_THREAD_STACK_SIZEOF(noturno_stack), fn_thread_led_noturno,
                NULL, NULL, NULL, 5, 0, K_NO_WAIT);
        }
    } else {
        LOG_INF("=== MODO NORMAL ATIVADO ===\n");
        set_leds(false, false);
    }
}

// --- MAIN ---
void main(void) {
    bool modo_noturno = false;   //modo noturno hard coded.
    LOG_INF("Iniciando semáforo de pedestres...\n");

    // Valida hardware
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_INF("Erro: LEDs não estão prontos\n");
        return;
    }
    if (!device_is_ready(button.port)) {
        LOG_INF("Erro: GPIOA não está pronto\n");
        return;
    }
    if (!device_is_ready(sync_signal.port)) {
        LOG_INF("Erro: GPIOB não está pronto\n");
        return;
    }

    // Configura GPIOs
    gpio_pin_configure_dt(&sync_signal, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);

    // Botão com interrupção
    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    // Inicializa sincronizadores
    k_mutex_init(&leds_mutex);
    k_sem_init(&ped_request_sem, 0, 10);
    k_sem_init(&cycle_sem, 0, 1);

    set_leds(false, false);

    if (modo_noturno) {
        toggle_modo_noturno();
    } else {
        LOG_INF("Modo normal ativado - ciclo 4s verde / 4s vermelho\n");

        tid_red = k_thread_create(&red_thread, red_stack,
            K_THREAD_STACK_SIZEOF(red_stack), fn_thread_led_vermelho,
            NULL, NULL, NULL, 5, 0, K_NO_WAIT);

        k_msleep(100);

        tid_green = k_thread_create(&green_thread, green_stack,
            K_THREAD_STACK_SIZEOF(green_stack), fn_thread_led_verde,
            NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    }

    LOG_INF("Sistema iniciado - PTB1 como saída de sincronismo\n");

    enviar_sinal_travessia(); //POG para bootar o semaforo
    k_msleep(1000);

    while (1) {
        k_msleep(10000);
    }
}
