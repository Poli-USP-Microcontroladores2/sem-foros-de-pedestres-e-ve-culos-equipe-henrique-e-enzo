#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(semaforo_veiculos, LOG_LEVEL_INF);

/* semáforos que ativam cada thread (uma thread por led) */
K_SEM_DEFINE(sem_verde, 1, 1);    /* inicia em 1 para começar com GREEN se desejar */
K_SEM_DEFINE(sem_amarelo, 0, 1);
K_SEM_DEFINE(sem_vermelho, 0, 1);

/* mutex para proteger o estado / decisões de transição */
K_MUTEX_DEFINE(transition_mutex);

/* flags vindas da thread processadora do pulso */
volatile bool ped_request = false;      /* pulso longo -> travessia */
volatile int64_t ped_request_ts = 0;    /* timestamp quando ped_request ocorreu */
volatile int64_t sync_timestamp_ms = 0; /* pulso curto -> marca quando ocorreu */

/* proteção das flags */
K_MUTEX_DEFINE(flags_mutex);

/* DT LED aliases (ajuste se sua placa usar outros aliases) */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#else
#error "led0 alias missing"
#endif
#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#else
#error "led1 alias missing"
#endif
#if DT_NODE_HAS_STATUS(LED2_NODE, okay)
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
#else
#error "led2 alias missing"
#endif

/* Sync input PTA5 */
#define PORTA_NODE DT_NODELABEL(gpioa)
static const struct gpio_dt_spec sync_input = {
    .port = DEVICE_DT_GET(PORTA_NODE),
    .pin = 5,
};
static struct gpio_callback sync_cb_data;

K_SEM_DEFINE(sem_sync_isr, 0, 1);

void sync_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_sem_give(&sem_sync_isr);
}

void thread_processa_sync(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    static int64_t last_pulse = 0;

    while (1) {
        k_sem_take(&sem_sync_isr, K_FOREVER);
        int64_t now = k_uptime_get();

        if (now - last_pulse < 200) continue;

        k_msleep(5);

        if (gpio_pin_get_dt(&sync_input) != 1) {
            last_pulse = now;
            continue;
        }

        int64_t start = k_uptime_get();
        while (gpio_pin_get_dt(&sync_input) == 1 && (k_uptime_get() - start) < 2000) {
            k_msleep(10);
        }
        int dur = (int)(k_uptime_get() - start);

        if (dur < 50) {
            LOG_INF("Pulso ignorado (ruído) %d ms", dur);
        } else if (dur >= 300) {
            k_mutex_lock(&flags_mutex, K_FOREVER);
            ped_request = true;
            ped_request_ts = k_uptime_get();
            k_mutex_unlock(&flags_mutex);
            LOG_INF("Pulso LONGO detectado (%d ms) -> ped_request=TRUE ts=%lld", dur, ped_request_ts);
        } else {
            k_mutex_lock(&flags_mutex, K_FOREVER);
            sync_timestamp_ms = k_uptime_get();
            k_mutex_unlock(&flags_mutex);
            LOG_INF("Pulso CURTO detectado (%d ms) -> sync_timestamp_ms=%lld", dur, sync_timestamp_ms);
        }

        last_pulse = k_uptime_get();
    }
}

static void set_leds_all(bool g, bool y, bool r)
{
    gpio_pin_set_dt(&led0, g ? 1 : 0);
    gpio_pin_set_dt(&led1, y ? 1 : 0);
    gpio_pin_set_dt(&led2, r ? 1 : 0);
}

/* THREAD: GREEN */
void thread_led_verde(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const int GREEN_MS = 3000;

    while (1) {
        k_sem_take(&sem_verde, K_FOREVER);
        gpio_pin_set_dt(&led0, 1);
        LOG_INF("GREEN ON");

        int elapsed = 0;

        /* registra start para decidir se ped_request que veio antes do green deve ou não interromper */
        int64_t green_start = k_uptime_get();

        while (elapsed < GREEN_MS) {
            k_mutex_lock(&flags_mutex, K_FOREVER);
            bool pr = ped_request;
            int64_t pr_ts = ped_request_ts;
            k_mutex_unlock(&flags_mutex);

            /* só interrompe se o pedido veio DEPOIS do início do GREEN */
            if (pr && pr_ts >= green_start) {
                LOG_INF("GREEN interrupted by ped_request (ts %lld >= green_start %lld)", pr_ts, green_start);
                break;
            }
            k_msleep(100);
            elapsed += 100;
        }

        gpio_pin_set_dt(&led0, 0);
        LOG_INF("GREEN OFF");

        k_mutex_lock(&transition_mutex, K_FOREVER);
        k_sem_give(&sem_amarelo);
        k_mutex_unlock(&transition_mutex);
    }
}

/* THREAD: YELLOW */
void thread_led_amarelo(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const int YELLOW_MS = 1000;

    while (1) {
        k_sem_take(&sem_amarelo, K_FOREVER);

        gpio_pin_set_dt(&led2, 1);
        gpio_pin_set_dt(&led0, 1);
        LOG_INF("YELLOW ON");
        k_msleep(YELLOW_MS);
        gpio_pin_set_dt(&led2, 0);
        gpio_pin_set_dt(&led0, 0);
        LOG_INF("YELLOW OFF");

        k_mutex_lock(&transition_mutex, K_FOREVER);
        k_sem_give(&sem_vermelho);
        k_mutex_unlock(&transition_mutex);
    }
}

