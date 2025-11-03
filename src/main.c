#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

//Botão na porta PTA16
#define PORTA_NODE DT_NODELABEL(gpioa)
static const struct gpio_dt_spec button = {
	.port = DEVICE_DT_GET(PORTA_NODE),
	.pin = 16,
	.dt_flags = GPIO_ACTIVE_LOW, //botão ativo em nível baixo
};
static struct gpio_callback button_cb_data;

//Mutex para controle dos LEDs e semaforo para controle do botão de travessia
struct k_mutex semaforo_mutex;	//protege acesso concorrente às rotinas
struct k_sem sem_ped_request;	//pedido de travessia
struct k_sem sem_green;			//dado pela thread do led vermelho quando ela cede controle ao pedestre
struct k_sem sem_red_resume;		//dado pela thread verde quando termina o tempo de travessia

//Definições das threads dos LEDs
k_tid_t tid_red, tid_green;
K_THREAD_STACK_DEFINE(red_stack, 1024);
K_THREAD_STACK_DEFINE(green_stack, 1024);
static struct k_thread red_thread;
static struct k_thread green_thread;


//Configurações e definições dos LEDs
#define LED_GREEN_NODE_SLEEP_TIME_MS  4000
#define LED_RED_NODE_SLEEP_TIME_MS	  2000
#define LED_RED_NODE_SLEEP_TIME_MS_NT 1000

#define LED_RED_NODE DT_NODELABEL(red_led)
#define LED_GREEN_NODE DT_NODELABEL(green_led)

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);

//ISR - chamada quando o botão é pressionado
void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	static int64_t last_press_time = 0;
	int64_t now = k_uptime_get();

	//Ignora rebotes menores que 200 ms
	if (now - last_press_time < 200) return;

	last_press_time = now;
	printk("Botão pressionado - solicitando travessia\n");
	k_sem_give(&sem_ped_request);
}

//THREAD - LED verde
void fn_thread_led_verde(){
	while(1){
		//Espera o botão ser pressionado
		k_sem_take(&sem_green, K_FOREVER);

		//Inicio da região crítica protegida
		k_mutex_lock(&semaforo_mutex, K_FOREVER);
		printk("GREEN ON\n");
		// LED verde
		gpio_pin_set_dt(&led_green, 1);

		k_mutex_unlock(&semaforo_mutex);
        k_msleep(LED_GREEN_NODE_SLEEP_TIME_MS);
		k_mutex_lock(&semaforo_mutex, K_FOREVER);
		printk("GREEN OFF\n");
		gpio_pin_set_dt(&led_green, 0);


		k_mutex_unlock(&semaforo_mutex);

		//avisa a thread vermelha que pode retomar o ciclo
		k_sem_give(&sem_red_resume);
	}
}

//THREAD - LED vermelho
void fn_thread_led_vermelho(){
	while(1){
		//Inicio da região crítica protegida
		k_mutex_lock(&semaforo_mutex, K_FOREVER);
		printk("RED ON\n");
		gpio_pin_set_dt(&led_red, 1);
		k_mutex_unlock(&semaforo_mutex);

		int waited = 0;
        while (waited < LED_RED_NODE_SLEEP_TIME_MS) {
            if (k_sem_take(&sem_ped_request, K_MSEC(50)) == 0) {
                // botão pressionado -> interrompe vermelho
                break;
            }
            waited += 50;
        }

		k_mutex_lock(&semaforo_mutex, K_FOREVER);
		printk("RED OFF\n");
		gpio_pin_set_dt(&led_red, 0);
		k_mutex_unlock(&semaforo_mutex);

		//sinaliza a thread verde para iniciar travessia (agora o led vermelho está apagado)
		k_sem_give(&sem_green);

		//espera a thread verde terminar a travessia
		k_sem_take(&sem_red_resume, K_FOREVER);
	}
}

void fn_thread_led_vermelho_nt(void *p1, void *p2, void *p3){
	while(1){
		//Inicio da região crítica protegida
		k_mutex_lock(&semaforo_mutex, K_FOREVER);

		//k_uptime_get() para testar o tempo do LED
		//int64_t inicio = k_uptime_get();

		// LED vermelho
		gpio_pin_set_dt(&led_red, 1);
		k_msleep(LED_RED_NODE_SLEEP_TIME_MS_NT);
		gpio_pin_set_dt(&led_red, 0);

		//int64_t fim = k_uptime_get();

		k_mutex_unlock(&semaforo_mutex);
		k_msleep(LED_RED_NODE_SLEEP_TIME_MS_NT);
		//printk("LED vermelho - %lld ms\n", fim - inicio);
	}
}

void main(void) {
	int noturno = 0;

    // Verifica se os devices estão prontos
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        printk("Error: LED devices is not ready\n");
        return;
    }
	if (!device_is_ready(button.port)) {
		printk("Erro: GPIOA is not ready\n");
	}

	//configurar o botão
	gpio_pin_configure(button.port, button.pin, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(button.port, button.pin, GPIO_INT_EDGE_TO_INACTIVE); // borda de descida (pressionar)
	gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	
	//configurar LEDs (começam apagados)
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	/*if(!noturno) {
		gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
	}*/

	//inicializa sincronizadores
	k_mutex_init(&semaforo_mutex);
	k_sem_init(&sem_ped_request, 0, 1);
	k_sem_init(&sem_green, 0, 1);
	k_sem_init(&sem_red_resume, 0, 1);

	if(noturno == 1) {
		tid_red = k_thread_create(&red_thread, red_stack, K_THREAD_STACK_SIZEOF(red_stack),fn_thread_led_vermelho_nt, (void *)(intptr_t)1, NULL, NULL, 5, 0, K_NO_WAIT);
		k_thread_join(tid_red, K_FOREVER);
	} else {
		tid_red = k_thread_create(&red_thread, red_stack, K_THREAD_STACK_SIZEOF(red_stack),fn_thread_led_vermelho, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
		tid_green = k_thread_create(&green_thread, green_stack, K_THREAD_STACK_SIZEOF(green_stack),fn_thread_led_verde, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
		
		//k_thread_join(tid_green, K_FOREVER);
		//k_thread_join(tid_red, K_FOREVER);

		//começa o ciclo com vermelho
		k_sem_give(&sem_red_resume);
	}
}