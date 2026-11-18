# YouTube Vita / Cobalt 25 Evergreen — status de inicialização (17/11/2026)

## Confirmado em Vita3K (título experimental isolado YTVCB0001)

- ELF oficial Cobalt Evergreen 5.50.2 (Starboard 15), sem modificações persistentes do binário original.
- Todos os 74 construtores estáticos C++ executam.
- 174 de 174 verificações Starboard passam no Vita3K (zero erros).
- Corrigido kSbFileAltSepChar de NUL para barra invertida: normalizador Cobalt gerava laço por busca do terminador da string. Teste de regressão criado.
- SbEventHandle(Start) real é chamado pela ponte de lifecycle, atravessando configuração, cache e watchdog.
- Estrutura Starboard Start agora oferece argc=1 e argv[0] válido.
- 144 arquivos do CRX oficial correspondentes ao ELF incluídos no VPK experimental: 140 certificados SSL, 3 splash HTML, fonts.xml.
- pthread_create retornou sucesso para 12 threads; suas 12 funções de entrada foram executadas.
- Janela fullscreen criada e interfaces GLES e EGL consultadas.
- eglGetDisplay retornou 0x3, eglInitialize sucesso, eglCreateWindowSurface retornou 0x2 e eglCreateContext retornou 0x1.
- O Start do Cobalt ainda não retornou; não foi observada chamada a eglMakeCurrent.
- Sem chamada a SbSystemRequestStop no intervalo dos testes.
- SbLogRaw otimizado para inicializar diretório apenas uma vez; reduz avisos repetidos sceIoMkdir.
- Teste automatizado do Vita3K recebeu tolerância a lock temporário de arquivo e startup mais lento.

## Bloqueio

O construtor principal Cobalt em offset ELF 0x12F54E0, invocado pela inicialização em SbEventHandle(Start) próxima de 0x819454, continua bloqueando o retorno. É necessário identificar a espera/dependência depois da criação de EGLContext e antes da primeira eglMakeCurrent.

O GDB stub foi conectado, mas não forneceu uma stack confiável porque a sessão ficou parada antes de iniciar o aplicativo.

## Limitações

Ainda não existe renderização DOM/YouTube TV, login, busca, streaming ou reprodução de vídeos nesta build. O VPK de testes YTVCB0001 não substitui o protótipo anterior YTVP00001. Não instalar como aplicativo funcional no PS Vita físico.

## Próxima etapa

Rastrear as condições de espera, locks e callbacks de threads no construtor após eglCreateContext; em seguida conseguir o retorno real de SbEventHandle(Start) e o pump de eventos. Validar fontes de tradução oficiais, sem criar arquivos artificiais que ocultem problemas.
