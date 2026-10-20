#include <Servo.h>

// Controle serial seguro para dois bracos e uma boca.
// Ao ligar, nenhum servo recebe pulsos de controle.

constexpr unsigned long SERIAL_BAUD = 9600;
constexpr unsigned long INTERVALO_PASSO_MS = 20;
constexpr int PASSO_SUAVE_US = 5;

// Faixa inicial conservadora. Ajustar somente depois da calibracao mecanica.
constexpr int PULSO_MINIMO_US = 1200;
constexpr int PULSO_MAXIMO_US = 1800;
constexpr int PULSO_INICIAL_MIN_US = 1400;
constexpr int PULSO_INICIAL_MAX_US = 1600;
constexpr int ALTERACAO_MAXIMA_US = 100;

// Teste temporizado para servo de rotacao continua/modificado.
// A boca nunca inicia sozinha: a sequencia exige o comando TESTAR_BOCA.
constexpr int BOCA_NEUTRO_US = 1500;
constexpr int BOCA_ABRIR_US = 1580;
constexpr int BOCA_FECHAR_US = 1420;
constexpr unsigned long BOCA_MOVIMENTO_MS = 2000;
constexpr unsigned long BOCA_PAUSA_MS = 300;
constexpr unsigned long BOCA_ASSENTAR_MS = 150;
constexpr unsigned long MOVIMENTO_TEMPORIZADO_MS = 2000;

// Coreografia conservadora: um minuto, com desligamento automatico.
constexpr unsigned long DURACAO_BRACOS_DANCA_MS = 25000;
constexpr unsigned long DURACAO_APRESENTACAO_MS = 30000;
constexpr unsigned long INTERVALO_BRACOS_DANCA_MS = 1000;
constexpr int BRACO_CENTRO_US = 1500;
constexpr int BRACO_AMPLITUDE_DANCA_US = 120;
// Os servos dos bracos estao montados espelhados.
constexpr int BRACO_ESQUERDO_CIMA_US = BRACO_CENTRO_US + BRACO_AMPLITUDE_DANCA_US;
constexpr int BRACO_ESQUERDO_BAIXO_US = BRACO_CENTRO_US - BRACO_AMPLITUDE_DANCA_US;
constexpr int BRACO_DIREITO_CIMA_US = BRACO_CENTRO_US - BRACO_AMPLITUDE_DANCA_US;
constexpr int BRACO_DIREITO_BAIXO_US = BRACO_CENTRO_US + BRACO_AMPLITUDE_DANCA_US;
// Amplitude maior durante a danca; TESTAR_BOCA preserva seus limites proprios.
constexpr int BOCA_DANCA_ABERTA_US = 1750;
constexpr int BOCA_DANCA_FECHADA_US = 1250;
constexpr unsigned long BOCA_DANCA_SUBIDA_MS = 600;
constexpr unsigned long BOCA_DANCA_DESCIDA_MS = 600;
constexpr unsigned long BOCA_DANCA_PAUSA_CURTA_MS = 0;
constexpr unsigned long BOCA_DANCA_PAUSA_LONGA_MS = 0;

constexpr int PINO_BOCA = 7;
constexpr int PINO_DIREITO = 8;
constexpr int PINO_ESQUERDO = 9;
constexpr int PINO_NAO_CONFIGURADO = -1;

Servo servoEsquerdo;
Servo servoDireito;
Servo servoBoca;

struct Atuador {
  const char *nome;
  char id;
  Servo *servo;
  int pino;
  int pulsoAtualUs;
  int pulsoAlvoUs;
  bool ativo;
};

Atuador atuadores[] = {
  {"ESQUERDO", 'E', &servoEsquerdo, PINO_ESQUERDO, 1500, 1500, false},
  {"DIREITO",  'D', &servoDireito,  PINO_DIREITO,  1500, 1500, false},
  {"BOCA",     'B', &servoBoca,     PINO_BOCA,     1500, 1500, false}
};

constexpr size_t QUANTIDADE_ATUADORES = sizeof(atuadores) / sizeof(atuadores[0]);
unsigned long ultimoPassoMs = 0;
bool desligamentoAgendado[QUANTIDADE_ATUADORES] = {false, false, false};
unsigned long inicioMovimentoMs[QUANTIDADE_ATUADORES] = {0, 0, 0};
bool dancaAtiva = false;
bool somenteBocaDanca = false;
bool faseBracosDanca = false;
uint8_t etapaBocaDanca = 0;
unsigned long inicioDancaMs = 0;
unsigned long ultimaTrocaBracosMs = 0;
unsigned long ultimaTrocaBocaMs = 0;

