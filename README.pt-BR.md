<p align="center"><img src="docs/jangada.gif" alt="Jangada" width="720"></p>

<p align="center">
<a href="README.md">English</a> · <b>Português</b><br>
<a href="https://github.com/zednaked/jangada/actions/workflows/ci.yml"><img src="https://github.com/zednaked/jangada/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<img src="https://img.shields.io/badge/licen%C3%A7a-GPL--3.0-ff14aa" alt="GPL-3.0">
<img src="https://img.shields.io/badge/M--VAVE-FM--1-ff14aa" alt="M-VAVE FM-1">
</p>

# Jangada 🛶

**Firmware alternativo para o M-VAVE FM-1, com sotaque escuro, industrial e brasileiro.**
Drones que respiram sozinhos, ferrugem, máquinas e manguebeat num synth de bolso baratinho: dez motores
de síntese (com FM de 6 operadores), um analógico com superwave, matriz de modulação, ratchets e quatro
trilhas. Um fork do [Felucca](https://github.com/hugelton/Felucca) de Leo Kuroshita (Hügelton
Instruments). A felucca é o barco à vela do Nilo; a jangada é a nossa.

## Filosofia

A Jangada não quer ser um compêndio dos outros firmwares do FM-1. Ela tem um gosto:

- **Escuro e industrial.** Para quem pensa em Nine Inch Nails, *The Downward Spiral*, *Ghosts I–IV*,
  trilhas de cinema: sons com ferrugem, grão, fita gasta, zumbido de fundo, pianos quebrados.
- **Drones como linguagem.** Um acorde que se sustenta e respira sozinho enquanto você toca o resto é
  a ideia mais nossa, e vai crescer: drones que evoluem, tensão que abre devagar.
- **Brasileiro.** Jangada é nome de barco do Nordeste. O **manguebeat** (Chico Science & Nação Zumbi)
  já juntava maracatu com peso industrial: é esse o nosso cruzamento. Kits de alfaia, zabumba e agogô,
  padrões de maracatu, baião e coco, escalas nordestinas.
- **Feito para tocar ao vivo.** Segurar um botão e mudar tudo, efeitos que entram com um dedo, nada
  que obrigue a olhar a tela.

Trazemos o que há de bom nos outros forks (Felucca, SLOOP, Melodee) quando serve a esse gosto, sempre
com o nome e a cara da Jangada e com os créditos de quem fez. O que é só mais do mesmo fica de fora.

> **Alfa.** Use por sua conta e risco. A área de boot do FM-1 nunca é tocada, e dá para voltar ao
> firmware oficial a qualquer momento.

## Ouça

Gerados pelo próprio DSP do firmware (o mesmo código C, rodando num PC):

| Escuros / industriais | Drones | Superwave |
|---|---|---|
| [RUST BASS](docs/sounds/rust-bass.mp3) | [DRONE SAW](docs/sounds/drone-saw.mp3) | [SUPER SAW](docs/sounds/super-saw.mp3) |
| [HURT PAD](docs/sounds/hurt-pad.mp3) | [DRONE RING](docs/sounds/drone-ring.mp3) | [SUPER PAD](docs/sounds/super-pad.mp3) |
| [GRIND LEAD](docs/sounds/grind-lead.mp3) | [DRONE FM](docs/sounds/drone-fm.mp3) | [HP SHIMMER](docs/sounds/hp-shimmer.mp3) |
| [MACHINE](docs/sounds/machine.mp3) | [DRONE DUST](docs/sounds/drone-dust.mp3) | [quatro trilhas juntas](docs/sounds/four-tracks.mp3) |
| [METAL HIT](docs/sounds/metal-hit.mp3) | [DRONE VOX](docs/sounds/drone-vox.mp3) | |
| [BROKEN BELL](docs/sounds/broken-bell.mp3) · [STATIC](docs/sounds/static.mp3) | [DRONE ORGAN](docs/sounds/drone-organ.mp3) | |
| [QUIET KEYS](docs/sounds/quiet-keys.mp3) · [BROKEN KEY](docs/sounds/broken-key.mp3) | | |
| [GHOST KEYS](docs/sounds/ghost-keys.mp3) · [DIRTY ORGAN](docs/sounds/dirty-organ.mp3) | | |

## Instalar

