#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_veiculos, LOG_LEVEL_INF);

K_SEM_DEFINE(sem_verde, 1, 1);   
K_SEM_DEFINE(sem_amarelo, 0, 1);  
K_SEM_DEFINE(sem_vermelho, 0, 1);

K_MUTEX_DEFINE(mutex_leds);

K_SEM_DEFINE(sem_modo_noturno, 0, 1);
bool modo_noturno = false;

// --- NOVO: Semáforo para a ISR acordar a thread de processamento ---
K_SEM_DEFINE(sem_sync_isr, 0, 1);
volatile bool recebeu_sincronizacao = false;
volatile bool recebeu_travessia = false;

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

// GPIO para receber sinal de sincronização - PTA5
#define PORTA_NODE DT_NODELABEL(gpioa)
static const struct gpio_dt_spec sync_input = {
    .port = DEVICE_DT_GET(PORTA_NODE),
    .pin = 5,
};
static struct gpio_callback sync_cb_data;

// Esta função é chamada pela interrupção. Rápida, sem 'sleeps', sem 'prints'.
void sync_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    // Apenas acorda a thread de processamento. NADA MAIS.
    k_sem_give(&sem_sync_isr);
}

void thread_led_verde(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_verde, K_FOREVER);
        LOG_INF("Pegou sem_verde");

        // Verifica se recebeu sinal de travessia (da ISR)
        if (recebeu_travessia) {
            recebeu_travessia = false; // "Consome" a flag
            LOG_INF("Verde: Travessia pedida, pulando para amarelo");
            k_sem_give(&sem_amarelo);
            continue; // Pula o resto do estado verde
        }

        k_mutex_lock(&mutex_leds, K_FOREVER);

        gpio_pin_set_dt(&led2, 0);
        gpio_pin_set_dt(&led0, 1);
        LOG_INF("Ligou Led Verde");

        k_mutex_unlock(&mutex_leds);

        k_msleep(3000);
        
        k_sem_give(&sem_amarelo);
        LOG_INF("Deu sem_amarelo");
    }
}

void thread_led_amarelo(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_amarelo, K_FOREVER);
        LOG_INF("Pegou sem_amarelo");
        
        k_mutex_lock(&mutex_leds, K_FOREVER);

        gpio_pin_set_dt(&led0, 1);
        gpio_pin_set_dt(&led2, 1);
        LOG_INF("Ligou Led Amarelo");

        k_mutex_unlock(&mutex_leds);

        k_msleep(1000);

        k_sem_give(&sem_vermelho);
        LOG_INF("Deu sem_vermelho");
    }
}

void thread_led_vermelho(void *arg1, void *arg2, void *arg3) {
    while (!modo_noturno) {
        k_sem_take(&sem_vermelho, K_FOREVER);
        LOG_INF("Pegou sem_vermelho");

        k_mutex_lock(&mutex_leds, K_FOREVER);
        
        gpio_pin_set_dt(&led0, 0);
        gpio_pin_set_dt(&led2, 1);
        LOG_INF("Ligou Led Vermelho");

        k_mutex_unlock(&mutex_leds);

        // Lógica de espera: 4s OU até receber sinal de sync do pedestre
        int espera = 0;
        while (espera < 4000 && !recebeu_sincronizacao) {
            if (modo_noturno) break;
            k_msleep(100);
            espera += 100;
        }
        
        if (recebeu_sincronizacao) {
            recebeu_sincronizacao = false; // "Consome" a flag
            LOG_INF("Vermelho: Sincronizado com pedestre! (esperou %d ms)", espera);
        } else {
            LOG_INF("Vermelho: Timeout de 4s atingido (sem sync)");
        }
        
        // Se pedestre pediu travessia (pulso longo) DURANTE o vermelho, 
        // ele será tratado no próximo ciclo verde.
        recebeu_travessia = false; // Limpa a flag por segurança

        k_sem_give(&sem_verde);
        LOG_INF("Deu sem_verde");
    }
}

