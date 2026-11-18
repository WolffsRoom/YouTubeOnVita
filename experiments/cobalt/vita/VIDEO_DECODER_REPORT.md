# Diagnóstico de vídeo do YouTube Vita (17/11/2026)

## Conteúdo sintético

- `assets/h264_test.h264`: 1 IDR H264 Constrained Baseline 960×544, 28.843 bytes, criado localmente com FFmpeg.
- `assets/vita_avplayer_test.mp4`: H264 Constrained Baseline level 3.1, 960×544 30 FPS, 3,0 s, 90 quadros, ~1,56 MB, `testsrc2` original, sem áudio ou conteúdo do YouTube.
- O MP4 foi reprodutível no FFmpeg do PC, e o quadro bruto produziu checksum MD5 de vídeo `684f1ebea50951ec139aebd63784d05a`.

## Passou — SceAvPlayer

A API de vídeo da Sony utilizada em outros ports (por exemplo Deltarune Vita) aceitou a fonte MP4 `app0:` e decodificou 60 frames YUV em 960×544, 59 hashes sucessivos diferentes, 7275 amostras com luminância variada e nenhum buffer de GPU pendente ao finalizar. VPK: `build-starboard15/YouTubeVitaAVPlayerTest.vpk`, TitleID `YTVAP0001`. Regressão `tools/test_avplayer_vita3k.ps1`: **6/6 PASS**.

O vídeo não foi exibido na tela nesse diagnóstico; a prova é leitura direta e hash da memória YUV de imagens reais.

## Não passou — SceAvcdecDecode direto

O decoder `sceAvcdecCreateDecoder` inicializa e aloca 3 MiB PHYCONT para 960×544. O `sceAvcdecDecode` retorna 0 e `numOfOutput=1`, mas o buffer RGBA fornecido permanece preenchido com o byte sentinela 0x44. As duas modalidades PHYCONT_RW e PHYCONT_NC_RW apresentaram o mesmo problema. Endereço de saída não foi alterado pela API. VPK: `YTVDF0001`. **14 PASS, 1 FAIL** (a falha correta é a verificação dos pixels).

Sem validação do buffer, não declarar SceAvcdecDecode funcional. O motivo ainda não foi isolado: emulação, parâmetros de saída ou código H264.

## Não passou — FFmpeg software VitaSDK

A biblioteca `libavcodec.a` está no VitaSDK, mas `avcodec_find_decoder(AV_CODEC_ID_H264)` retorna nulo nessa compilação: **4 PASS, 1 FAIL**. O VPK opcional YTVSW0001 é apenas diagnóstico; `YTV_BUILD_SW_H264_PROBE` está OFF por padrão.

## Próximo passo para SbPlayer

O Cobalt consome amostras codificadas pela API `SbPlayerWriteSamples`, enquanto o `SceAvPlayer` demonstrado aqui consome contêiner MP4 pelo caminho de arquivo. **Essa diferença de contrato ainda precisa de uma adaptação real** (demuxer/inserção de amostras e sincronismo PTS), além do caminho YUV→GXM e áudio. Não declarar capacidade H264 ao Cobalt até o `SbPlayer` estar funcional.

A certificação do YouTube TV, o frontend JS/HTML e a autenticação continuam não validados. Nenhuma interface fake deve substituir esse trabalho.

