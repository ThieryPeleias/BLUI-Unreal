# BLUI: layout, tamanho e input — pesquisa e design

Data: 2026-11-17. Base: BLUI `9da6439` (v5.1.0, CEF 128) e Unreal Engine 5.8.

## Problema

No BLUI, a página web só aparece num tamanho fixo, digitado à mão (`Settings.ViewSize`), presa no canto superior esquerdo. Não dá para centralizar nem esticar com âncoras do UMG. O widget **Web Browser** do UE (plugin `WebBrowserWidget`) faz isso sem configuração. Este documento explica por quê e define o design da correção.

## Como o Web Browser do UE funciona (UE 5.8)

Caminho base: `C:\Program Files\Epic Games\UE_5.8\Engine`.

### Cadeia de classes

| Classe | Arquivo | Papel |
|---|---|---|
| `UWebBrowser : UWidget` | `Plugins/Runtime/WebBrowserWidget/Source/WebBrowserWidget/Public/WebBrowser.h:15` | Widget UMG. Só tem `InitialURL` e `bSupportsTransparency`. Nenhuma propriedade de tamanho. |
| `SWebBrowser` | `Source/Runtime/WebBrowser/Public/SWebBrowser.h:21` | Barra de controles (oculta) + `SWebBrowserView` num slot Fill. |
| `SWebBrowserView` | `SWebBrowserView.h:38` | Cria a janela do browser e um `SViewport`. |
| `FWebBrowserViewport : ISlateViewport` | `WebBrowserViewport.h:16` | Ponte: leva tamanho e input do `SViewport` para o browser. |
| `FCEFWebBrowserWindow` | `CEF/CEFWebBrowserWindow.h:73` | Implementação CEF. |

`UWebBrowser::RebuildWidget` (`WebBrowser.cpp:79-103`) mostra um placeholder "Web Browser" no Designer e cria o `SWebBrowser` só em runtime.

### O tamanho vem da geometria, a cada tick

`SViewport::Tick` → `FWebBrowserViewport::Tick` (`WebBrowserViewport.cpp:25-40`):

```cpp
const float DPIScale = AllottedGeometry.Scale / DPI;          // DPI = DPI do SO
FVector2D AbsoluteSize = AllottedGeometry.GetLocalSize() * DPIScale;
WebBrowserWindow->SetViewportSize(AbsoluteSize.IntPoint(), AllottedGeometry.GetAbsolutePosition().IntPoint());
```

`FCEFWebBrowserWindow::SetViewportSize` (`CEFWebBrowserWindow.cpp:626-720`) ignora tamanho 0 e tamanho igual ao anterior. Senão guarda o tamanho e chama `WasResized()` (mais `Invalidate` no CEF ≥ 128). O CEF então chama:

- `GetViewRect` → `ViewportSize` em pixels CSS (`CEFWebBrowserWindow.cpp:1885-1898`).
- `GetScreenInfo` → `device_scale_factor` = DPI do SO (`CEFBrowserHandler.cpp:361-381`).

Resultado: a textura tem 1 texel por pixel físico da tela (tamanho local × `Geometry.Scale`). Não há debounce. O único filtro é "mudou e não é zero".

### Desenho

`SViewport::OnPaint` (`SViewport.cpp:167-176`) desenha a textura esticada para a geometria. Um frame atrasado (tamanho antigo) é só escalado, nunca lido fora do buffer. `OnPaint` do CEF descarta frames cujo tamanho não bate com o viewport (`CEFWebBrowserWindow.cpp:2021-2056`).

### Tamanho desejado (por que âncoras funcionam)

Nenhuma classe sobrescreve `ComputeDesiredSize`. O `SViewport` devolve o padrão **320x240** (`SViewport.cpp:18-21`). Esse valor só importa quando o pai pede o tamanho do filho (slot de Canvas com `Size To Content`, alinhamento Center, slot Auto, `SizeBox` sem override). Com âncora de stretch ou slot Fill, o pai decide o tamanho e o browser segue no próximo tick.

### Mouse

`GetCefMouseEvent` (`CEFWebBrowserWindow.cpp:2637-2656`):

```cpp
FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
LocalPos *= ViewportScaleFactor;   // = Geometry.Scale / DPI do SO
```