enum EstadoProgramaBoca {
  BOCA_PARADA,
  BOCA_ABRINDO,
  BOCA_PAUSA_ABERTA,
  BOCA_FECHANDO,
  BOCA_FINALIZANDO
};

EstadoProgramaBoca estadoProgramaBoca = BOCA_PARADA;
unsigned long inicioEtapaBocaMs = 0;

Atuador *buscarAtuador(char id) {
  if (id >= 'a' && id <= 'z') {
    id -= ('a' - 'A');
  }

  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    if (atuadores[i].id == id) {
      return &atuadores[i];
    }
  }
  return nullptr;
}

bool pinoPermitido(int pino) {
  if (pino < 2 || pino > 13) {
    return false;
  }

  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    if (atuadores[i].pino == pino) {
      return false;
    }
  }
  return true;
}

void manterPinoBaixo(int pino) {
  if (pino >= 2 && pino <= 13) {
    pinMode(pino, OUTPUT);
    digitalWrite(pino, LOW);
  }
}

void mostrarAjuda() {
  Serial.println(F("Comandos:"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  PINO B <2..13>"));
  Serial.println(F("  ATIVAR <E|D|B> <1400..1600>"));
  Serial.println(F("  MOVER <E|D|B> <1400..1600>  (2 s e desliga)"));
  Serial.println(F("  MOVER_DIREITO <1400..1600>  (2 s e desliga)"));
  Serial.println(F("  MOVER_ESQUERDO <1400..1600>  (2 s e desliga)"));
  Serial.println(F("  MOVER_BRACOS <esquerdo> <direito>  (2 s e desliga)"));
  Serial.println(F("  MOVER_TUDO <esquerdo> <direito> <boca>  (2 s e desliga)"));
  Serial.println(F("  TODOS <esquerdo> <direito> <boca>  (2 s e desliga)"));
  Serial.println(F("  DESATIVAR <E|D|B>"));
  Serial.println(F("  TESTAR_BOCA  (movimento curto, retorno e desligamento)"));
  Serial.println(F("  DANCAR  (25 s completos + 5 s somente boca)"));
  Serial.println(F("  PARAR"));
  Serial.println(F("  AJUDA"));
}

void mostrarStatus() {
  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    Atuador &a = atuadores[i];
    Serial.print(a.nome);
    Serial.print(F(": pino="));
    Serial.print(a.pino);
    Serial.print(F(" estado="));
    Serial.print(a.ativo ? F("ATIVO") : F("INATIVO"));
    Serial.print(F(" atual="));
    Serial.print(a.pulsoAtualUs);
    Serial.print(F(" alvo="));
    Serial.println(a.pulsoAlvoUs);
  }
}

void configurarPinoBoca(int pino) {
  Atuador &boca = atuadores[2];

  if (boca.ativo) {
    Serial.println(F("ERRO: desative a boca antes de trocar o pino."));
    return;
  }
  if (!pinoPermitido(pino)) {
    Serial.println(F("ERRO: pino invalido, reservado ou ja utilizado."));
    return;
  }

  boca.pino = pino;
  manterPinoBaixo(boca.pino);
  Serial.print(F("OK: boca configurada no pino "));
  Serial.println(boca.pino);
}

void ativarAtuador(Atuador &a, int pulsoInicialUs) {
  if (a.pino == PINO_NAO_CONFIGURADO) {
    Serial.println(F("ERRO: configure o pino deste atuador primeiro."));
    return;
  }
  if (pulsoInicialUs < PULSO_INICIAL_MIN_US || pulsoInicialUs > PULSO_INICIAL_MAX_US) {
    Serial.println(F("ERRO: pulso inicial deve estar entre 1400 e 1600 us."));
    return;
  }
  if (a.ativo) {
    Serial.println(F("ERRO: atuador ja esta ativo."));
    return;
  }

  a.pulsoAtualUs = pulsoInicialUs;
  a.pulsoAlvoUs = pulsoInicialUs;
  a.servo->writeMicroseconds(pulsoInicialUs);
  a.servo->attach(a.pino, PULSO_MINIMO_US, PULSO_MAXIMO_US);
  a.servo->writeMicroseconds(pulsoInicialUs);
  a.ativo = true;

  Serial.print(F("OK: ativado "));
  Serial.println(a.nome);
}

void desativarAtuador(Atuador &a) {
  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    if (&atuadores[i] == &a) {
      desligamentoAgendado[i] = false;
      break;
    }
  }
  if (a.servo->attached()) {
    a.servo->detach();
  }
  a.ativo = false;
  manterPinoBaixo(a.pino);

  Serial.print(F("OK: desativado "));
  Serial.println(a.nome);
}

