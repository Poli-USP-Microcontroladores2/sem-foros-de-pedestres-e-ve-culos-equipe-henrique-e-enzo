##Planejamento de testes do semáforo de pedestres - Modo Diurno
Utilizando um microcontrolador onde é implementado o código de controle deve-se observar as seguintes regras:
- O LED verde acende por 4 segundos
- O LED vermelho acende por 4 segundos
Duas threads independentes com exclusão mútua(mutex) controlam os LEDs.
#Método utilizado:
Com a função k_uptime_get() foi feita a captura dos tempos entre o início e o fim da função da thread do led, com isso foi obtido o seguinte resultado:
tempo de duração dos leds
LED verde - 4000 ms
LED vermelho - 6001 ms
LED verde - 6003 ms
LED vermelho - 6002 ms
LED verde - 6003 ms
LED vermelho - 6002 ms
Isso não é um erro, mas efeito esperado da exclusão mútua gerando um efeito de “somatório” nos tempos. A exclusão mútua foi confirmada, pois nunca há dois LEDs acesos simultaneamente, além disso a simples observação e cronometragem do tempo de cada LED confirma que os tempos estão corretos. Em vista disso um novo teste foi realizado, dessa vez colocando k_uptime_get antes e depois do acender e apagar dos leds pela função gpio_pin_set_dt e do k_msleep. Além disso aproveitando o momento de refatoração, optamos por deixar de usar printk e passamos a utilizar o LOG, então obtemos o seguinte resultado:
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
Portanto, Anteriormente foi calculado o tempo dentro da região protegida pelo mutex, incluindo o tempo que a thread ficava bloqueada esperando o mutex. Agora, ao alterar a posição de k_uptime_get(), foi calculado apenas o tempo do LED efetivamente aceso, após adquirir o mutex.
##Modo Noturno
O modo noturno foi implementado com uma thread exclusiva para simplesmente acender e apagar o LED. Para escolher entre o modo normal e modo noturno, optamos por deixar hard coded. Abaixo segue o log como evidência do funcionamento do modo noturno.

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

##Sincronismo entre as placas
Para sincronizar o semaforo de pedestres com o de veículos pela porta PTB1 enviamos um pulso curto indicando o final do ciclo do LED vermelho. No semaforo de veículos o sinal de sincronismo é recebido pela porta PTA05 que faz o seu led entrar no amarelo ao passo que o LED do de pedestres tem um atraso em um segundo para sincronizar, garantindo assim a interoperabilidade.
