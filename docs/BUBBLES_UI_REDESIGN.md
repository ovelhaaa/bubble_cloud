# Bubbles — Cloud Chamber UI

## Direção visual

A referência principal é o mockup fornecido em 2 de outubro de 2026. A UI compartilha a disciplina de materiais do Apollo e a contenção tipográfica do Nimbus, consultados nos repositórios locais. A identidade própria permanece na nuvem granular: aqua suave, bolhas com profundidade, partículas e órbitas discretas.

- Grafite e carvão nas superfícies, marfim na informação, âmbar nas interações.
- Seis macros grandes em uma faixa; cinco knobs menores em Tonal Shaping.
- Cloud Chamber ampla ao lado de um painel integrado de Performance.
- Seletores, botões, menus, knobs e morph usam o mesmo tema.
- Valores dos knobs aparecem durante a interação; a descrição funcional continua no tooltip.
- Navegação anterior/próximo de presets usa o mesmo caminho do seletor existente.

## Organização

- `BubblesTheme.h`: cores semânticas, tipografia e retângulos compartilhados de layout.
- `BubblesLookAndFeel.h/.cpp`: desenho reutilizável de knobs, botões, seletores, menus e morph.
- `CloudVisualizer.h/.cpp`: visualização e suavização da telemetria, separadas do editor.
- `PluginEditor.cpp/.h`: composição, controles e vínculos existentes com o processador.
- `CMakeLists.txt`: registro dos novos arquivos de UI.
- `processor_smoke.cpp`: dimensão atualizada, verificação de exposição dos controles e imagens de repouso/áudio.

## Adaptações da referência

A janela passa de 1080 × 760 para 1160 × 900. O Rhythm Lab usa uma linha de seletores e uma segunda para o padrão, mantendo Freeze MIDI Mode/Note acessíveis. Os nomes e opções musicais existentes foram mantidos, mesmo quando diferentes dos textos ilustrativos do mockup.

Não foram inventados seletores 2D/3D/Stereo, engrenagem sem função, spread ou contagem de grãos: esses elementos da imagem não correspondem a recursos atuais. A visualização informa a quantidade real de vozes, seu limite, estado do motor e níveis de saída estéreo. As órbitas são cenário gráfico; bolhas, partículas locais, pulso e brilho respondem às vozes, pan, fase, ganho, pitch, freeze, spawn, envelope e picos reais.

A densidade visual acompanha o áudio. Em silêncio, a câmara mostra seu estado de espera, em vez de simular atividade granular. O desenho vetorial permite mudar escala e cores sem texturas raster; o compromisso é uma materialidade mais contida que a renderização cinematográfica da referência.

## Integridade funcional

O DSP e o processador não foram alterados. Presets, IDs, ranges, attachments, morph, callbacks de Freeze/Capture/Store e lógica do ritmo foram preservados. A comparação textual dos blocos funcionais e dos attachments confirmou que continuam iguais à versão anterior.

## Validação

O build de revisão fica fora do repositório em `C:/progs/vst/bubble_cloud_build_ui_ninja`, com cópia automática para `C:/VST` desativada. O smoke test existente verifica estado, MIDI, transporte, morph e os 20 presets, além dos controles visíveis e dos snapshots da nova interface.

Resultado: build Release do VST3 concluído e CTest aprovado (1/1). O smoke cobre os 20 presets, MIDI, transporte, freeze/capture e estado de morph. A nova verificação confirma 12 sliders, 8 seletores e 16 steps visíveis, dentro do editor, com altura mínima de clique/leitura no padrão. Os snapshots finais foram inspecionados visualmente. Não foi feito teste interativo numa DAW.