void iniciarProgramaBoca() {
  Atuador &boca = atuadores[2];

  if (estadoProgramaBoca != BOCA_PARADA || boca.ativo) {
    Serial.println(F("ERRO: boca ja esta ativa."));
    return;
  }

  boca.pulsoAtualUs = BOCA_ABRIR_US;
  boca.pulsoAlvoUs = BOCA_ABRIR_US;
  boca.servo->attach(boca.pino, PULSO_MINIMO_US, PULSO_MAXIMO_US);
  boca.servo->writeMicroseconds(BOCA_ABRIR_US);
  boca.ativo = true;
  estadoProgramaBoca = BOCA_ABRINDO;
  inicioEtapaBocaMs = millis();
  Serial.println(F("OK: teste temporizado da boca iniciado."));
}

void atualizarProgramaBoca() {
  if (estadoProgramaBoca == BOCA_PARADA) {
    return;
  }

  Atuador &boca = atuadores[2];
  unsigned long decorrido = millis() - inicioEtapaBocaMs;

  if (estadoProgramaBoca == BOCA_ABRINDO && decorrido >= BOCA_MOVIMENTO_MS) {
    boca.servo->writeMicroseconds(BOCA_NEUTRO_US);
    boca.pulsoAtualUs = BOCA_NEUTRO_US;
    boca.pulsoAlvoUs = BOCA_NEUTRO_US;
    estadoProgramaBoca = BOCA_PAUSA_ABERTA;
    inicioEtapaBocaMs = millis();
  } else if (estadoProgramaBoca == BOCA_PAUSA_ABERTA && decorrido >= BOCA_PAUSA_MS) {
    boca.servo->writeMicroseconds(BOCA_FECHAR_US);
    boca.pulsoAtualUs = BOCA_FECHAR_US;
    boca.pulsoAlvoUs = BOCA_FECHAR_US;
    estadoProgramaBoca = BOCA_FECHANDO;
    inicioEtapaBocaMs = millis();
  } else if (estadoProgramaBoca == BOCA_FECHANDO && decorrido >= BOCA_MOVIMENTO_MS) {
    boca.servo->writeMicroseconds(BOCA_NEUTRO_US);
    boca.pulsoAtualUs = BOCA_NEUTRO_US;
    boca.pulsoAlvoUs = BOCA_NEUTRO_US;
    estadoProgramaBoca = BOCA_FINALIZANDO;
    inicioEtapaBocaMs = millis();
  } else if (estadoProgramaBoca == BOCA_FINALIZANDO && decorrido >= BOCA_ASSENTAR_MS) {
    desativarAtuador(boca);
    estadoProgramaBoca = BOCA_PARADA;
    Serial.println(F("OK: teste da boca concluido; sinal desligado."));
  }
}

bool alvoValido(const Atuador &a, int novoAlvoUs) {
  if (!a.ativo) {
    Serial.println(F("ERRO: ative o atuador primeiro."));
    return false;
  }
  if (novoAlvoUs < PULSO_MINIMO_US || novoAlvoUs > PULSO_MAXIMO_US) {
    Serial.println(F("ERRO: alvo deve estar entre 1200 e 1800 us."));
    return false;
  }

  int diferenca = novoAlvoUs - a.pulsoAlvoUs;
  if (diferenca > ALTERACAO_MAXIMA_US || diferenca < -ALTERACAO_MAXIMA_US) {
    Serial.println(F("ERRO: altere no maximo 100 us por comando."));
    return false;
  }
  return true;
}