`AbsoluteToLocal` desfaz posição, âncora, alinhamento e render transform. O clique cai certo com o widget centralizado, esticado ou deslocado.

## Como o BLUI funciona hoje

Caminho base: `E:\@Virtuart\Claude\Projetos\BLUI`.

### Classes

- `UBluEye` (`Source/Blu/Public/BluEye.h:10`) é um `UObject`, não um widget. Dono do browser CEF, do `RenderHandler`, de um `UTexture2D` e de um `UMaterialInstanceDynamic` opcional.
- `RenderHandler : CefRenderHandler` (`Source/Blu/Public/RenderHandler.h:9`) guarda `Width`/`Height` e implementa `GetViewRect` e `OnPaint`. Não implementa `GetScreenInfo`, então o fator de escala é sempre 1.
- Não existe nenhum `SWidget` nem `UWidget` em C++. `Blu.Build.cs` já depende de `Slate`, `SlateCore` e `UMG`.
- `OnPaint` do CEF roda na game thread: o tick do CEF é `BluManager::DoBluMessageLoop()` chamado por um `FTSTicker` (`BluEye.cpp:654-680`).

### Fluxo do tamanho

A única fonte é `Settings.ViewSize` (padrão 1280x720, `BluEye.cpp:12-13`). Usos:

- `Init`: recusa se ≤ 0 (`BluEye.cpp:46-50`); cria `RenderHandler(ViewSize)` (`:75`).
- Textura: `UTexture2D::CreateTransient(ViewSize)` em `ResetTexture` (`:121`), `ResizeBrowser` (`:359`), `CropWindow` (`:393`) e `GetTexture` quando a textura é nula (`:686`, cria uma textura nova a cada chamada).
- `TextureUpdate` (`:167-210`) copia `ViewSize.X * ViewSize.Y * 4` bytes do buffer do CEF. Ignora o `InWidth`/`InHeight` que o CEF passa para `OnPaint` (`RenderHandler.cpp:17-34`) e ignora o tipo (`PET_VIEW` ou `PET_POPUP`).

### Como chega na tela

Só pelo Blueprint `Content/BluiWidget.uasset`: um `Image` dentro de um `CanvasPanel`. `InitBluEye(url, ViewSize)` chama `SetBrushFromTexture` e `SetSize(ViewSize)` no slot do canvas. O slot fica com a âncora padrão (canto superior esquerdo). Não há repasse de mouse. (Lido de strings do `.uasset`; os grafos não foram abertos.)

### Input

`TriggerMouseMove/LeftMouseDown/...(Pos, Scale)` mandam `Pos / Scale` direto ao CEF, em pixels do browser (`BluEye.cpp:431-492`). Não há conversão de coordenadas locais do widget.

### Defeitos encontrados

1. **`ResizeBrowser` vaza a textura antiga** (`BluEye.cpp:336-375`): cria e faz `AddToRoot` da nova sem `DestroyTexture` da antiga.
2. **`ResizeBrowser` não atualiza o material**: não chama `ResetMatInstance`, então o `MaterialInstance` continua com a textura antiga.
3. **`ResizeBrowser` força `bEnabled = true`** no fim, mesmo que o usuário tenha desligado o BluEye.
4. **Leitura além do buffer em `TextureUpdate`**: depois de um resize, o CEF ainda entrega 1-2 frames no tamanho antigo. O pitch e a cópia usam o `ViewSize` novo. Se o novo é maior, o `Memcpy` lê além do fim do buffer do CEF.
5. **Popups corrompem a textura**: `OnPaint` com `PET_POPUP` (ex.: lista de um `<select>`) traz um buffer do tamanho do popup, copiado como se fosse a view.
6. **Vazamento de `UpdateRegions`**: `OnPaint` faz `FMemory::Malloc` das regiões; as saídas antecipadas de `TextureUpdate` não liberam.
7. **Sem DPI**: o browser sempre renderiza com fator 1.

Fora de escopo (anotado, não corrigido aqui): `CloseBrowser` chama `BeginDestroy` à mão, e o GC chama de novo, decrementando `EyeCount` duas vezes. `GetTexture` cria textura nova a cada chamada quando não há textura.

