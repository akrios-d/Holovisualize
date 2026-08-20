# myo-control

Controla o mouse e o teclado do PC usando um bracelete **Myo** (Thalmic
Labs), treinado a partir de gravações reais de você usando o mouse e o
teclado com o braço direito.

Fluxo:

1. **`record`** — você usa o PC normalmente (cliques, teclas, mover o
   mouse) enquanto o Myo grava EMG (atividade muscular) + IMU (orientação
   do antebraço) em paralelo.
2. **`train`** — a partir das gravações, treina:
   - um classificador (RandomForest) que reconhece qual gesto muscular
     corresponde a qual ação (clique esquerdo, clique direito, teclas);
   - uma regressão (Ridge) que mapeia a rotação do antebraço (giroscópio)
     para o deslocamento do cursor.
3. **`control`** — roda os dois modelos ao vivo: os gestos disparam
   cliques/teclas, e a rotação do braço move o cursor — sem precisar
   tocar no mouse.

## Por que não usa o Myo Connect / SDK oficial?

A Thalmic Labs encerrou as atividades em 2018. O **Myo Connect** (driver
oficial) exige ativação de conta online contra servidores que não existem
mais — instalações novas hoje em dia normalmente falham na ativação.

Este projeto usa [`pyomyo`](https://github.com/PerlinWarp/pyomyo), uma
biblioteca Python de código aberto que conversa diretamente com o dongle
Bluetooth (protocolo BLED112/BGAPI) sem precisar do Myo Connect, SDK ou
conta — só o dongle plugado na USB.

> `myo_control/dongle.py` é a única peça que depende da API do `pyomyo`.
> Se a API da lib mudar entre versões, é o único arquivo a ajustar.

## Setup (Windows)

```bat
cd myo-control
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

1. Plugue o dongle Bluetooth da Myo na USB.
2. Descubra a porta COM dele:
   ```bat
   python -m myo_control list-ports
   ```
   (o dongle Bluegiga BLED112 costuma aparecer como "Bluegiga Bluetooth
   Low Energy" no Gerenciador de Dispositivos — se houver mais de uma
   porta COM na lista, teste até achar a certa)
3. Vista o bracelete no antebraço direito, com os LEDs voltados pra mão.

## Uso

### 1. Gravar dados de treino

```bat
python -m myo_control record --name cliques_basicos --port COM5
```

Enquanto grava, faça **uma ação de cada vez, em blocos**, controlando o
mouse/teclado normalmente com o braço direito:

- ~20-30 cliques esquerdos (com uma pausa de descanso entre eles)
- ~20-30 cliques direitos
- pressione espaço e enter algumas vezes
- movimente o cursor livremente por um tempo (isso treina o mapeamento de
  cursor via giroscópio)
- para o gesto de **clutch** (liga/desliga o controle de cursor — ver
  abaixo), segure a tecla `m` sempre que fizer o gesto escolhido

Repita `record` várias vezes (sessões curtas e consistentes valem mais que
uma sessão longa e cansada) — todas ficam salvas em `data/sessions/`.

Pare com `Ctrl+C`.

### 2. Treinar os modelos

```bat
python -m myo_control train
```

Usa todas as sessões em `data/sessions/` por padrão (ou `--sessions nome1
nome2` pra escolher). Salva os modelos em `models/`.

### 3. Controlar o PC sem mouse

```bat
python -m myo_control control --port COM5
```

O controle de cursor começa **desligado** — faça o gesto de "clutch"
(o que você marcou com a tecla `m` durante a gravação) pra ligar, gesto de
novo pra desligar. Isso evita que qualquer movimento do braço fique
arrastando o cursor o tempo todo, do mesmo jeito que levantar um mouse
físico da mesa para de mover o cursor.

**Recomendado:** na primeira vez, teste em uma janela sem risco (bloco de
notas) antes de confiar no controle em algo importante.

## Gestos/ações padrão

Configurável em `myo_control_config.json` (gerado a partir de
`myo_control/config.py` na primeira execução). Padrão:

| label         | ação              | detalhe (o que dispara durante o `record`) |
|---------------|-------------------|---------------------------------------------|
| `click_left`  | clique esquerdo   | clique esquerdo real do mouse                |
| `click_right` | clique direito    | clique direito real do mouse                 |
| `key_space`   | tecla espaço      | tecla espaço real                            |
| `key_enter`   | tecla enter       | tecla enter real                             |
| `clutch`      | liga/desliga cursor | tecla `m` segurada durante o gesto         |
| `rest`        | nenhuma (implícito) | amostras longe de qualquer gesto acima     |

## Estrutura

```
myo_control/
├── dongle.py     # wrapper sobre pyomyo (única dependência de hardware)
├── session.py    # gravação/leitura de sessões em JSONL
├── record.py     # grava EMG/IMU + eventos de mouse/teclado em paralelo
├── features.py   # extração de features EMG por janela deslizante
├── labeling.py   # alinha eventos gravados com janelas de sensor -> dataset
├── train.py      # treina classificador de gestos + regressão de cursor
├── control.py    # loop ao vivo: sensor -> modelo -> ação real no PC
├── config.py     # bindings gesto->ação, sample rates, thresholds
└── cli.py        # `python -m myo_control <record|train|control|list-ports>`
```

## Testes

```bash
pip install pytest
pytest
```

Cobrem extração de features e o alinhamento de rótulos (`labeling.py`)
com dados sintéticos — não dependem do hardware.

## Limitações conhecidas / próximos passos

- O mapeamento de cursor é um MVP (regressão linear giro->delta); pode
  ficar impreciso em movimentos rápidos ou combinados — ajustar
  `cursor_gain_xy` / `cursor_deadzone_dps` em `config.py` ajuda.
- O classificador de gestos não faz o que travas de segurança fariam
  (ex: nunca vai clicar em algo destrutivo sozinho) — teste em ambiente
  controlado antes de usar em tarefas críticas.
- `pyomyo` é uma dependência de terceiros que evoluiu por reverse
  engineering da comunidade; se a conexão falhar, confira primeiro se o
  dongle aparece em `list-ports` e se o firmware do bracelete está
  emparelhado com aquele dongle específico (a Myo grava o pareamento no
  dongle, então um dongle "genérico" pode não conectar sem antes ser
  pareado ao menos uma vez).