Versão atual: **[Jangada 0.4](https://github.com/zednaked/jangada/releases/tag/v0.4)** (alfa).
Conecte o FM-1 direto no computador por um cabo USB **de dados**.

**Mac / Windows / Linux, pelo navegador**: abra o
**[instalador web da Jangada](https://zednaked.github.io/jangada/)** no Chrome ou no Edge e aperte
*Instalar*. Ele já traz a última versão; ao lado fica o editor web.

**Linux, pelo terminal**: clone este repositório e rode

```
./instalar-linux.sh                    # baixa e instala a última versão publicada
./instalar-linux.sh jangada-0.4.fwsc   # instala um arquivo baixado das releases
./instalar-linux.sh --original         # volta ao firmware oficial da M-VAVE (V15)
./instalar-linux.sh --info             # o que o FM-1 está rodando
./instalar-linux.sh --console          # acesso ao console serial (regra udev, pede sudo)
```

O script prepara sozinho um ambiente Python (`mido` + `python-rtmidi`) em `~/.local/share/jangada/`
e confere o SHA-256 do que baixou.

- **Se a instalação falhar**: segure **OCT−** ao ligar o FM-1 (resgate por USB) e instale de novo.
- **Voltar ao oficial**: `./instalar-linux.sh --original`, ou o M-UPGRADE da M-VAVE.
- Desde a 0.2 o FM-1 aparece no computador como **Jangada** (MIDI e áudio). O instalador web do
  Felucca não acha mais um FM-1 com a Jangada: use o da Jangada.

## O que muda em relação ao Felucca

### Performance: segure um botão
Toque um botão de função e as páginas dele abrem, como sempre. **Segure** e ele vira uma
**camada**: as 16 teclas brancas e os 4 knobs mudam de função enquanto ele está apertado, e a tela
mostra as teclas como 16 tiles (4 × 4) e os knobs como dials. **HOME** tocado com a camada segurada
a **trava** aberta (as duas mãos livres); qualquer outro botão a solta. PLAY, REC e OCT continuam
valendo dentro dela. Ideia e boa parte do código do [SLOOP](https://github.com/isod89/sloop-fm1).

| Segure | Teclas | Knobs 1 · 2 · 3 · 4 |
|---|---|---|
| **FX**: punch | 16 efeitos no mix inteiro enquanto a tecla está apertada: loops 1/4 a 1/32, stutter, reverse, tape stop, half, LP / HP sweep, phone, crush, alias, gate, echo, wobble | FILT · DUST · DUCK |
| **GLO**: mix | 1–4 mute, 5–8 solo, a última tap tempo | nível das trilhas 1–4 |
| **SEQ**: passos | os 16 passos da página: vazio = cria com a última nota, cheio = apertar e soltar apaga. Pretas: F# G# A# C# = página; D#4 / F#4 desloca, G#4 / A#4 metade / dobro, C#5 / D#5 transpõe, **F#5 segurada apaga** o que o playhead passa | NOTE · DIV · SWG · LEN; com passos segurados: NOTE · RTCH · CHNC · FLAG |
| **SCL**: tom | qualquer tecla = o tom da música (todas as trilhas) | CHRD · SCL · QNT · TRN |
| **EDIT**: motor | 1–9 = o motor da trilha; a última = trilha 4 DRUM / SYNTH | PRST · VOICE · GLIDE · LVL |

Com **SEQ** segurado, **OCT− / OCT+** = undo / redo do padrão.

### Master
**GLO → MASTER** (e os knobs da camada FX): **DUST** (sampler velho e disco: bits, taxa, chiado
enquanto toca), **DUCK** (o bumbo abaixa os synths por uma colcheia), **FILT** (filtro de DJ:
esquerda passa-baixa, direita passa-alta).

### Acordes de uma tecla
**SCL → CHORD** (ou o knob 1 da camada SCL): OFF, TRIAD, 7TH, 9TH, SUS4, POWER. Ligado, as teclas
brancas andam pela escala a partir do C4 e cada uma toca o acorde da escala inteiro (gravado como
acorde no passo). A trilha passa a POLY sozinha.

### TRACKS
O **REC** numa página sem nada para gravar abre a tela de trilhas: BPM, compasso.beat, uma linha
por trilha com o som, o motor, os passos e o playhead, o nível e os selos REC / SOLO / MUTE.

### Som
- **ANALOG turbinado** (EDIT 3 / 4): **SUPR** superwave (até 6 cópias desafinadas do oscilador),
  **SDTN** abertura, **SUB** quadrada uma oitava abaixo, **DRFT** desafinação lenta por voz,
  **FTYP** filtro LP12 / LP24 / BP / HP. Com muitas vozes o superwave usa menos cópias, para caber
  na CPU (8 vozes de SUPER SAW: 55 % no FM-1).
- **Matriz de modulação**: botão LFO → páginas **MOD 1–4**. Cada slot: origem (LFO, ENV, VEL, KEY,
  RND) → destino (filtro, pitch, forma ou qualquer parâmetro do motor) × quantidade.
- **16 parâmetros por motor** (o Felucca tem 8).
- **20 presets novos**: texturas escuras e industriais, superwaves e seis drones.
- **Hi-hats e crash** do kit GM tocam a própria amostra (soavam como toms).
- **FM6**: FM de 6 operadores (o núcleo msfa do Dexed, portado pelo Felucca 1.0), 32 algoritmos,
  8 patches de fábrica (PTCH F1–F8) e macros nos knobs (ALG FB MLVL MRAT MEG VMOD DTUN). O DIGITAL de
  4 operadores continua.
- **Bateria sintetizada** (do SLOOP): **GLO → KIT**, ou o knob PRESETS na trilha de bateria: o kit GM
  sampleado ou 37 kits sintetizados (808, 909, TECHNO, INDUSTR, GLITCH, DUBSTEP, JUNGLE…).
- **Kits da Jangada**, os primeiros da lista: **RUST** (industrial seco: bumbo distorcido, caixa com
  gate, hats esmagados), **FORGE** (bigorna, correntes, chapas: metal que soa), **PISTON** (máquinas:
  estalos, vapor, válvulas), **HURT** (baixo, abafado, respirado) e **MANGUE** (alfaia, zabumba e
  bacalhau, caixa de maracatu, gonguê, agogô, cuíca, ganzá, triângulo, palmas, com o peso do mangue).
- **Batidas de fábrica** (**GLO → KIT**, knob 4 **BEAT**): MARACATU (baque virado), BAIAO, COCO,
  GRIND, ANVIL, ENGINE, FRAGILE. Num kit da Jangada com a trilha de bateria vazia, a batida dele já
  vem junto; sobre um padrão seu, o BEAT pede uma segunda volta do knob.
- **Nordeste**: a escala **NORD** (SCL; mixolídio com a 4ª aumentada, o modo nordestino: use NORD ou
  MIX no baião, DOR no xote e na toada), e os presets **BAIAO BASS** e **RABECA** (ANALOG) e
  **SANFONA** (WHEEL), estes dois em drone.
- **Reverbs** (**FX → REVERB**, TYPE): ROOM (a de sempre), SPRING (mola, do Felucca 1.0) e PLATE
  (rede de atrasos estéreo, do SLOOP).

### MIDI
- **Entrada MIDI TRS** (o conector de 3,5 mm do FM-1): um teclado MIDI toca como pelo USB. Canais
  1–3 tocam as trilhas de synth, o 4 a trilha 4 quando ela é SYNTH, o canal de bateria (GLO → DRUMS,
  padrão 10) a bateria, os outros a trilha selecionada.
- **Pitch bend** (±2 semitons), **sustain** (CC64), **all notes off** (CC120 / 123), reset (CC121).
- **Mod wheel**, **aftertouch** e **expressão** (CC11) como origens da matriz (MODW, AT, EXPR).
- **GLO → GLOBAL CLK USB** ou **TRS**: segue o clock MIDI do computador ou do conector TRS, pulso a
  pulso (24 por batida, sem deriva): os passos, o BPM, o punch FX, o DUCK e a tela TRACKS acompanham;
  START recomeça do início, CONTINUE retoma de onde parou, STOP para; sem pulso por 0,5 s volta ao
  relógio interno.
- **GLO → SYSTEM SYNC OUT**: manda clock pelo USB (24 por batida, start, stop); nunca enquanto segue
  um clock (CLK USB ou TRS).

### Áudio por USB
O FM-1 aparece no computador como uma entrada de áudio estéreo (44,1 kHz, "Jangada"), sem driver:
grave o master direto na DAW (HOME → USB AUDIO: o nível segue o MASTER, ou FULL) (no Linux: `arecord -D hw:Jangada -f S16_LE -r 44100 -c 2 take.wav`).

### Editor e instalador web
No **[site da Jangada](https://zednaked.github.io/jangada/)** (Chrome ou Edge):
- **Backup e restauração** (editor → Projects → Backup): tudo do FM-1 num arquivo
  `jangada-backup-DATA.json` (o projeto de trabalho, os 4 projetos, os 32 presets, os samples
  USR1–3, as configurações), e de volta. Um arquivo danificado é recusado antes de qualquer gravação.
- **CHOP** (editor → Samples): corte uma gravação de qualquer tamanho em até 16 fatias, escolha as que
  ficam, encurte, *Fit to slot*, e mande para USR1–3.
- **Voltar ao firmware oficial** pelo instalador (o arquivo FM-1 V15 da M-VAVE), com um backup antes.

### Memória e segurança
- **Autosave**: parado e sem mexer por alguns segundos, o projeto vai para a flash e volta ao ligar.
  **OCT+** segurado ao ligar começa vazio; **HOME → NEW PROJECT** zera tudo.
- **Atualização mais segura** (do SLOOP): o instalador recusa pacote danificado; o loader confere o
  CRC antes de liberar o firmware novo.
- **Resgate por USB**: **OCT−** segurado ao ligar (ou dois boots que falham) abre JANGADA USB RESCUE,
  onde só o instalador roda. A calibração do painel: **OCT− + OCT+** ao ligar.
- Menu (segure **HOME**): COLOR, SPEAKER (corta graves para o alto-falante), LIGHTS, KEYS, NOTES, USB AUDIO,
  NEW PROJECT, ABOUT. KNOB 1 muda o valor da linha.
- **Luzes para o escuro**: **LIGHTS** OFF / LOW / MID / HIGH faz todos os botões brilharem fraco (dá para
  ler os rótulos no escuro; os acesos continuam cheios); **KEYS** acende também as teclas C ou todas as
  brancas; **NOTES** acende a tecla de cada nota que soa (sequencer, MIDI, bateria), em toda página e
  camada. O brilho fraco é um pulso curto a cada varredura: não pisca. Ficam salvos com as
  configurações do aparelho, não no projeto.
- **USB AUDIO**: MASTER (a gravação segue o knob MASTER) ou FULL (nível fixo, como o MASTER no máximo).

### Drones
Os presets DRONE usam o arpejador em **RPT** a cada **4 compassos** com **HOLD**: toque um acorde,
solte, e ele continua respirando sozinho, inclusive enquanto você toca outras trilhas.
- **Segure ARP** → DRONE OFF: solta os acordes presos (saem com o release do preset).
- **Segure ARP de novo** → SILENCE: as caudas param na hora.

Para sequenciar um drone: arp OFF, PATTERN com **DIV 4BAR**, um acorde por passo (cada passo dura
4 compassos).

**Drones que evoluem.** Toque ARP até a página **DRONE** (HOLD, EVOL, TENS, RAMP), por trilha:
- **EVOL**: enquanto a trilha soa, passeios aleatórios lentos e suaves movem o som sozinhos (ciclos de
  dezenas de segundos a minutos, nunca iguais): o filtro, a forma, o parâmetro natural do motor (o MIX
  do superwave, o índice do FM, o tamanho do grão…), e cada voz desafina e respira do seu jeito. Para
  quando o drone para; um drone novo começa do som do preset.
- **TENS**: a tensão. Abre o filtro, sobe ressonância / drive / brilho e o DIST da trilha, separa as
  vozes na afinação (até ±12 cents) e deixa o passeio mais fundo e mais rápido. Em 0, o som de hoje.
- **RAMP**: OFF, 1 a **32BAR**: a tensão viaja até TENS nesse número de compassos (no tempo). Um drone
  que nasce do silêncio começa em 0 e constrói; baixar TENS volta com a mesma calma.
- A tela mostra os quatro passeios e a barra da tensão subindo até a marca de TENS.
- Na matriz, a origem **DRIFT** é o passeio da trilha, para qualquer destino.
- Presets: **FERRUGEM** (ANALOG, superwave), **ABISMO** (DIGITAL, FM), **SERTAO** (WHEEL, a sanfona
  que azeda) e **CINZA** (GRAIN, cinza de piano).

### Arpejador e sequencer
- Modos **UDI** (sobe e desce repetindo as pontas) e **RPT** (o acorde inteiro a cada passo);
  divisões **1/2, 1/1, 2BAR, 4BAR** (também no sequencer).
- Página **STEP 2**: **RTCH** ratchet x1–x4 e **CHNC** chance 100/75/50/25 % por passo.

### Trilhas
- **Trilha 4: DRUM ou SYNTH.** Em **TRACKS**, escolha a trilha 4 com o ALGORITHM e gire o
  **knob 1 (TYPE)**: SYNTH a transforma numa quarta parte de synth (motor, preset, arp, sequencer,
  MIDI canal 4); DRUM volta ao kit GM.

### Tela e memória
- Paleta **CHOQUE** (rosa-choque) como padrão; as outras continuam no menu (segure HOME → COLOR).
- Projetos (**JNG1**) e presets de usuário (**UPB2**) guardam cada valor com uma **chave
  estável**: parâmetros podem ser acrescentados ou movidos sem perder o que foi salvo. Projetos e
  presets do Felucca são lidos e convertidos.

## Ferramentas

| | |
|---|---|
| `tools/fm1_console.py status` | CPU, áudio, USB, bateria |
| `tools/fm1_console.py check` | teste no aparelho: pico de CPU, atrasos de áudio, reinícios |
| `tools/fm1_console.py voices` | o que soa em cada trilha, e por quê |
| `tools/fm1_console.py preset E I [T]` | carrega o preset I do motor E na trilha T |
| `tools/fm1_console.py t4 synth\|drum` | tipo da trilha 4 |
| `tools/fm1_console.py droneoff` | como segurar ARP |
| `tools/fm1_console.py color CHOQUE` | paleta da tela |
| `tools/fm1_console.py g ID [VALOR]` | lê ou muda um parâmetro global (ex.: `g 28 90` = DUST) |
| `tools/fm1_console.py punch N\|off` | liga um efeito punch (0–15) ou desliga |

## Compilar e testar

Veja [BUILDING.md](BUILDING.md). No Linux x86-64 o toolchain da JieLi roda nativo, sem Docker:

```
tools/get_toolchain.sh        # o toolchain, em ~/.jieli
tools/get_sdk_files.sh        # só os 3 arquivos do SDK AC79 que o pacote usa
./build.sh                    # build/felucca.fwsc
sh tests/run_tests.sh         # todos os testes, no PC
```

- **Build reprodutível**: a data vem do último commit; dois builds dão os mesmos bytes.
- Os testes cobrem o som (renders de todos os presets com impressão digital), saúde (clipping, DC,
  notas presas), orçamento de CPU (contador de instruções no Linux e no Mac), formatos, arp, steps,
  a matriz, a trilha 4, o instalador e o editor web, cujas tabelas do simulador são geradas a
  partir do firmware (`tools/gen_editor_tables.py`).
- **CI** a cada push; uma tag `vX.Y[-sufixo]` publica o `.fwsc` numa release.

## Próximos passos

1. ~~Grit no master~~ (feito: TAPE, HUM, FUZZ / FOLD / CRUSH / RING).
2. ~~Kits próprios~~ (feito: RUST, FORGE, PISTON, HURT, MANGUE; maracatu, baião, coco; escala NORD).
3. ~~O Studio no navegador~~ (feito: `webapp/studio/` no site).
4. **Banco de patches FM6**, importação e exportação de `.syx` do DX7, e o Dexed editando a Jangada ao
   vivo por SysEx.
5. MIDI mais fino (bend por canal, vibrato no CC1) e o nome do acorde no HOME.

Depois: ~~drones que evoluem~~ (feito: EVOL, TENS, RAMP, DRIFT), pianos quebrados, paletas RUST, ASH e MANGUE, a jangada na tela de boot.

## Créditos e licença

A Jangada é GPL-3.0-only, como o Felucca. As camadas, o punch FX, o master e os acordes vêm do
[SLOOP](https://github.com/isod89/sloop-fm1) (GPL-3.0), outro fork do Felucca. O trabalho original é de **Leo Kuroshita (@kurogedelic),
Hügelton Instruments**: veja [README.felucca.md](README.felucca.md) e [LICENSING.md](LICENSING.md)
para os créditos completos (fontes, amostras, motores).

M-VAVE e FM-1 são marcas de seus donos. A Jangada não é afiliada nem endossada por eles, nem pelo
Felucca.
