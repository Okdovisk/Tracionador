/*  
    Include das bibliotecas necessárias.
    Algumas dessas bibliotecas (TFT_Layout_Express e Imperial_March) são simplesmente cabeçalhos e 
    foram criadas apenas para diminuir a quantidade de #defines feitos aqui. Além disso, seriam usadas
    para a criação de classes, enxugando ainda mais o código principal. Porém, não houve tempo para 
    reestruturar o código como eu gostaria. Pelo menos não até agora.

    Outras bibliotecas podem ser encontradas facilmente nas bibliotecas principais do Platformio ou 
    serem inseridas como lib_deps no arquivo platformio.ini
*/
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <TFT_Layout_Express.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <AiEsp32RotaryEncoder.h>
#include <Imperial_March.h>

/* Include das imagens usadas no menu. */
#include <images/Settings_16.h>
#include <images/Play_16.h>
#include <images/Author_16.h>
#include <images/Perfil_16.h>
#include <images/Automatic_16.h>
#include <images/Manual_16.h>
#include <images/Test_16.h>
#include <images/Velocity_16.h>
#include <images/Autor_close_112_76.h>
#include <images/Orientador_close_112_76.h>
#include <images/Pedro_M_36_60.h>
#include <images/Vinicius_N_36_60.h>
#include <images/Pedro_Lucas_N_36_60.h>

/* Definição dos pinos e suas funcionalidades */
#define BUTTON_CONFIRM 33           /* Botões para interface com o menu. */
#define BUTTON_NEXT 34
#define BUTTON_PREVIOUS 35
#define BUTTON_RETURN 32
#define BUZZER_PIN 15               /* Pino do buzzer. */
#define LED_PIN 2                   /* Pino do LED vermelho. */
#define LED_CHANNEL 2               /* Canal para controlar o led. */
#define ROTARY_ENCODER_PHASE_A 12   /* Pino da fase A do encoder. */
#define ROTARY_ENCODER_PHASE_B 13   /* Pino da fase B do encoder. */
#define ROTARY_ENCODER_BUTTON 21    /* Botão para auxiliar nas leituras (espero que seja um botão facultativo). */
#define ROTARY_ENCODER_STEPS 4      /* Número de passos. Ainda não está claro para mim como isso funciona. */
#define ROTARY_ENCODER_VCC -1       /* Forma de alimentação do encoder. Penso que -1 significa que ele será alimentado diretamente da fonte. */
#define MIN_VALUE 0                 /* Valor mínimo das leituras do encoder. */
#define MAX_VALUE 5000              /* Valor máximo das leituras do encoder. */
#define IS_C_PIN 26                 /* Pino para leitura do sensor de corrente do motor no sentido horário. */
#define IS_CC_PIN 25                /* Pino para leitura do sensor de corrente do motor no sentido anti-horário. */
#define PWM_C_PIN 14                /* Pino de giro do sentido horário. */
#define PWM_CC_PIN 27               /* Pino de giro do sentido anti-horário. */
#define PWM_FREQUENCY 10000         /* Frequência do sinal PWM [10 kHz]. */
#define PWM_C_CHANNEL 0             /* Canal do sinal PWM do sentido horário [0]. */
#define PWM_CC_CHANNEL 1            /* Canal do sinal PWM do sentido anti-horário [1]. */
#define PWM_RESOLUTION 8            /* Resolução do sinal PWM [8 bits][0-255]. */

/* Criação dos objetos para manipular e imprimir as páginas do menu. */
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite layout = TFT_eSprite(&tft);     /* Todo o menu foi impressa em formato de Sprite, um recurso que melhora a exibição em alguns aspectos (Ler a documentação da biblioteca). */

/* Criação do objeto encoder com seus devidos atributos. */
AiEsp32RotaryEncoder encoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_PHASE_A, ROTARY_ENCODER_PHASE_B, ROTARY_ENCODER_BUTTON, ROTARY_ENCODER_VCC, ROTARY_ENCODER_STEPS);

uint num_max_options = 0U;        /* Número de opções existentes dentro de uma página específica. */
     
const uint vector_length = 15U;   /* Tamanho padrão das strings que especificam as páginas/opções. */

uint8_t page = 0,             /* Marcador de página. Juntos, os marcadores mapeiam a posição do usuário dentre as páginas e opções. */
        option = 1,           /* Marcador de opção. */
        option_line = 0,      /* Marcador do número da linha no display da opção em seleção. */
        i = 0,                /* Contador para sincronizar o piscar da opção em seleção e a troca de imagens nos créditos. */
        v = 0,                /* Contador para manipular as posições no vetor de cálculo da velocidade média, aquela que é exibida em tela durante o rastreio da velocidade de referência. */
        credits = 1;          /* Marcador do participante do projeto na página de créditos. */

uint16_t velocity_reference = 300,    /* Velocidade de referência [mm/s]. */
         velocity_measured = 0,       /* Velocidade medida/calculada pela lei de controle [mm/s]. */
         velocity_vec[100],           /* Vetor de velocidades para melhorar a exibição em tela [mm/s]. */
         velocity_mean = 0,           /* Valor médio da velocidade. Valor exibido em tela durante o rastreio. */
         thisNote = 0,                /* Contador para manipular a seleção das notas da marcha imperial a serem tocadas durante os créditos. */
         pulses = 0;                  /* Contador da quantidade de pulsos lida pelo encoder. */

double pulley_radius = 72.7,           /* raio da polia de tração [72.7 mm] */
       velocity_rads_reference = velocity_reference/pulley_radius,    /* Velocidade de referência em rad/s.*/
       velocity_rads_measured = 0.0,          /* As velocidades em rad/s são importantes porque o controlador foi desenvolvido nessa métrica e só pode operar nela. */
       uk = 0.0,                              /* Housekeeping das entradas e erros do controlador. */
       ukm1 = 0.0,
       error = 0.0,
       errorm1 = 0.0;

