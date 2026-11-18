# Etapas técnicas — YouTube Vita oficial (1:1)

1. **Completar o carregador Cobalt:** 159/211 referências externas de realocação vinculadas; restam **52** (incluindo funções reais de áudio, SbPlayer, decodificação, UI/sistema). O carregador se recusa a executar com as referências faltantes.
2. **Implementar a ponte real SbPlayer:** integrar amostras codificadas de Cobalt a um pipeline aceito pelo decodificador Sony. `SceAvPlayer` demonstrou 60 quadros H264 YUV reais no Vita3K; ainda opera a partir de arquivos MP4, não da API `SbPlayerWriteSamples`.
3. **Renderizar vídeo GXM:** criar textura YUV/GXM e apresentar os quadros nativos, com sincronização PTS, áudio e buffering.
4. **Inicializar Cobalt de fato:** eventos Starboard, plataforma ABI, construtores ELF, V8/Skia. A ponte EGL/Starboard renderiza 40 quadros diagnósticos, mas o DOM/JS do YouTube TV ainda não roda.
5. **Abrir o YouTube on TV:** `https://www.youtube.com/tv` já devolveu HTTP 200, mas baixar HTML não executa a interface. Validar funcionamento no serviço, login e disponibilidade para dispositivo não certificado.
6. **Hardware PS Vita:** requisitos de RAM, performance, vídeo/áudio, teclado, D-pad, touch, plugins opcionais e testes reais.
7. **Critério 1:1:** reproduzir a interface oficial do serviço, navegar e pesquisar com controle, tocar vídeos reais sem simulação. Só então anunciar um VPK de aplicativo e produzir uma captura GIF autêntica.

Os VPKs atuais são exclusivamente diagnósticos locais. Não substituir a UI por um clone desenhado manualmente.