/* THREAD: RED */
void thread_led_vermelho(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const int RED_MS = 4000;

    while (1) {
        k_sem_take(&sem_vermelho, K_FOREVER);

        gpio_pin_set_dt(&led2, 1);
        LOG_INF("RED ON");

        int elapsed = 0;
        int64_t red_start = k_uptime_get();
        bool detected_ped_in_red = false;

        while (elapsed < RED_MS) {
            k_mutex_lock(&flags_mutex, K_FOREVER);
            bool ped = ped_request;
            int64_t st = sync_timestamp_ms;
            k_mutex_unlock(&flags_mutex);

            if (ped) {
                /* marca que houve pedido durante vermelho — não limpia aqui */
                detected_ped_in_red = true;
            } else if (st != 0 && st >= red_start) {
                /* curto recebido DURANTE este vermelho -> encurta */
                k_mutex_lock(&flags_mutex, K_FOREVER);
                sync_timestamp_ms = 0;
                k_mutex_unlock(&flags_mutex);

                LOG_INF("RED shortened by sync at %lld (red start %lld)", st, red_start);
                break;
            }
            k_msleep(100);
            elapsed += 100;
        }

        /* se houve ped_request DURANTE o vermelho, espere agora explicitamente pelo pulso curto
           (que é enviado pelo semáforo de pedestres ao término da travessia). */
        if (detected_ped_in_red) {
            LOG_INF("ped_request detected during RED -> waiting for end-sync before releasing GREEN");
            /* Espera pelo sync curto (com timeout razoável) */
            int waited = 0;
            bool got_end_sync = false;
            while (waited < 8000) { /* timeout 8s pra segurança */
                k_mutex_lock(&flags_mutex, K_FOREVER);
                int64_t st = sync_timestamp_ms;
                k_mutex_unlock(&flags_mutex);
                if (st != 0 && st >= red_start) {
                    /* consome sync */
                    k_mutex_lock(&flags_mutex, K_FOREVER);
                    sync_timestamp_ms = 0;
                    ped_request = false;
                    ped_request_ts = 0;
                    k_mutex_unlock(&flags_mutex);
                    got_end_sync = true;
                    LOG_INF("End-sync received at %lld, ped_request cleared", st);
                    break;
                }
                k_msleep(100);
                waited += 100;
            }
            if (!got_end_sync) {
                /* timeout: limpa ped_request para não travar o ciclo */
                k_mutex_lock(&flags_mutex, K_FOREVER);
                ped_request = false;
                ped_request_ts = 0;
                sync_timestamp_ms = 0;
                k_mutex_unlock(&flags_mutex);
                LOG_WRN("Timeout waiting end-sync; forced clear ped_request");
            }
        } else {
            /* se não houve pedido, podemos também checar se curto (sincronização) encurtou anteriormente */
            /* nada extra aqui */
        }

        /* apaga vermelho */
        gpio_pin_set_dt(&led2, 0);
        LOG_INF("RED OFF");

        /* libera GREEN */
        k_mutex_lock(&transition_mutex, K_FOREVER);
        k_sem_give(&sem_verde);
        k_mutex_unlock(&transition_mutex);

        LOG_INF("RED signaled GREEN");
    }
}

K_THREAD_DEFINE(t_processa_sync, 512, thread_processa_sync, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(t_led_verde, 512, thread_led_verde, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(t_led_amarelo, 512, thread_led_amarelo, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(t_led_vermelho, 512, thread_led_vermelho, NULL, NULL, NULL, 6, 0, 0);

void main(void)
{
    int rc;
    LOG_INF("Iniciando semáforo veículos (1 thread por LED, transições protegidas)");

    if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2)) {
        LOG_ERR("LEDs não prontos");
        return;
    }

    rc = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (rc) { LOG_ERR("led0 cfg %d", rc); return; }
    rc = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (rc) { LOG_ERR("led1 cfg %d", rc); return; }
    rc = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    if (rc) { LOG_ERR("led2 cfg %d", rc); return; }

    if (!device_is_ready(sync_input.port)) {
        LOG_ERR("Sync input port not ready");
        return;
    }

    rc = gpio_pin_configure_dt(&sync_input, GPIO_INPUT | GPIO_PULL_DOWN);
    if (rc) { LOG_ERR("sync_input cfg %d", rc); return; }

    rc = gpio_pin_interrupt_configure_dt(&sync_input, GPIO_INT_EDGE_RISING);
    if (rc) { LOG_ERR("sync_input irq %d", rc); return; }

    gpio_init_callback(&sync_cb_data, sync_isr, BIT(sync_input.pin));
    gpio_add_callback(sync_input.port, &sync_cb_data);

    LOG_INF("Setup ok. PTA5 configurado como entrada. Ciclo iniciado.");

    while (1) {
        k_msleep(10000);
    }
}