/* Strings para gerenciar a exibição dos cabeçalhos, opções e palavras variadas apresentadas no menu. */
const char headers[4][vector_length] = {"MENU", "ENSAIO", "CONFIG","CREDITOS"},     /* Strings de cabeçalho. */
           menu[3][vector_length] = {"Ensaio","Configuracoes","Creditos"},          /* Strings da página inicial (menu). */
           ensaio[2][vector_length] = {"Manual","Automatico"},                      /* Strings da página ensaio. */
           manual[2][vector_length] = {"Iniciar", "Vref[mm/s]="},                   /* Strings da página ensaio-manual. */
           iniciar[3][vector_length] = {"Perfil: ", "Vref[mm/s] = ", "Vmed[mm/s] = "},  /* Talvez não seja necessário colocar páginas especiais nesse formato. */
           rotation_sense[2][vector_length] = {"a-horario", "horario"};

const char *head_option = &menu[0][0],                 /* As funções drawString exigem um ponteiro para um const char caso não seja escrita
                                                      diretamente uma string. Assim, para alternar entre diferentes palavras de forma 
                                                      mais organizada entre as páginas, é preciso trabalhar com essa estrutura de arquivos.
                                                      Coisa que ainda não implementei porque demanda mais tempo. */
           *head_page = &headers[0][0],
           *head_sense = &rotation_sense[0][0];

bool status_button_confirm = false,              /* Status atuais dos botões de interface. */
     status_button_next = false,
     status_button_previous = false,
     status_button_return = false,
     previous_status_button_confirm = false,     /* Status passados dos botões de interface. */
     previous_status_button_next = false,
     previous_status_button_previous = false,
     previous_status_button_return = false,
     page_change = true,        /* Marcador de troca de página. */
     page_increment = true,     /* Marcador de incremento/decremento de página. Booleano necessário para a função pageDecoder fazer a navegação. */
     special_page = false,      /* Marcador de página especial em exibição. */
     option_change = true,      /* Marcador de mudança de opção. */
     track = false,             /* Marcador de acesso à página de rastreamento, começando com a função PAUSE dentro da página especial de rastreamento. */
     tracking = false,          /* Marcador do estado ativo de rastreamento, função PLAY dentro da página especial de rastreamento. Quando confirm é novamente pressionado, entra na função PAUSE. */
     counter_clockwise = true,  /* Marcador do sentido de giro do motor. */
     circle_values = true;      /* Tipo de funcionamento do encoder. Circle values: após registrar o último pulso (5000), o próximo reseta a contagem, retornando ao 1º pulso. */

// Dentro das funções/páginas/tasks existem mais detalhes a respeito de suas funções, observações, etc.
void mainLayout(void);              /* Função para carregar a estrutura completa da página principal. */
void feedMenu(void);                /* Função para destacar a opção em seleção. */
void taskControl(void *arg);        /* Task para gerenciar o controle: controlador digital, cálculos, ... */
void taskCredits(void *arg);        /* Task para gerenciar a música dos créditos */
void taskInterface(void *arg);      /* Task para gerenciar a interface: Ordena e exibe as páginas de interface. */
void taskButtonManager(void *arg);  /* Task para gerenciar os estados dos botões. */
void nextOption(void);              /* Função para avançar à próxima opção. */
void previousOption(void);          /* Função para retroceder à opção anterior. */
void confirmOption(void);           /* Função para confirmar a escolha de seleção. */
void confirmAction(void);           /* Função para confirmar a ação na página especial. */
void returnPage(void);              /* Função para retornar à página anterior. */
void change_vref(void);             /* Função para confirmar (salvar em memória) a nova velocidade de referência. */
void ensaioLayout(void);            /* Exibição da página de ensaio (página 1). */
void configuracoesLayout(void);     /* Exibição da página de configurações (página 2). */
void creditosLayout(void);          /* Exibição da página de créditos (página 3). */
void ensaioManualLayout(void);      /* Exibição da página de configuração do ensaio manual (página 4). */
void ensaioAutomaticoLayout(void);  /* Exibição da página de início de ensaio automático (página 5). */
void manualInicioLayout(void);      /* Exibição da página de início de ensaio manual (página 6). */
void manualVrefLayout(void);        /* Exibição da página de configração da velocidade de referência do ensaio manual (página 7). */
void resetLayout(uint8_t);          /* Função para carregar novamente o sprite da página atual. */
int pageDecoder(void);              /* Função para decodificar o número da página a ser carregada. */
double velocityCalculation(uint16_t, unsigned long, bool);
void IRAM_ATTR readEncoderISR();

TaskHandle_t xHandlerInterface,     /* Handlers podem ser bastante úteis para suspender e reativar tarefas. */
             xHandlerButtons,       /* Caso eu não precise de tais funcionalidades, eles podem ser removidos para econimizar espaço. */
             xHandlerPages,
             xHandlerControl,
             xHandlerCredits;       
  
void setup()
{
  pinMode(BUTTON_CONFIRM, INPUT_PULLUP);     /* Declaração dos pinos dos botões e potenciômetro. */
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PREVIOUS, INPUT_PULLUP);
  pinMode(BUTTON_RETURN, INPUT_PULLUP);

  ledcSetup(PWM_C_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);    // Criando o canal PWM horário com a resolução e frequência especificada.
  ledcAttachPin(PWM_C_PIN, PWM_C_CHANNEL);                    // Anexando o pino de giro do sentido horário ao canal criado.
  ledcSetup(PWM_CC_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);   // Criando o canal PWM anti-horário com a resolução e frequência especificada.
  ledcAttachPin(PWM_CC_PIN, PWM_CC_CHANNEL);                  // Anexando o pino de giro do sentido anti-horário ao canal criado.
  ledcSetup(LED_CHANNEL, 1000, 8);
  ledcAttachPin(LED_PIN, LED_CHANNEL);

  encoder.begin();                      // É preciso ativar o encoder com a função begin.
  encoder.setup(readEncoderISR);        // É preciso configurar o encoder com a função redEncoderISR
  encoder.setBoundaries(MIN_VALUE, MAX_VALUE, circle_values);
  encoder.disableAcceleration();    // Enquanto eu ainda não entendo direito pra que isso serve, talvez seja melhor deixar desativado.

  Serial.begin(115200);             /* Abertura de comunicação serial somente para fins de debug. */
  tft.begin();                      /* Inicialização da biblioteca TFT_eSPI. */
  layout.createSprite(130, 131);    /* Inicialização da biblioteca TFT_eSprite. */
  mainLayout();                     /* Exibição da tela inicial de menu. */

  xTaskCreatePinnedToCore(taskControl,        /* Criação da tarefa de controle ligada ao processador PRO. */
                          "Control",
                          2048,
                          NULL,
                          8,
                          &xHandlerControl,
                          PRO_CPU_NUM);

  xTaskCreatePinnedToCore(taskCredits,        /* Criação da tarefa de créditos para tocara  marcha imperial ligada ao processador PRO. */
                          "Credits",
                          2048,
                          NULL,
                          10,
                          &xHandlerCredits,
                          APP_CPU_NUM);
  
  
  xTaskCreatePinnedToCore(taskInterface,      /* Criação da tarefa de interface ligada ao processador APP. */
                          "Interface",
                          2048,
                          NULL,
                          8,
                          &xHandlerInterface,
                          APP_CPU_NUM);
  
  xTaskCreatePinnedToCore(taskButtonManager,  /* Criação da tarefa gerenciadora de botões ligada ao processador APP. */
                          "Buttons",
                          1024,
                          NULL,
                          7,
                          &xHandlerButtons,
                          APP_CPU_NUM);
  
}

