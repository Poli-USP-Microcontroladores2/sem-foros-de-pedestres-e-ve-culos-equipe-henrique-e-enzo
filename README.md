# 🟢 Planejamento de Testes do Semáforo de Pedestres

## 🚦 Modo Diurno

O sistema foi implementado em um **microcontrolador** utilizando **Zephyr RTOS**, com o objetivo de controlar um semáforo de pedestres composto por dois LEDs: **vermelho** e **verde**.

### 🔧 Regras de Funcionamento
- O **LED verde** acende por **4 segundos**  
- O **LED vermelho** acende por **4 segundos**

O controle dos LEDs é feito por **duas threads independentes**, com **exclusão mútua (mutex)** para evitar que ambos os LEDs fiquem acesos simultaneamente.

---

### 🧪 Método de Teste

Foi utilizada a função `k_uptime_get()` para capturar os tempos entre o início e o fim da execução das threads dos LEDs.  
Os resultados obtidos inicialmente foram:

| Ciclo | LED | Duração (ms) |
|:------:|:----|:-------------:|
| 1 | Verde | 4000 |
| 2 | Vermelho | 6001 |
| 3 | Verde | 6003 |
| 4 | Vermelho | 6002 |
| 5 | Verde | 6003 |
| 6 | Vermelho | 6002 |

Esse comportamento **não é um erro**, mas sim um **efeito esperado da exclusão mútua**, que adiciona um pequeno atraso (efeito de “somatório” dos tempos).  
A exclusão mútua foi confirmada, pois **nunca há dois LEDs acesos simultaneamente**.  
A observação direta e a cronometragem confirmaram que os tempos estão corretos.

---

### 🔍 Teste Refinado

Em um novo teste, o `k_uptime_get()` foi reposicionado para medir o tempo **antes e depois do acionamento dos LEDs** (`gpio_pin_set_dt`) e do `k_msleep`.  

Durante essa refatoração, o sistema passou a usar o **módulo de log (`LOG`)** em vez de `printk`.  
O resultado foi o seguinte:

[00:00:00.000,000] <inf> semaforo_pedestres: Iniciando semáforo de pedestres...

[00:00:00.000,000] <inf> semaforo_pedestres: Modo normal ativado - ciclo 4s verde / 4s vermelho

[00:00:00.000,000] <inf> semaforo_pedestres: >>> VERMELHO LIGADO

[00:00:04.104,000] <inf> semaforo_pedestres: >>> VERDE LIGADO

[00:00:08.108,000] <inf> semaforo_pedestres: >>> VERDE DESLIGADO

[00:00:08.108,000] <inf> semaforo_pedestres: >>> VERMELHO LIGADO

[00:00:12.212,000] <inf> semaforo_pedestres: >>> VERDE LIGADO

[00:00:16.216,000] <inf> semaforo_pedestres: >>> VERDE DESLIGADO

[00:00:16.216,000] <inf> semaforo_pedestres: >>> VERMELHO LIGADO

[00:00:20.321,000] <inf> semaforo_pedestres: >>> VERDE LIGADO

[00:00:24.325,000] <inf> semaforo_pedestres: >>> VERDE DESLIGADO

[00:00:24.325,000] <inf> semaforo_pedestres: >>> VERMELHO LIGADO


📘 **Conclusão:**  
Anteriormente, o tempo era calculado **dentro da região protegida pelo mutex**, incluindo o tempo que a thread permanecia bloqueada.  
Após reposicionar o `k_uptime_get()`, o cálculo passou a considerar **apenas o tempo efetivo de LED aceso**, após adquirir o mutex.

---

## 🌙 Modo Noturno

O **modo noturno** foi implementado com uma **thread exclusiva** responsável por fazer o **LED vermelho piscar continuamente**.  
A seleção entre modo normal e modo noturno foi definida **via código (hard-coded)**.

### Log de funcionamento:

[00:00:00.000,000] <inf> semaforo_pedestres: Iniciando semáforo de pedestres...

[00:00:00.000,000] <inf> semaforo_pedestres: === MODO NOTURNO ATIVADO ===

[00:00:00.000,000] <inf> semaforo_pedestres: Sistema iniciado - PTB1 como saída de sincronismo

[00:00:00.000,000] <inf> semaforo_pedestres: LED VERMELHO LIGADO

[00:00:01.000,000] <inf> semaforo_pedestres: LED VERMELHO DESLIGADO

[00:00:02.000,000] <inf> semaforo_pedestres: LED VERMELHO LIGADO

[00:00:03.000,000] <inf> semaforo_pedestres: LED VERMELHO DESLIGADO

[00:00:04.000,000] <inf> semaforo_pedestres: LED VERMELHO LIGADO

[00:00:05.001,000] <inf> semaforo_pedestres: LED VERMELHO DESLIGADO

[00:00:06.001,000] <inf> semaforo_pedestres: LED VERMELHO LIGADO


---

## 🔄 Sincronismo Entre Placas

O **sincronismo** entre o semáforo de pedestres e o de veículos é feito através da porta **PTB1**, que envia um **pulso curto** indicando o **fim do ciclo do LED vermelho**.

No semáforo de veículos:
- O sinal é recebido pela porta **PTA05**
- O LED entra no **amarelo** quando o sinal é recebido
- O semáforo de pedestres mantém um **atraso de 1 segundo** para sincronizar corretamente, garantindo **interoperabilidade** entre os dois sistemas.

🎥 [Assista à demonstração do semáforo](./sincronismo.mp4)


---

🧠 **Resumo:**
- O sistema está funcional e sincronizado.  
- O controle por threads e mutexes garante exclusão mútua.  
- O modo noturno opera com thread independente.  
- Os tempos medidos estão dentro do esperado.
