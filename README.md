# LoraProgrammer

Firmware ESP32 (ESP-IDF) para programar e configurar módulos de rádio LoRa **RF1276**, da [AppConWireless](https://www.appconwireless.com/), via UART.

O ESP32 conversa com o módulo RF1276 por UART (leitura/escrita dos parâmetros de rádio: baudrate, paridade, frequência, spreading factor, modo, largura de banda, ID, Net ID e potência) e expõe essa configuração através de um servidor HTTP em Wi-Fi (SoftAP/STA), acessível também via mDNS (`http://<hostname>.local/`).

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

`radio.json` documenta o formato dos parâmetros de rádio expostos pela API HTTP do firmware.

## Licença

MIT — veja [LICENSE](LICENSE).