void definirAlvo(Atuador &a, int novoAlvoUs) {
  if (!alvoValido(a, novoAlvoUs)) {
    return;
  }
  a.pulsoAlvoUs = novoAlvoUs;
  Serial.print(F("OK: novo alvo de "));
  Serial.print(a.nome);
  Serial.print(F(" = "));
  Serial.println(a.pulsoAlvoUs);
}

bool pulsoTemporizadoValido(int pulsoUs) {
  if (pulsoUs < PULSO_INICIAL_MIN_US || pulsoUs > PULSO_INICIAL_MAX_US) {
    Serial.println(F("ERRO: movimento temporizado deve estar entre 1400 e 1600 us."));
    return false;
  }
  return true;
}

void iniciarMovimentoTemporizado(size_t indice, int pulsoUs) {
  Atuador &a = atuadores[indice];

  if (a.servo->attached()) {
    a.servo->detach();
  }
  a.servo->attach(a.pino, PULSO_MINIMO_US, PULSO_MAXIMO_US);
  a.servo->writeMicroseconds(pulsoUs);
  a.pulsoAtualUs = pulsoUs;
  a.pulsoAlvoUs = pulsoUs;
  a.ativo = true;
  desligamentoAgendado[indice] = true;
  inicioMovimentoMs[indice] = millis();
}

void atualizarMovimentosTemporizados() {
  unsigned long agora = millis();

  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    if (!desligamentoAgendado[i] ||
        agora - inicioMovimentoMs[i] < MOVIMENTO_TEMPORIZADO_MS) {
      continue;
    }

    desativarAtuador(atuadores[i]);
    Serial.print(F("OK: movimento temporizado concluido em "));
    Serial.println(atuadores[i].nome);
  }
}

void iniciarDanca() {
  estadoProgramaBoca = BOCA_PARADA;
  unsigned long agora = millis();

  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    desligamentoAgendado[i] = false;
    Atuador &a = atuadores[i];
    if (a.servo->attached()) {
      a.servo->detach();
    }
    a.pulsoAtualUs = BOCA_NEUTRO_US;
    a.pulsoAlvoUs = BOCA_NEUTRO_US;
    a.servo->attach(a.pino, PULSO_MINIMO_US, PULSO_MAXIMO_US);
    a.servo->writeMicroseconds(BOCA_NEUTRO_US);
    a.ativo = true;
  }

  atuadores[0].pulsoAlvoUs = BRACO_ESQUERDO_CIMA_US;
  atuadores[1].pulsoAlvoUs = BRACO_DIREITO_BAIXO_US;
  atuadores[2].servo->writeMicroseconds(BOCA_DANCA_ABERTA_US);
  atuadores[2].pulsoAtualUs = BOCA_DANCA_ABERTA_US;
  atuadores[2].pulsoAlvoUs = BOCA_DANCA_ABERTA_US;

  faseBracosDanca = false;
  etapaBocaDanca = 0;
  inicioDancaMs = agora;
  ultimaTrocaBracosMs = agora;
  ultimaTrocaBocaMs = agora;
  dancaAtiva = true;
  somenteBocaDanca = false;
  Serial.println(F("OK: apresentacao iniciada por 30 segundos. Use PARAR para interromper."));
}

