# Testes de regressão — YouTube Vita / Cobalt

**Data:** 17/11/2026 no PC-Wolff (Vita3K 0.2.1 build 4242).

| Teste | Resultado | Limite do que comprova |
|---|---|---|
| StarboardVitaTest / YTVSB0001 | **137 PASS, 0 FAIL** | API Starboard parcial, não o browser |
| CobaltEvergreenRelocations / YTVRL0001 | **24 PASS, 0 FAIL** | 159/211 referências ELF vinculadas; **não executou libcobalt.so** |
| YouTubeVitaAVPlayerTest / YTVAP0001 | **6 PASS, 0 FAIL** | 60 frames YUV válidos e 59 hashes distintos em MP4 local |
| YouTubeVitaH264Probe / YTVVD0001 | **3 PASS, 0 FAIL** | decoder H264 iniciado, framebuffer 3 MiB |
| YouTubeVitaH264FrameTest / YTVDF0001 | **14 PASS, 1 FAIL** | API reporta quadro de saída, mas os pixels permanecem iguais a 0x44 |
| YouTubeVitaSoftwareH264Test / YTVSW0001 | **4 PASS, 1 FAIL** | VitaSDK libavcodec atual não expõe decoder H264 |
| StarboardVitaTcpTest | 15 PASS, 0 FAIL | TCP loopback real |
| StarboardVitaEpollTest | 13 PASS, 0 FAIL | event callback de pacote UDP |
| YouTubeVitaHttpsProbe | HTTP 200 no YouTube TV | apenas download de HTML, não navegador |
| YouTubeVitaGraphicsTest | 19 PASS, 0 FAIL | 40 quadros GXM por ponte EGL/Starboard |

## Evidência do H264 SceAvPlayer

```text
AVPLAYER_INIT 0x81800020
AVPLAYER_ADD_SOURCE 0x00000000
FIRST_FRAME_WIDTH 0x000003C0 (960)
FIRST_FRAME_HEIGHT 0x00000220 (544)
FRAME_COUNT 0x0000003C (60)
MAX_FRAME_NONCONSTANT_SAMPLES 0x00001C6B (7275)
CHANGING_FRAME_HASHES 0x0000003B (59)
GPU_ALLOCATIONS_AFTER 0x00000000 (0)
RESULT PASS (6 passed, 0 failed)
```

O `SceAvPlayerHandle` é um identificador de 32 bits que pode parecer negativo quando tratado como `int`. Foi corrigido pelo teste de intervalo `0x81000000..0x9FFFFFFF`, alinhado em 4 bytes, conforme o port de vídeo já presente no projeto Deltarune Vita.

### Logs brutos no Vita3K

`ux0:data/youtubevita/starboard15.log`, `relocations.log`, `avplayer.log`, `decode_frame.log`, `software_decoder.log`, `https.log` e `graphics.log`.

Os logs acima são execuções reais com VPK instalado; não são prints de uma interface YouTube.