void loop()
{
  /* A função loop funciona como uma tarefa ligada ao processador APP, mas não sei quanto de memória é alocado a ela,
  nem seu nível de prioridade ou outras informações. Por enquanto deixo-a vazia. */
  vTaskSuspend(NULL);
}

void IRAM_ATTR readEncoderISR()  // Função ISR para configuração da classe AiEsp32RotaryEncoder.
{
  encoder.readEncoder_ISR();
}

void taskControl(void *arg)
{
  /* Código de gerenciamento de controle.
      Gerenciamento de tempo, atualizaçao da lei de controle,
      e demais processamentos necessários para um controle de sucesso. */
  TickType_t xFirstWakeUp,      /* Variáveis para garantir a periodicidade de execução da tarefa. */
             xFrequency = 5;

  xFirstWakeUp = xTaskGetTickCount();   /* Uma vez determinado o tempo inicial da tarefa, ela se orienta a partir da
                                        frequência definida para deixar a tarefa em espera pelo tempo determinado (5 ms). */
  while(true)
  {
    // Leitura do encoder
    // Implementação dos filtros
    // Cálculos de velocidade e referência
    // Aplicação da lei de controle
    // Housekeeping
    if(tracking)       // O início do rastreamento só começará quando for confirmado a partir de sua devida página.
    {
      pulses = encoder.readEncoder();     // Leitura dos pulsos do encoder.
      velocity_rads_measured = velocityCalculation(pulses, xFrequency, counter_clockwise);    // Cálculo da velocidade em rad/s. (Tenho quase certeza que será chamada a cada 5 ms, dispensando o segundo parâmetro da função)
      //velocity_measured = int(velocity_rads_measured*pulley_radius);
      encoder.reset();    // Devo resetar a variável de leituras do encoder a cada cálculo de velocidade.

      velocity_vec[v++] = velocity_rads_measured*pulley_radius;
      if(v == 100)
      {
        v = 0;
        velocity_mean = 0;
      }
      

      for(int j = 0; j < 100; j++)
      {
        velocity_mean += velocity_vec[j];
      }
      velocity_mean /= 100;
      // Testando o cálculo de controle sem a utilização do filtro Butterworth usado anteriormente.

      error = velocity_rads_reference - velocity_rads_measured;        // Calculando o erro.
      uk = ukm1 + 0.1075*error - 0.0925*errorm1;    // Calculando a entrada. (0.1075*error - 0.0925*errorm)

      uk = min(uk, 1.0);    // Saturação superior da entrada.
      uk = max(uk, 0.0);    // Saturação inferior da entrada.

      if(counter_clockwise)   // A entrada será aplicada num canal a depender do sentido de giro definido.
      {
        ledcChangeFrequency(PWM_CC_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcWrite(PWM_CC_CHANNEL, uk*255);  // A entrada preisa ser multiplicada por 255 porque a variável uk é normalizada.
      }
      else
      {
        ledcChangeFrequency(PWM_C_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcWrite(PWM_C_CHANNEL, uk*255);
      }

      ukm1 = uk;          // Housekeeping da entrada e do erro.
      errorm1 = error;
    }
    else
    {
      velocity_mean = 0;
      if(counter_clockwise)   // A entrada será aplicada num canal a depender do sentido de giro definido.
      {
        ledcWrite(PWM_CC_CHANNEL, 0);  // A entrada preisa ser multiplicada por 255 porque a variável uk é normalizada.
      }
      else
      {
        ledcWrite(PWM_C_CHANNEL, 0);
      }
    }

    vTaskDelayUntil(&xFirstWakeUp, xFrequency);
  }
}

void taskCredits(void *arg)
{
  TickType_t xDelay = 0;
  vTaskSuspend(NULL);

  while(true)
  {
    for (thisNote = 0; thisNote < notes; thisNote = thisNote + 2)
    {
      // calculates the duration of each note
      divider = melody[thisNote + 1];

      if (divider > 0)
      {
        // regular note, just proceed
        duration = (wholenote) / divider;
      }
      else if (divider < 0)
      {
        // dotted notes are represented with negative durations!!
        duration = (wholenote) / abs(divider);
        duration *= 1.5; // increases the duration in half for dotted notes
      }
      else
      {
        duration = 0;
      }

      // we only play the note for 90% of the duration, leaving 10% as a pause
      tone(BUZZER_PIN, melody[thisNote], duration * 0.9);

      // Wait for the specief duration before playing the next note.
      xDelay = duration / portTICK_PERIOD_MS;
      vTaskDelay(xDelay);

      // stop the waveform generation before the next note.
      noTone(BUZZER_PIN);
    }
  }
}

void taskInterface(void *arg)
{
  TickType_t xFirstWakeUp,      /* Variáveis de controle do tempo de espera da tarefa. */
             xFrequency = 250;

  xFirstWakeUp = xTaskGetTickCount();
  while(true)
  {
    /* Quando uma página for selecionada, o return for pressionado ou outra condição pertinente, será preciso
    carregar novamente uma página. */
    if(page_change)   
    {
      switch(page)   /* O marcador de página será consultado para que a página atual seja carregada. */
      {
      case 0:         /* Contruir o layout principal. */
        option_change = true;     /* option_change precisa ser acionado para que as opções da página sejam carregadas. */
        page_change = false;      /* Uma vez que a página é carregada não será mais necessário atualizá-la. */
        mainLayout();             /* Exibição da página completa de menu. */
        num_max_options = sizeof(menu)/vector_length; /* Definição da quantidade de opções existentes dentro da página. */
        break;

      case 1:         /* Construir o layout da página Ensaio. */
        option_change = true;     /* Segue a ideia acima para carregar todas as páginas do menu. */
        page_change = false;
        ensaioLayout();
        num_max_options = sizeof(ensaio)/vector_length;
        break;

      case 2:         /* Contruir o layout da página Configurações. */
        configuracoesLayout();
        option_change = true;
        page_change = false;
        break;

      case 3:         /* Contruir o layout da página Créditos. */
        option_change = true;
        page_change = false;
        ledcWrite(LED_CHANNEL, 255);
        vTaskResume(xHandlerCredits);
        //esp_ipc_call(PRO_CPU_NUM, (esp_ipc_func_t)PROMenager, NULL);
        creditosLayout();
        break;

      case 4:         /* Construir a página de ensaio manual. */
        option_change = true;
        page_change = false;
        //ensaioIniciarLayout();
        ensaioManualLayout();
        num_max_options = sizeof(manual)/vector_length;
        break;

      case 5:         /* Construir a página de ensaio automático (página especial). */
        option_change = true;
        page_change = false;
        ensaioAutomaticoLayout();
        break;
      
      case 6:         /* Construir a página de Início de ensaio manual (página especial). */
        option_change = true;
        page_change = false;
        manualInicioLayout();
        break;
      
      case 7:         /* Construir a página de configuração da velocidade de referência da página manual (página especial). */
        option_change = true;
        page_change = false;
        manualVrefLayout();
        break;
      
      case 8:
        option_change = true;
        page_change = false;
        break;

      default:        /* Caso nenhuma página seja identificada, carregar o menu principal (resetar o menu). */
        option_change = true;
        page_change = false;
        page = 0;
        mainLayout();
        break;
      }
    }

    feedMenu();     /* A função feedMenu sempre será chamada, visto que ela é responsável por piscar a opção em
                    seleção durante a naveção pelo menu. */
    vTaskDelayUntil(&xFirstWakeUp, xFrequency);   /* Uma vez executada, ela deverá esperar 250 ms para começar novamente. */
  }
}

void taskButtonManager(void *arg)   /* Tarefa para o gerenciamento dos botões e potenciômetro. */
{
  TickType_t xFirstWakeUp,
             xFrequency = 25;
  xFirstWakeUp = xTaskGetTickCount();
  while(true)
  {
    status_button_confirm = digitalRead(BUTTON_CONFIRM);      /* Atualização dos status dos botões. */
    status_button_next = digitalRead(BUTTON_NEXT);            
    status_button_previous = digitalRead(BUTTON_PREVIOUS);    
    status_button_return = digitalRead(BUTTON_RETURN);

    /* Condições para identificar que o botão foi pressionado.
       Cada botão ativa uma função específica ao ser pressionado. */
    if(status_button_next != previous_status_button_next && status_button_next == 0)
    {
      if(special_page)
      {
        switch(page)
        {
        case 6:
          if(!tracking)
          {
            counter_clockwise = false;
            head_sense = rotation_sense[0];
            resetLayout(page);
          }
          break;

        case 7:
          velocity_reference -= 10;
          velocity_rads_reference = velocity_reference/pulley_radius;
          resetLayout(page);
          break;
        
        default:
          break;
        }
      }
      else
      {
        nextOption();
      }
    }else if (status_button_confirm != previous_status_button_confirm && status_button_confirm == 0)
    {
      if(special_page)
      {
        confirmAction();
      }
      else
      {
        confirmOption();
      }
    }else if(status_button_previous != previous_status_button_previous && status_button_previous == 0)
    {
      if(special_page)
      {
        switch(page)
        {
        case 6:
          if(!tracking)
          {
            counter_clockwise = true;
            head_sense = rotation_sense[1];
            resetLayout(page);
          }
          break;
        
        case 7:
          velocity_reference += 10;
          velocity_rads_reference = velocity_reference/pulley_radius;
          resetLayout(page);
          break;
        
        default:
          break;
        }
      }
      else
      {
      previousOption();
      }
    }else if(status_button_return != previous_status_button_return && status_button_return == 0)
    {
      returnPage();
    }
    
    previous_status_button_confirm = status_button_confirm;     /* Housekeeping do status dos botões para saber quando algum foi pressionado. */
    previous_status_button_next = status_button_next;
    previous_status_button_previous = status_button_previous;
    previous_status_button_return = status_button_return;

    vTaskDelayUntil(&xFirstWakeUp, xFrequency);
  }
}

void mainLayout(void)  // Main layout é a tela do primeiro menu, ao carregar o sistema.
{ 
  /* As primeiras linhas do menu e o formato das informações são iguais para todas as páginas. */
  layout.setSwapBytes(false);
  layout.fillScreen(BLACK);
  layout.fillRect(10, 10, 112, 30, WHITE);   // Retangulos do cabeçalho
  layout.fillRect(20, 15, 92, 20, BLACK);

  layout.fillRect(48, 118, 34, 15, WHITE);   // Retangulos do rodapé
  layout.fillRect(50, 119, 30, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  layout.drawFastHLine(4, 125, 44, WHITE);
  layout.drawFastHLine(82, 125, 43, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  layout.drawCentreString("UFU", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);    /* Strings do menu */
  layout.drawCentreString("MENU", TFT_MIDDLE_COLUM, TFT_HEADER_LINE, TFT_MAIN_FONT);
  layout.drawString("Ensaio", TFT_OPTIONS_COLUM, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString("Configuracoes", TFT_OPTIONS_COLUM, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString("Creditos", TFT_OPTIONS_COLUM, TFT_THIRD_OPTION_LINE, TFT_MAIN_FONT);

  layout.setSwapBytes(true);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE, 16, 16, Test_16);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_SECOND_IMAGE_LINE, 16, 16, Settings_16);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_THIRD_IMAGE_LINE, 16, 16, Author_16);

  layout.pushSprite(0, 0);
}

void feedMenu(void)  /* Função para guiar visualmente o usuário, piscando a opções em seleção. */
{                    /* Para que a opção correta esteja piscando, é preciso rastrear os cliques dos botões. */
  if(option_change)     /* option_change será ativado com o clique de determinados botões ou em condições especiais. */
  {                     
    resetLayout(page);  /* É preciso contruir novamente a página para evitar que uma opção que não esteja em seleção fique */
    switch (page)       /* destacada devido ao instante em que o usuário pressionou o botão. */
    {
    case 0:
      head_option = menu[option - 1];   /* O ponteiro das strings da página em questão deve ser carregado para que sejam exibidas. */
      break;
    case 1:
      head_option = ensaio[option - 1];
      break;
    case 2:
      break;
    case 4:
      head_option = manual[option - 1];
      break;
    default:
      break;
    }

    switch (option)     /* Atualização do número da linha referente à opção em seleção. */
    {
    case 1:
      option_line = TFT_FIRST_OPTION_LINE;
      option_change = false;
      break;
    case 2:
      option_line = TFT_SECOND_OPTION_LINE;
      option_change = false;
      break;
    case 3:
      option_line = TFT_THIRD_OPTION_LINE;
      option_change = false;
      break;
    case 4:
      option_line = TFT_FORTH_OPTION_LINE;
      option_change = false;
      break;
    }
    
  }

  if(special_page)  // Inicialmente, vou colocar a atualização do valor da velocidade medida diretamente no feedMenu.
  {                 // Posteriormente, pode ser interessante criar um pouco mais de abstração.
    switch(page)
    {
    case 6:
      if(track)       /* O track será ativado somente quando o botão de confirmação for pressionado enquanto o usuário está na página de inicia ensaio. Aqui ela serve somente para ativar a exibição e incrementar o valor. */
      {
        if(tracking)
        {
          layout.drawString("Vmed[mm/s]=", TFT_IMAGE_COLUM, TFT_THIRD_OPTION_LINE, TFT_MAIN_FONT);
          layout.drawString(String(velocity_mean), 85, TFT_THIRD_OPTION_LINE, TFT_MAIN_FONT);
          layout.drawString("RASTREANDO", TFT_IMAGE_COLUM, TFT_FORTH_IMAGE_LINE, TFT_MAIN_FONT);
          layout.pushSprite(0, 0);
        }
        else
        {
          layout.drawString("Vmed[mm/s]=", TFT_IMAGE_COLUM, TFT_THIRD_OPTION_LINE, TFT_MAIN_FONT);
          layout.drawString(String(velocity_mean), 85, TFT_THIRD_OPTION_LINE, TFT_MAIN_FONT);
          layout.drawString("PARADO", TFT_IMAGE_COLUM, TFT_FORTH_OPTION_LINE, TFT_MAIN_FONT);
          layout.pushSprite(0, 0);
        }
      
      }
      break;

    case 3:
      if(i++ % 16 == 0)
      {
        // Sempre que o contador for múltiplo de 16 (passados 4 segundos), troca-se o alvo dos créditos.
        switch (credits++)
        {
        case 1:     // Autor
          layout.setSwapBytes(false);
          layout.fillScreen(BLACK);

          layout.fillRect(41, 118, 49, 15, WHITE);   // Retangulos do rodapé Autor
          layout.fillRect(43, 119, 45, 13, BLACK);

          layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
          layout.drawFastVLine(4, 5, 120, WHITE);
          layout.drawFastVLine(127, 5, 120, WHITE);
          layout.drawFastHLine(4, 125, 36, WHITE);  // Autor
          layout.drawFastHLine(92, 125, 34, WHITE);

          layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
          layout.fillCircle(127, 5, 2, WHITE);
          layout.fillCircle(4, 125, 2, WHITE);
          layout.fillCircle(127, 125, 2 ,WHITE);
          layout.drawCentreString("AUTOR", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);

          layout.drawCentreString("Alexandre", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
          layout.drawCentreString("Mendes Marcelo", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);

          layout.setSwapBytes(true);
          layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE - 1, 112, 76, Autor_close_112_76);

          layout.setPivot(131, 133);
          layout.pushRotated(180);

          break;
        
        case 2:     // Orientador
          layout.setSwapBytes(false);

          layout.fillRect(21, 118, 88, 15, WHITE);   // Retangulos do rodapé Orientador
          layout.fillRect(23, 119, 84, 13, BLACK);

          layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
          layout.drawFastVLine(4, 5, 120, WHITE);
          layout.drawFastVLine(127, 5, 120, WHITE);
          layout.drawFastHLine(4, 125, 17, WHITE);    // Orientador
          layout.drawFastHLine(109, 125, 16, WHITE);

          layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
          layout.fillCircle(127, 5, 2, WHITE);
          layout.fillCircle(4, 125, 2, WHITE);
          layout.fillCircle(127, 125, 2 ,WHITE);
          layout.drawCentreString("ORIENTADOR", TFT_MIDDLE_COLUM, TFT_LOGO_LINE, TFT_MAIN_FONT);

          layout.drawCentreString("Pedro Augusto", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
          layout.drawCentreString("Queiroz de Assis", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);

          layout.setSwapBytes(true);
          layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE - 1, 112, 76, Orientador_close_112_76);

          layout.setPivot(131, 133);
          layout.pushRotated(180);

          break;
        
        case 3:     // Apoio
          layout.setSwapBytes(false);
          layout.fillScreen(BLACK);

          layout.fillRect(41, 118, 49, 15, WHITE);   // Retangulos do rodapé Apoio
          layout.fillRect(43, 119, 45, 13, BLACK);

          layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
          layout.drawFastVLine(4, 5, 120, WHITE);
          layout.drawFastVLine(127, 5, 120, WHITE);
          layout.drawFastHLine(4, 125, 36, WHITE);    // Apoio
          layout.drawFastHLine(92, 125, 34, WHITE);

          layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
          layout.fillCircle(127, 5, 2, WHITE);
          layout.fillCircle(4, 125, 2, WHITE);
          layout.fillCircle(127, 125, 2 ,WHITE);
          layout.drawCentreString("APOIO", TFT_MIDDLE_COLUM, TFT_LOGO_LINE, TFT_MAIN_FONT);

          layout.drawCentreString("Pedro M Vinicius N", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
          layout.drawCentreString("Pedro Lucas N", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);

          layout.setSwapBytes(true);
          layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE + 7, 36, 60, Pedro_M_36_60);
          layout.pushImage(TFT_IMAGE_COLUM + 38, TFT_FIRST_IMAGE_LINE +7, 36, 60, Vinicius_N_36_60);
          layout.pushImage(TFT_IMAGE_COLUM + 76, TFT_FIRST_IMAGE_LINE + 7, 36, 60, Pedro_Lucas_N_36_60);

          layout.setPivot(131, 133);
          layout.pushRotated(180);

          break;

        case 4:
          credits = 1;
          i = 0;

          break;
        }
      }
      break;
    }
  }
  else
  {
    if(i++ % 2 == 0)  /* Escopo onde acontece o piscar da opção em seleção com período de 250 ms. */
    {
      layout.setTextColor(BLACK, WHITE);
      layout.drawString(head_option, TFT_OPTIONS_COLUM, option_line, TFT_MAIN_FONT);
      layout.setTextColor(WHITE, BLACK);

      layout.pushSprite(0, 0);
    }
    else
    {
      layout.drawString(head_option, TFT_OPTIONS_COLUM, option_line, TFT_MAIN_FONT);
      i = 0;

      layout.pushSprite(0, 0);
    }
  }
  
}

void nextOption(void)   /* Função para ir à próxima opção em seleção. */
{
  option++;
  if(option > num_max_options)
    {
      option = 1;
    }
  option_change = true;
}

void previousOption(void) /* Função para retornar à opção em seleção anterior. */
{
  option--;
  if(option == 0)
    {
      option = num_max_options;
    }
  option_change = true;
}

void confirmAction(void)  /* Função para ativar a(s) funcionalidade de uma página especial. */
{
  switch (page)
  {
  case 3:
    returnPage();
    break;

  case 5:   /* Ao confirmar dentro da página automático, começará o rastreamento da velocidade de referência. */
    tracking = true;
    break;

  case 6:   /* Ao confirmar dentro da página inicar(manual), o rastreamento da velocidade de referência será ativado/desativado. */
    tracking = !tracking;
    resetLayout(page);
    break;
  
  case 7:   /* Os botões previous e next configuram a velocidade de referência. */
    change_vref();
    break;
  
  default:
    break;
  }
}

void confirmOption(void)  /* Função para confirmar a opção em seleção e avançar na navegação do menu. */
{
  page_increment = true;
  page = pageDecoder();   /* pageDecoder retém a lógica de cálculo para a próxima página do menu. */
}

void returnPage(void)     /* Função para retornar à última página. */
{
  page_increment = false;   /* Ativando o decremento para que pageDecoder faça a alteração devida. */
  page = pageDecoder();
}

void resetLayout(uint8_t page)    /* Em algumas condições é interessante construir novamente a página do menu. */
{                                 /* i.e: mudança de página, mudança de opção em seleção, etc. */
  switch(page)
  {
  case 0:
    mainLayout();
    break;

  case 1:
    ensaioLayout();
    break;

  case 2:
    configuracoesLayout();
    break;

  case 3:
    creditosLayout();
    break;

  case 4:
    //ensaioIniciarLayout();
    ensaioManualLayout();
    break;

  case 5:
    //ensaioPerfilLayout();
    ensaioAutomaticoLayout();
    break;
  
  case 6:
    manualInicioLayout();
    break;
  
  case 7:
    manualVrefLayout();
    break;;

  default:
    break;
  }
}

void ensaioLayout(void)   /* Função para construir a página de ensaio. */
{
  layout.setSwapBytes(false);
  layout.fillScreen(BLACK);
  layout.fillRect(10, 10, 112, 30, WHITE);   // Retangulos do cabeçalho
  layout.fillRect(20, 15, 92, 20, BLACK);

  layout.fillRect(48, 118, 34, 15, WHITE);   // Retangulos do rodapé
  layout.fillRect(50, 119, 30, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  layout.drawFastHLine(4, 125, 44, WHITE);
  layout.drawFastHLine(82, 125, 43, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  layout.drawCentreString("UFU", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);
  layout.drawCentreString("ENSAIO", TFT_MIDDLE_COLUM, TFT_HEADER_LINE, TFT_MAIN_FONT);
  layout.drawString("Manual", TFT_OPTIONS_COLUM, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString("Automatico", TFT_OPTIONS_COLUM, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);

  /* Trocar as imagens para as novas opções. */
  layout.setSwapBytes(true);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE, 16, 16, Manual_16);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_SECOND_IMAGE_LINE, 16, 16, Automatic_16);

  layout.pushSprite(0, 0);
}

void configuracoesLayout(void)  /* Função para construir a página de configurações. */
{
  page = 0;
}

void creditosLayout(void)   /* Função para construir a página de créditos. */
{
  layout.setSwapBytes(false);
  layout.fillScreen(BLACK);

  //layout.fillRect(41, 118, 49, 15, WHITE);   // Retangulos do rodapé Autor
  //layout.fillRect(43, 119, 45, 13, BLACK);

  //layout.fillRect(21, 118, 88, 15, WHITE);   // Retangulos do rodapé Orientador
  //layout.fillRect(23, 119, 84, 13, BLACK);

  //layout.fillRect(41, 118, 49, 15, WHITE);   // Retangulos do rodapé Apoio
  //layout.fillRect(43, 119, 45, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  //layout.drawFastHLine(4, 125, 36, WHITE);  // Autor
  //layout.drawFastHLine(92, 125, 34, WHITE);
  //layout.drawFastHLine(4, 125, 17, WHITE);    // Orientador
  //layout.drawFastHLine(109, 125, 16, WHITE);
  //layout.drawFastHLine(4, 125, 36, WHITE);    // Apoio
  //layout.drawFastHLine(92, 125, 34, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  //layout.drawCentreString("AUTOR", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);
  //layout.drawCentreString("ORIENTADOR", TFT_MIDDLE_COLUM, TFT_LOGO_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("APOIO", TFT_MIDDLE_COLUM, TFT_LOGO_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Alexandre", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Mendes Marcelo", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Pedro Augusto", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Queiroz de Assis", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Pedro M Vinicius R", TFT_MIDDLE_COLUM, TFT_NAME_LINE, TFT_MAIN_FONT);
  //layout.drawCentreString("Heitor S Rafael F", TFT_MIDDLE_COLUM, TFT_LAST_NAME_LINE, TFT_MAIN_FONT);

  //layout.setSwapBytes(true);
  //layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE - 1, 112, 76, Autor_112_76);
  //layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE - 1, 112, 76, Orientador_112_76);
  //layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE - 1, 112, 76, Apoio_112_76);

  //layout.setPivot(131, 133);
  //layout.pushRotated(180);

  special_page = true;
  i = 0;
}

void ensaioManualLayout(void)
{
  layout.fillScreen(BLACK);
  layout.fillRect(10, 10, 112, 30, WHITE);   // Retangulos do cabeçalho
  layout.fillRect(20, 15, 92, 20, BLACK);

  layout.fillRect(48, 118, 34, 15, WHITE);   // Retangulos do rodapé
  layout.fillRect(50, 119, 30, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  layout.drawFastHLine(4, 125, 44, WHITE);
  layout.drawFastHLine(82, 125, 43, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  layout.drawCentreString("UFU", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);
  layout.drawCentreString("MANUAL", TFT_MIDDLE_COLUM, TFT_HEADER_LINE, TFT_MAIN_FONT);
  layout.drawString("Iniciar", TFT_OPTIONS_COLUM, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString("Vref[mm/s]=", TFT_OPTIONS_COLUM, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString(String(velocity_reference), 100, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);

  /* Trocar as imagens para as novas opções. */
  layout.setSwapBytes(true);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE, 16, 16, Play_16);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_SECOND_IMAGE_LINE, 16, 16, Velocity_16);

  layout.pushSprite(0, 0);
}

void ensaioAutomaticoLayout(void)
{
  page = 0; 
  mainLayout();           /* Ainda sem funcionalidade, apenas retorna à página principal. */
  page_change = true;
}

void manualInicioLayout(void)
{
  layout.fillScreen(BLACK);
  layout.fillRect(10, 10, 112, 30, WHITE);   // Retangulos do cabeçalho
  layout.fillRect(20, 15, 92, 20, BLACK);

  layout.fillRect(48, 118, 34, 15, WHITE);   // Retangulos do rodapé
  layout.fillRect(50, 119, 30, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  layout.drawFastHLine(4, 125, 44, WHITE);
  layout.drawFastHLine(82, 125, 43, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  layout.drawCentreString("UFU", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);
  layout.drawCentreString("INICIAR", TFT_MIDDLE_COLUM, TFT_HEADER_LINE, TFT_MAIN_FONT);
  layout.drawString("Vref[mm/s]=", TFT_IMAGE_COLUM, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString(String(velocity_reference), 83, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString("Sentido:", TFT_IMAGE_COLUM, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString(head_sense, 63, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);

  layout.pushSprite(0, 0);

  special_page = true;  /* É preciso ativar special_page para que as funcionalidades de seleção não interfiram na exibição desta. */
  track = true;
}

void manualVrefLayout(void)
{
  layout.fillScreen(BLACK);
  layout.fillRect(10, 10, 112, 30, WHITE);   // Retangulos do cabeçalho
  layout.fillRect(20, 15, 92, 20, BLACK);

  layout.fillRect(48, 118, 34, 15, WHITE);   // Retangulos do rodapé
  layout.fillRect(50, 119, 30, 13, BLACK);

  layout.drawFastHLine(10, 35, 112, BLACK);  // Linhas de estilo do cabeçalho
  layout.drawFastHLine(10, 15, 112, BLACK);
  layout.drawFastVLine(20, 10, 30, BLACK);
  layout.drawFastVLine(112, 10, 30, BLACK);

  layout.drawFastHLine(4, 5, 121, WHITE);    // Linhas de contorno
  layout.drawFastVLine(4, 5, 120, WHITE);
  layout.drawFastVLine(127, 5, 120, WHITE);
  layout.drawFastHLine(4, 125, 44, WHITE);
  layout.drawFastHLine(82, 125, 43, WHITE);

  layout.fillCircle(4, 5, 2, WHITE);         // Circulos de estilo
  layout.fillCircle(127, 5, 2, WHITE);
  layout.fillCircle(4, 125, 2, WHITE);
  layout.fillCircle(127, 125, 2 ,WHITE);
  layout.drawCentreString("UFU", TFT_MIDDLE_COLUM, TFT_LOGO_LINE,  TFT_MAIN_FONT);
  layout.drawCentreString("MANUAL", TFT_MIDDLE_COLUM, TFT_HEADER_LINE, TFT_MAIN_FONT);
  layout.drawString("Iniciar", TFT_OPTIONS_COLUM, TFT_FIRST_OPTION_LINE, TFT_MAIN_FONT);
  layout.setTextColor(BLACK, WHITE);
  layout.drawString("Vref[mm/s]=", TFT_OPTIONS_COLUM, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);
  layout.drawString(String(velocity_reference), 100, TFT_SECOND_OPTION_LINE, TFT_MAIN_FONT);
  layout.setTextColor(WHITE, BLACK);

  /* Trocar as imagens para as novas opções. */
  layout.setSwapBytes(true);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_FIRST_IMAGE_LINE, 16, 16, Play_16);
  layout.pushImage(TFT_IMAGE_COLUM, TFT_SECOND_IMAGE_LINE, 16, 16, Velocity_16);

  layout.pushSprite(0, 0);

  special_page = true;  /* É preciso ativar special_page para que as funcionalidades de seleção não interfiram na exibição desta. */
}

int pageDecoder(void)   /* O decodificador de página é responsável por analisar a localização atual de página e opção para */
{                       /* indicar a próxima página a ser carregada baseado nas escolhas (botões pressionados) do usuário. */
  uint8_t page_decoded = 0;
  // A página depende da em seleção no momento da confirmação.
  if(page_increment)     /* Quando em páginas normais, o decodificador pode incrementar ou decrementar o número das páginas */
  {                     /* a depender do botão pressionado. */
    switch (page)       /* pageDecoder deve ser refinado para operações de incremento/decremento de página. */
    {
    case 0: /* Lógica para identificar o número da próxima página. */
      page_decoded = page + option;
      break;

    case 1: /* Lógica para identificar o número da próxima página estando na página 1. */
      page_decoded = page + option + 2; /* Direciona para as páginas 4 e 5 */
      break;

    case 2: /* Lógica para identificar o número da próxima página estando na página 2. */
      page_decoded = page + option + 5; /* Direciona para a página  */
      break;
    
    case 4:
      page_decoded = page + option + 1; /* Direciona para as páginas 8 e 9 */
      break;

    default:  /* Em condições estranhas, resetar o menu. */
      page_decoded = 0;
      break;
    }
  }
  else
  {
    switch (page) /* Caso o usuário esteja em uma página especial, o decodificador pode apenas retroceder páginas. */
    {
    case 3:
      page_decoded = 0;
      special_page = false;
      credits = 1;
      i = 0;
      thisNote = 0;
      ledcWrite(LED_CHANNEL, 0);
      vTaskSuspend(xHandlerCredits);
      //esp_ipc_call(PRO_CPU_NUM, *(esp_ipc_func_t)PROMenager, NULL);
      break;

    case 4: /* Aqui está escrito para retornar à página inicial do menu ao sair de iniciar ensaio, mas caso outra */
            /* opção seja mais interessante, basta implementá-la. */
      page_decoded = 1;             // Essa lógica somente será acessada a partir do clique do botão "return".
      break;

    case 5: /* A página de ensaio automático é uma página especial. Seu retorno volta à página de ensaios. */
      page_decoded = 1;
      track = false;        /* Ao sair de uma página especial de ensaio, é importante desativar o rastreamento. */
      special_page = false; /* Ao sair de uma página especial, é importante desativar seu status. */
      break;
    
    case 6:
      page_decoded = 4;
      track = false;
      tracking = false;
      special_page = false;
      break;
    
    case 7:
      page_decoded = 4;
      special_page = false;
      break;

    default:  /* Em condições estranhas, resetar o menu. */
      page_decoded = 0;
      break;
    }
  }

  page_change = true;
  option = 1;

  return page_decoded;  /* A função retorna um valor, mas isso é desnecessário quando ela está dentro do próprio */
}                       /* código que a declara e que contém a variável manipulada. */
                        /* Ao reescrever o código de forma mais organizada, é importante reestruturar as funções. */
                        
double velocityCalculation(uint16_t pulses, unsigned long velocity_timer, bool counter_clockwise) // Função para calcular a velocidade de rotação [rad/s].
{
  // A equação para calcular a velocidade é um pouco diferente dependendo do sentido de giro.
  // Isso é necessário por causa da forma de contagem de pulsos da biblioteca usada: de min_value a max_value, ordem crescente, começando em min_value;
  //                                                                                 de max_value a min_value, ordem decrescente, começando em max_value;
  // Ou seja, não há contagem negativa de pulsos ao inverter o sentido de rotação. Ao invés disso, os valores decrescem a partir do max_value definido.

  double velocity_calculation;
  //Serial.print("Pulses in function: ");
  //Serial.println(pulses);

  if(pulses == 0) // Como a contagem de pulsos sempre estará em 0 após o resete, é indispensável checar se ela continua em zero após o loop para definir a
                  // velocidade diretamente como 0. Do contrário pode haver uma descontinuidade na leitura dos pulsos caso esteja no sentido horário.
                  // Em outras palavras, os pulsos podem passar de 0 para 990 após 10 pulsos no sentido horário, gerando um cálculo de velocidade impróprio.
  {
    velocity_calculation = 0.0;
  }
  else if(counter_clockwise)
  {
    //velocity_calculation = 1.7453*pulses/(millis() - velocity_timer);
    velocity_calculation = 1.2566*pulses/(velocity_timer);
  }
  else if(!counter_clockwise)
  {
    velocity_calculation = 1.2566*(uint16_t(MAX_VALUE) - pulses)/(velocity_timer); // Isso não funciona. Quando parado o cálculo da velocidade entende que os pulsos estão em max_value.
  }
  
  return velocity_calculation;
}

void change_vref(void)
{
  // Escrever um salvamento da nova velocidade de referência em memória EEPROM (talvez).
  page_increment = false;
  page = pageDecoder();
}

// // /* A lógica para avançar/retroceder entre as páginas depende da quantidade de opções que existem dentro de cada página.
// // O número da próxima página depende da página em que o usuário se encontra e da opção em seleção.

// // A primeira página é a 0 e nela temos 3 opções: ensaio(1), configurações(2) e créditos(3).
// // A próxima página carregada será a soma da página atual (0) com a opção selecionada (1, 2 ou 3).
// // Supondo que a opção Ensaio fosse selecionada, a próxima página seria: 0 + 1 = 1.
// // Assim, a página de número 1 equivale à página Ensaio.

// // Estando na página 1, temos 2 opção para selecionar: iniciar(1) e perfil(2).
// // Para que não exista coincidência de valores a partir da lógica anterior, é preciso somar também um índice diferenciador.
// // i.e: page = 1 e option = 1 seria next_page = 2 e page = 0 e option = 2 também seria next_page = 2. Isso é uma coincidência.
// // O índice é o número de opções de todas as páginas anteriores.
// // i.e: na page = 1, o índice é 3, pois as páginas anteriores (0) possuem 3 opções ao todo, enquanto o índice da page = 6 é 5,
// // pois as páginas anteriores (0 e 1) possuem 3 + 2 opções possíveis.
// // */