void atualizarDanca() {
  if (!dancaAtiva) {
    return;
  }

  unsigned long agora = millis();
  unsigned long tempoApresentacaoMs = agora - inicioDancaMs;
  if (tempoApresentacaoMs >= DURACAO_APRESENTACAO_MS) {
    dancaAtiva = false;
    pararTodos();
    Serial.println(F("OK: apresentacao concluida; todos os sinais desligados."));
    return;
  }

  if (!somenteBocaDanca && tempoApresentacaoMs >= DURACAO_BRACOS_DANCA_MS) {
    desativarAtuador(atuadores[0]);
    desativarAtuador(atuadores[1]);
    somenteBocaDanca = true;
    Serial.println(F("OK: etapa final; somente a boca pelos ultimos 5 segundos."));
  }

  if (!somenteBocaDanca &&
      agora - ultimaTrocaBracosMs >= INTERVALO_BRACOS_DANCA_MS) {
    faseBracosDanca = !faseBracosDanca;
    atuadores[0].pulsoAlvoUs = faseBracosDanca
      ? BRACO_ESQUERDO_BAIXO_US
      : BRACO_ESQUERDO_CIMA_US;
    atuadores[1].pulsoAlvoUs = faseBracosDanca
      ? BRACO_DIREITO_CIMA_US
      : BRACO_DIREITO_BAIXO_US;
    ultimaTrocaBracosMs = agora;
  }

  unsigned long duracaoEtapaBoca = BOCA_DANCA_SUBIDA_MS;
  if (etapaBocaDanca == 1) {
    duracaoEtapaBoca = BOCA_DANCA_PAUSA_CURTA_MS;
  } else if (etapaBocaDanca == 2) {
    duracaoEtapaBoca = BOCA_DANCA_DESCIDA_MS;
  } else if (etapaBocaDanca == 3) {
    duracaoEtapaBoca = BOCA_DANCA_PAUSA_LONGA_MS;
  }

  if (agora - ultimaTrocaBocaMs >= duracaoEtapaBoca) {
    etapaBocaDanca = (etapaBocaDanca + 1) % 4;

    int pulsoBocaUs = BOCA_NEUTRO_US;
    if (etapaBocaDanca == 0) {
      pulsoBocaUs = BOCA_DANCA_ABERTA_US;
    } else if (etapaBocaDanca == 2) {
      pulsoBocaUs = BOCA_DANCA_FECHADA_US;
    }

    atuadores[2].servo->writeMicroseconds(pulsoBocaUs);
    atuadores[2].pulsoAtualUs = pulsoBocaUs;
    atuadores[2].pulsoAlvoUs = pulsoBocaUs;
    ultimaTrocaBocaMs = agora;
  }
}

void pararTodos() {
  dancaAtiva = false;
  somenteBocaDanca = false;
  estadoProgramaBoca = BOCA_PARADA;
  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    desligamentoAgendado[i] = false;
    Atuador &a = atuadores[i];
    if (a.servo->attached()) {
      a.servo->detach();
    }
    a.ativo = false;
    manterPinoBaixo(a.pino);
  }
  Serial.println(F("OK: todos os sinais foram desligados."));
}

void moverSuavemente() {
  unsigned long agora = millis();
  if (agora - ultimoPassoMs < INTERVALO_PASSO_MS) {
    return;
  }
  ultimoPassoMs = agora;

  for (size_t i = 0; i < QUANTIDADE_ATUADORES; ++i) {
    Atuador &a = atuadores[i];
    if (!a.ativo || a.pulsoAtualUs == a.pulsoAlvoUs) {
      continue;
    }

    if (a.pulsoAtualUs < a.pulsoAlvoUs) {
      a.pulsoAtualUs += min(PASSO_SUAVE_US, a.pulsoAlvoUs - a.pulsoAtualUs);
    } else {
      a.pulsoAtualUs -= min(PASSO_SUAVE_US, a.pulsoAtualUs - a.pulsoAlvoUs);
    }
    a.servo->writeMicroseconds(a.pulsoAtualUs);
  }
}

