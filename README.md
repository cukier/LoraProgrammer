# LoraProgrammer

Firmware ESP32 (ESP-IDF) para programar e configurar módulos de rádio LoRa, expondo a configuração através de um servidor HTTP em Wi-Fi (SoftAP/STA), acessível também via mDNS (`http://<hostname>.local/`).

Dois rádios são suportados; apenas um é compilado por build, escolhido no `menuconfig`:

| Rádio | Interface | Como funciona |
|---|---|---|
| **RF1276 / YL_800IL** — [AppConWireless](https://www.appconwireless.com/) (*padrão*) | UART | Módulo com MCU próprio. O ESP32 lê/escreve os parâmetros (baudrate, paridade, frequência, spreading factor, modo, largura de banda, ID, Net ID, potência) pelo protocolo de comandos com header `0xAF`; os dados de payload são transparentes (bytes crus na mesma UART). |
| **LoRa127X-C1 / SX1276** — [NiceRF](https://www.nicerf.com/) | SPI nativa | Modem SX1276 controlado diretamente pelo ESP32 (componente `dernasherbrezon/sx127x`). Não tem memória de configuração: "programar" significa aplicar os parâmetros (frequência, largura de banda, SF, coding rate, sync word, preâmbulo, potência, CRC) a cada boot e em runtime via `POST /lora`. |

### Selecionar o rádio

```bash
cd src
idf.py menuconfig   # → "KLORA Configuration" → "LoRa radio module"
idf.py build
```

Sem `menuconfig`, o build usa o RF1276 (`CONFIG_KLORA_RADIO_RF1276=y`). Os pinos
(UART ou SPI) e os parâmetros de rádio ficam no mesmo menu, e são aplicados no
boot. A escolha do rádio também está exposta como variável de CMake, então o
`main/CMakeLists.txt` compila só o back end selecionado (`kuart.c` + `RF1216.c`
ou `klora_sx127x.c`).

### API HTTP (igual para os dois rádios)

| Método | Rota | Descrição |
|---|---|---|
| `GET`  | `/info`      | RAM livre, versão do firmware, IP, uptime, hora local |
| `GET`  | `/lora`      | Configuração atual do rádio (JSON) |
| `POST` | `/lora`      | Aplica/grava parâmetros do rádio (corpo JSON, qualquer subconjunto) |
| `GET`  | `/lora/rxtx` | Hex dump do último pacote recebido pelo ar |
| `POST` | `/lora/rxtx` | Transmite um payload — corpo JSON `{"message": "..."}` |
| `POST` | `/ota`       | Atualização de firmware |
| `POST` | `/reboot`    | Reinicia o dispositivo após 5 s |

O formato do JSON de `/lora` difere entre os rádios (parâmetros distintos), mas a
saída do `GET` sempre volta a valer como corpo do `POST` para o mesmo back end.

## Estrutura do repositório

```
src/        Firmware ESP-IDF (projeto CMake)
  main/     Componente principal (código-fonte)
sch/        Projeto de hardware em KiCad
.github/    Workflows de CI (build do firmware e exports do KiCad)
```

## Firmware

Projeto ESP-IDF padrão. Para compilar:

```bash
cd src
idf.py build
idf.py -p <PORT> flash monitor
```

O CI (`.github/workflows/firmware.yml`) compila o firmware a cada push/PR em `src/**` e publica os binários (`.bin`, `bootloader.bin`, `partition-table.bin`) como artifact.

## Hardware

Esquemático e PCB em `sch/` (KiCad 9). O CI (`.github/workflows/hardware.yml`) exporta Gerbers, drill files e BOM a cada push/PR em `sch/**`.

## Configuração

`radio.json` documenta o formato dos parâmetros de rádio (back end RF1276) expostos pela API HTTP do firmware.

## Licença

MIT — veja [LICENSE](LICENSE).