## Design da correção

### Objetivo

Um widget UMG nativo, **`BluBrowser`**, que se comporta como o Web Browser do UE: arrasta no Designer, usa âncora, centralização e Fill, e o browser acompanha o tamanho sozinho. Cliques e teclado funcionam em qualquer posição.

### Decisões

| # | Decisão | Por quê |
|---|---|---|
| D1 | `UBluBrowser : UWidget` + `SBluBrowser : SLeafWidget`, nos arquivos novos `BluBrowser.h/.cpp` e `SBluBrowser.h/.cpp`. | Mesmo modelo do `UWebBrowser`. Um `SLeafWidget` desenha a textura direto, sem `SViewport`. |
| D2 | O `SBluBrowser` lê o tamanho local e `Geometry.Scale` no `Tick` e redimensiona só quando muda e não é zero. Sem debounce. | Igual ao engine. |
| D3 | **Unidades**: tamanho CSS do browser = tamanho local em unidades Slate (arredondado). `device_scale_factor` = `Geometry.Scale`. Textura = tamanho CSS × escala, arredondado para cima (pixels físicos, como o Chromium). | A página tem o mesmo tamanho lógico do design UMG em qualquer resolução, e o texto fica nítido. Mouse: posição local = pixel CSS, sem conversão extra. Difere do engine, cuja largura CSS muda com a curva de DPI do UMG. |
| D4 | `ComputeDesiredSize` devolve `Settings.ViewSize`. | Mantém `ViewSize` útil: é o tamanho usado quando o slot pede o tamanho do conteúdo. |
| D5 | No Designer (editor fora do PIE), `RebuildWidget` mostra um placeholder "BLUI Browser" e não cria `UBluEye`. | `UBluEye::Init` recusa fora do PIE; evita CEF no editor. |
| D6 | `UBluBrowser` é dono do `UBluEye` (`UPROPERTY`, outer = o widget) e o reaproveita se `RebuildWidget` rodar de novo. Expõe `GetBluEye()` para JS, `LoadURL` e eventos. | Remover e re-adicionar o widget à tela não cria um segundo browser. |
| D7 | Nova `UBluEye::ResizeBrowserScaled(W, H, Scale)`. `ResizeBrowser(W, H)` passa a chamar ela com a escala atual. Corrige os defeitos 1, 2 e 3. Sem mudança de assinatura para quem já usa `ResizeBrowser`. | Compatível com Blueprints existentes; escala padrão 1 mantém o comportamento antigo. |
| D8 | `TextureUpdate` recebe `InWidth`/`InHeight` e usa como pitch e tamanho da cópia. Frame com o tamanho de antes do último resize é descartado. Qualquer outro tamanho diferente da textura recria a textura no tamanho do buffer e envia o buffer inteiro. `OnPaint` ignora `PET_POPUP`. Toda saída antecipada libera `UpdateRegions`. | Corrige 4, 5 e 6. A cópia nunca passa do buffer. Descartar o frame antigo = 1-2 frames com a imagem anterior esticada, como no engine. A recriação cobre a diferença de arredondamento entre o nosso cálculo e o do Chromium (`ScaleToCeiledSize`, arredonda para cima). |
| D9 | `RenderHandler` ganha `float ScaleFactor = 1.f` e `GetScreenInfo`. Mudança só de escala chama `NotifyScreenInfoChanged()`. | Corrige 7. |
| D10 | Repasse de mouse (move, down, up, wheel, com captura) e teclado (key down/up, char) no `SBluBrowser`, usando os `Trigger*`/`KeyDown`/`KeyUp`/`CharKeyInput` existentes. | Sem isso, centralizar ou esticar desalinha os cliques. |
| D11 | O Blueprint `BluiWidget.uasset` não é alterado. O README passa a recomendar o `BluBrowser`. | Não editar asset binário; quem usa o antigo não quebra. |

### Fora de escopo

Controles de navegação, throbber, transparência no widget, cache de frame durante resize ("smooth resizing" do engine), correção de `CloseBrowser`/`GetTexture`, edição do `BluiWidget.uasset`, plataformas Mac/Linux (só Win64 é verificado).