void processarComando(char *linha) {
  char id = '\0';
  int valor1 = -1;
  int valor2 = -1;
  int valor3 = -1;

  if (dancaAtiva &&
      strcmp(linha, "PARAR") != 0 &&
      strcmp(linha, "STATUS") != 0 &&
      strcmp(linha, "AJUDA") != 0) {
    Serial.println(F("ERRO: danca em andamento. Use PARAR antes de outro comando."));
    return;
  }

  if (strcmp(linha, "AJUDA") == 0) {
    mostrarAjuda();
  } else if (strcmp(linha, "STATUS") == 0) {
    mostrarStatus();
  } else if (strcmp(linha, "PARAR") == 0) {
    pararTodos();
  } else if (strcmp(linha, "DANCAR") == 0) {
    iniciarDanca();
  } else if (strcmp(linha, "TESTAR_BOCA") == 0) {
    iniciarProgramaBoca();
  } else if (sscanf(linha, "PINO %c %d", &id, &valor1) == 2 && (id == 'B' || id == 'b')) {
    configurarPinoBoca(valor1);
  } else if (sscanf(linha, "ATIVAR %c %d", &id, &valor1) == 2) {
    Atuador *a = buscarAtuador(id);
    if (a != nullptr) {
      ativarAtuador(*a, valor1);
    } else {
      Serial.println(F("ERRO: atuador deve ser E, D ou B."));
    }
  } else if (sscanf(linha, "MOVER_DIREITO %d", &valor1) == 1) {
    if (pulsoTemporizadoValido(valor1)) {
      iniciarMovimentoTemporizado(1, valor1);
      Serial.println(F("OK: braco direito ativo por 2 segundos."));
    }
  } else if (sscanf(linha, "MOVER_ESQUERDO %d", &valor1) == 1) {
    if (pulsoTemporizadoValido(valor1)) {
      iniciarMovimentoTemporizado(0, valor1);
      Serial.println(F("OK: braco esquerdo ativo por 2 segundos."));
    }
  } else if (sscanf(linha, "MOVER_BRACOS %d %d", &valor1, &valor2) == 2) {
    if (pulsoTemporizadoValido(valor1) && pulsoTemporizadoValido(valor2)) {
      iniciarMovimentoTemporizado(0, valor1);
      iniciarMovimentoTemporizado(1, valor2);
      Serial.println(F("OK: dois bracos ativos por 2 segundos."));
    }
  } else if (sscanf(linha, "MOVER_TUDO %d %d %d", &valor1, &valor2, &valor3) == 3) {
    if (pulsoTemporizadoValido(valor1) &&
        pulsoTemporizadoValido(valor2) &&
        pulsoTemporizadoValido(valor3)) {
      iniciarMovimentoTemporizado(0, valor1);
      iniciarMovimentoTemporizado(1, valor2);
      iniciarMovimentoTemporizado(2, valor3);
      Serial.println(F("OK: boca e bracos ativos por 2 segundos."));
    }
  } else if (sscanf(linha, "MOVER %c %d", &id, &valor1) == 2) {
    Atuador *a = buscarAtuador(id);
    if (a != nullptr && pulsoTemporizadoValido(valor1)) {
      size_t indice = static_cast<size_t>(a - atuadores);
      iniciarMovimentoTemporizado(indice, valor1);
      Serial.println(F("OK: atuador ativo por 2 segundos."));
    } else {
      if (a == nullptr) {
        Serial.println(F("ERRO: atuador deve ser E, D ou B."));
      }
    }
  } else if (sscanf(linha, "TODOS %d %d %d", &valor1, &valor2, &valor3) == 3) {
    if (pulsoTemporizadoValido(valor1) &&
        pulsoTemporizadoValido(valor2) &&
        pulsoTemporizadoValido(valor3)) {
      iniciarMovimentoTemporizado(0, valor1);
      iniciarMovimentoTemporizado(1, valor2);
      iniciarMovimentoTemporizado(2, valor3);
      Serial.println(F("OK: tres atuadores ativos por 2 segundos."));
    }
  } else if (sscanf(linha, "DESATIVAR %c", &id) == 1) {
    Atuador *a = buscarAtuador(id);
    if (a != nullptr) {
      if (a->id == 'B') {
        estadoProgramaBoca = BOCA_PARADA;
      }
      desativarAtuador(*a);
    } else {
      Serial.println(F("ERRO: atuador deve ser E, D ou B."));
    }
  } else {
    Serial.println(F("ERRO: comando desconhecido. Digite AJUDA."));
  }
}

void lerSerial() {
  static char linha[64];
  static size_t tamanho = 0;

  while (Serial.available() > 0) {
    char caractere = static_cast<char>(Serial.read());

    if (caractere == '\r') {
      continue;
    }
    if (caractere == '\n') {
      linha[tamanho] = '\0';
      if (tamanho > 0) {
        processarComando(linha);
      }
      tamanho = 0;
      continue;
    }
    if (tamanho < sizeof(linha) - 1) {
      linha[tamanho++] = caractere;
    }
  }
}

void setup() {
  // Impede sinais flutuantes nos tres pinos conhecidos.
  manterPinoBaixo(PINO_BOCA);
  manterPinoBaixo(PINO_ESQUERDO);
  manterPinoBaixo(PINO_DIREITO);

  Serial.begin(SERIAL_BAUD);
  Serial.println();
  Serial.println(F("BRACO_ROBO_CONTROLE v2"));
  Serial.println(F("Todos os atuadores iniciaram desativados."));
  mostrarAjuda();
}

void loop() {
  lerSerial();
  atualizarProgramaBoca();
  atualizarMovimentosTemporizados();
  atualizarDanca();
  moverSuavemente();
}