void thread_modo_noturno(void *arg1, void *arg2, void *arg3) {
    k_sem_take(&sem_modo_noturno, K_FOREVER);
    LOG_INF("Pegou sem_modo_noturno");
    
    k_mutex_lock(&mutex_leds, K_FOREVER);
     
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led2, 0);
    LOG_INF("Desligou Leds ativos");

    k_mutex_unlock(&mutex_leds);

    while (modo_noturno) {
        k_mutex_lock(&mutex_leds, K_FOREVER);
        
        gpio_pin_toggle_dt(&led0);
        gpio_pin_toggle_dt(&led2);
        LOG_INF("Ligou/Desligou Led Amarelo");

        k_mutex_unlock(&mutex_leds);
        k_msleep(1000);
    }
}

// --- NOVO: Thread "Processadora" da ISR ---
// Esta thread faz o trabalho pesado que a ISR não pode fazer.
void thread_processa_sync(void *arg1, void *arg2, void *arg3) {
    static int64_t last_sync_time = 0; // Para debounce

    while (1) {
        // 1. Dorme até a ISR (sync_isr) acordá-la (na borda de SUBIDA)
        k_sem_take(&sem_sync_isr, K_FOREVER);

        if (modo_noturno) {
            continue; // Ignora sinais se estiver em modo noturno
        }

        // 2. A thread (NÃO a ISR) faz o trabalho
        int64_t now = k_uptime_get();

        // 3. Debounce: Ignora gatilhos por 200ms após o último gatilho
        //    (Evita ruído elétrico na borda de subida)
        if (now - last_sync_time < 200) {
            LOG_WRN("ISR_Handler: Debounce (200ms), ignorando pulso.");
            continue;
        }
        last_sync_time = now; // Registra o início deste pulso

        // O pino ACABOU de subir (estado 1).
        
        // 4. Esperamos 250ms. Este é o tempo "intermediário".
        //    (100ms < 250ms < 500ms)
        
        k_msleep(250); // <--- A lógica de decisão foi movida para cá.

        int state = gpio_pin_get_dt(&sync_input);

        if (state == 1) {
            // Se ainda está ALTO depois de 250ms = Pulso Longo (500ms)
            recebeu_travessia = true;
            LOG_INF("ISR_Handler: Sinal de TRAVESSIA (longo) recebido");

            // Bônus: Esperar o pulso terminar para limpar a linha
            // e atualizar o 'last_sync_time' para o *fim* do pulso.
            int64_t start_wait = k_uptime_get();
            while (gpio_pin_get_dt(&sync_input) == 1 && (k_uptime_get() - start_wait < 1000)) {
                k_msleep(20);
            }
            // Atualiza o debounce para o FIM do pulso longo
            last_sync_time = k_uptime_get(); 

        } else {
            // Se já está BAIXO depois de 250ms = Pulso Curto (100ms)
            recebeu_sincronizacao = true;
            LOG_INF("ISR_Handler: Sinal de SINCRONIZAÇÃO (curto) recebido");
            // O 'last_sync_time' (do início) já garante o debounce
        }
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
// --- NOVO: Definição da thread processadora da ISR ---
// Prioridade 5 (mais alta que os LEDs) para processar o sinal rápido
K_THREAD_DEFINE(t_processa_sync, 512, thread_processa_sync,
                NULL, NULL, NULL,
                5, 0, 0);

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

    // --- NOVO: Configuração completa do pino de entrada e ISR ---
    LOG_INF("Configurando pino de entrada sync (PTA5)...");
    if (!device_is_ready(sync_input.port)) {
        printk("Error: GPIOA (sync_input) não está pronto\n");
        return;
    }
    
    // Configura pino como Entrada com PULL_DOWN
    int ret_sync = gpio_pin_configure_dt(&sync_input, (GPIO_INPUT | GPIO_PULL_DOWN));
    if (ret_sync != 0) {
        printk("Erro %d configurando entrada de sincronização\n", ret_sync);
        return;
    }
    
    // Configura interrupção para Borda de SUBIDA (quando o sinal vai de 0 para 1)
    ret_sync = gpio_pin_interrupt_configure_dt(&sync_input, GPIO_INT_EDGE_RISING);
    if (ret_sync != 0) {
        printk("Erro %d configurando interrupção de sync\n", ret_sync);
        return;
    }
    
    // Registra o callback (sync_isr)
    gpio_init_callback(&sync_cb_data, sync_isr, BIT(sync_input.pin));
    gpio_add_callback(sync_input.port, &sync_cb_data);
    LOG_INF("ISR de Sincronização configurada em PTA5.");
    // --- FIM DA ADIÇÃO DA ISR ---

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