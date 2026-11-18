# UE 5.8 Web Browser widget: layout, tamanho e input (relatório bruto)

Relatório do subagente de exploração, 2026-11-17. Base: `C:\Program Files\Epic Games\UE_5.8\Engine`. Resumo e decisões em `../layout-research.md`.

O Web Browser widget pega o tamanho da geometria que o layout dá a ele. Não há tamanho fixo em nenhum ponto da cadeia. A cada tick do Slate o browser CEF é redimensionado para o tamanho local do widget, por isso âncoras, stretch e fill já funcionam. O padrão 320x240 só aparece quando o pai pede o tamanho desejado (auto-size ou alinhamento central).

## 1. Cadeia de classes

- **`UWebBrowser : UWidget`** — `Plugins/Runtime/WebBrowserWidget/Source/WebBrowserWidget/Public/WebBrowser.h:15`.
  - Propriedades: só `InitialURL` e `bSupportsTransparency` (`WebBrowser.h:89-94`). Nenhuma de tamanho.
  - `RebuildWidget` em `WebBrowser.cpp:79-103`. No Designer devolve um `SBox` com o texto "Web Browser". Em runtime:
    ```cpp
    WebBrowserWidget = SNew(SWebBrowser)
        .InitialURL(InitialURL)
        .ShowControls(false)
        .SupportsTransparency(bSupportsTransparency) ...;
    ```
  - `SynchronizeProperties` vazio (`WebBrowser.cpp:105-113`). `ViewportSize` nunca é setado.
- **`SWebBrowser : SCompoundWidget`** — `Source/Runtime/WebBrowser/Public/SWebBrowser.h:21`.
  - Layout: `SVerticalBox` = [barra de controles, colapsada com `ShowControls(false)`] + [slot fill: `SOverlay`{`SWebBrowserView`, throbber centralizado}] (`SWebBrowser.cpp:62-170`).
  - Repassa `.ViewportSize(InArgs._ViewportSize)` (linha 141). Padrão `ZeroVector` (`SWebBrowser.h:42`).
- **`SWebBrowserView : SCompoundWidget`** — `SWebBrowserView.h:38`.
  - `Construct` cria a janela via `IWebBrowserModule::Get().GetSingleton()->CreateBrowserWindow(Settings)` (`SWebBrowserView.cpp:131`).
  - Filho = `FWebBrowserWindow::CreateWidget()` (`SWebBrowserView.cpp:142-147`), que é um `SViewport` (`CEFWebBrowserWindow.cpp:600-613`).
  - Cria um `FWebBrowserViewport` e liga como interface do viewport: `BrowserWidget->SetViewportInterface(BrowserViewport.ToSharedRef());` (`SWebBrowserView.cpp:232-234`).
  - O argumento `ViewportSize` (`SWebBrowserView.h:64,110`) só é usado no `SViewport` do popup/menu (`SWebBrowserView.cpp:719`). A view principal ignora.
- **`FWebBrowserViewport : ISlateViewport`** — `WebBrowserViewport.h:16`. Ponte que leva tamanho e input do `SViewport` para a janela do browser.
- **`IWebBrowserWindow`** — `IWebBrowserWindow.h:85`. Declara `SetViewportSize(FIntPoint WindowSize, FIntPoint WindowPos)` na linha 109.
- **`FCEFWebBrowserWindow : IWebBrowserWindow`** — `CEF/CEFWebBrowserWindow.h:73-74`. Implementação Windows/Mac/Linux.
  - `GetViewportSize()` devolve `NoneValue` (linha 124): não dá para consultar o tamanho por ela.

## 2. Como a geometria define o tamanho em pixels do browser

Roda a cada tick do Slate enquanto o widget está visível e pintado: `SViewport::Tick` → `FWebBrowserViewport::Tick` → `FCEFWebBrowserWindow::SetViewportSize` → CEF `WasResized()` → CEF chama `GetViewRect` e `GetScreenInfo` → `OnPaint` entrega um buffer do tamanho novo.

**Passo 1 — `SViewport::Tick`** (`Runtime/Slate/Private/Widgets/SViewport.cpp:254-262`) repassa a própria `AllottedGeometry`:
```cpp
ViewportInterface.Pin()->Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
```

**Passo 2 — `FWebBrowserViewport::Tick`** (`WebBrowserViewport.cpp:25-40`):
```cpp
if (!bIsPopup)
{
    const float DPI = (WebBrowserWindow->GetParentWindow().IsValid() ? WebBrowserWindow->GetParentWindow()->GetNativeWindow()->GetDPIScaleFactor() : 1.0f);
    const float DPIScale = AllottedGeometry.Scale / DPI;
    FVector2D AbsoluteSize = AllottedGeometry.GetLocalSize() * DPIScale;
    WebBrowserWindow->SetViewportSize(AbsoluteSize.IntPoint(), AllottedGeometry.GetAbsolutePosition().IntPoint());
#if WITH_CEF3
    CefWebBrowserWindow->UpdateCachedGeometry(AllottedGeometry);
#endif
}
```
- `Geometry.Scale` é a escala de layout acumulada: DPI do SO × curva de DPI do UMG × escalas de layout como `SScaleBox`. Não inclui render transforms.
- `ViewportSize` (pixels CSS) = tamanho local × (escala de layout ÷ DPI do SO).
- `.IntPoint()` trunca.
- A janela pai só é cacheada depois, em `SWebBrowserView::OnPaint` (`SWebBrowserView.cpp:256-266`). Até lá o DPI vale 1. Quando a janela pai é conhecida, a mudança de DPI dispara um resize.

**Passo 3 — `FCEFWebBrowserWindow::SetViewportSize`** (`CEFWebBrowserWindow.cpp:626-720`):
```cpp
if (!bIsDisabled) { SetIsHidden(false); }
bTickedLastFrame=true;
float WindowDPIScaleFactor = 1.0f;
if (TSharedPtr<SWindow> ParentWindowPtr = ParentWindow.Pin())
    WindowDPIScaleFactor = ParentWindowPtr->GetNativeWindow()->GetDPIScaleFactor();
ViewportPos = WindowPos;
// Ignore sizes that can't be seen as it forces CEF to re-render whole image
if ((WindowSize.X > 0 && WindowSize.Y > 0 && ViewportSize != WindowSize) || WindowDPIScaleFactor != ViewportDPIScaleFactor)
{
    ViewportSize = WindowSize;
    ViewportDPIScaleFactor = WindowDPIScaleFactor;
    FIntPoint WindowSizeScaled = (FVector2D(WindowSize) * WindowDPIScaleFactor).IntPoint();
    ...  // só no modo HWND direto: ::SetWindowPos(NativeHandle, ..., WindowSizeScaled) (664)
    RequestCefResize();   // -> GetHost()->WasResized() (+Invalidate no CEF>=128) (2961-2979)
    if (bUsingAcceleratedPaint && RHIRenderHelper)
        UpdatableTextures[PET_VIEW] = RHIRenderHelper->ResizeSlateUpdatableTexture(UpdatableTextures[PET_VIEW], WindowSizeScaled);
    else if (CapturedCefFrame.IsValid() && !CapturedCefFrame->IsEmpty() && NumFramesSinceViewportResize < 10)
        { /* new FSlateUpdatableTexture(WindowSizeScaled), copia o último frame 1:1, libera o antigo */ }
    NumFramesSinceViewportResize = 0;
}
```

**Passo 4 — CEF lê o tamanho** via `FCEFBrowserHandler::GetViewRect` (`CEFBrowserHandler.cpp:293-307`), que chama `FCEFWebBrowserWindow::GetViewRect` (`CEFWebBrowserWindow.cpp:1885-1898`):
```cpp
Rect.width = FMath::Max(ViewportSize.X, 1);
Rect.height = FMath::Max(ViewportSize.Y, 1);   // 1x1 enquanto ViewportSize==0
```

**Passo 5 — DPI vai para o CEF** via `GetScreenInfo` (`CEFBrowserHandler.cpp:361-381`):
```cpp
ScreenInfo.device_scale_factor = BrowserWindow->GetParentWindow()->GetNativeWindow()->GetDPIScaleFactor();
```
- O CEF renderiza `ViewportSize × DPI do SO` pixels reais = tamanho local × `Geometry.Scale`. Um texel por pixel físico.
- `devicePixelRatio` na página = DPI do SO. A escala de DPI do UMG muda a largura CSS.

**Passo 6 — textura e desenho.**
- `OnPaint` (`CEFWebBrowserWindow.cpp:2013-2121`) calcula `ViewportRect = FIntRect(0,0,ViewportSize).Scale(ViewportDPIScaleFactor)` e descarta frames enquanto está vazio (2021-2025).
- Cria a textura sob demanda com `Renderer->CreateUpdatableTexture(Width, Height)` (2077) e preenche com `UpdateTextureThreadSafeRaw(Width, Height, Buffer, DirtyRect)` (2102).
- `GetTexture` devolve `UpdatableTextures[...]->GetSlateResource()` (722-729). `FWebBrowserViewport::GetViewportRenderTargetTexture` devolve essa textura (`WebBrowserViewport.cpp:20-23`).
- `SViewport::OnPaint` desenha esticada para a geometria, seja qual for o tamanho da textura (`SViewport.cpp:167-176`):
  ```cpp
  FSlateDrawElement::MakeViewport( OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), ViewportInterfacePin, DrawEffects, ...);
  // senão (sem textura ainda) MakeBox com brush preto
  ```
  Frame atrasado ou de tamanho antigo é escalado. Render transform do UMG escala a imagem sem re-rasterizar.
- Modo acelerado (textura compartilhada) no Windows quando o RHI suporta (`CanSupportAcceleratedPaint`, 2696). Padrão 60 fps; caminho software 24 fps (`WebBrowserSingleton.cpp:690-698`, `IWebBrowserSingleton.h:62`).

**Visibilidade:** `CheckTickActivity` (`CEFWebBrowserWindow.cpp:2728-2746`) esconde o browser (`WasHidden`) se não houve tick do Slate desde a última checagem. Widget colapsado ou não pintado para de renderizar e de redimensionar.

## 3. O que `ComputeDesiredSize` devolve

- Nem `SWebBrowser` nem `SWebBrowserView` sobrescrevem. São `SCompoundWidget`, então devolvem o desejado do filho.
- A folha é o `SViewport` de `CreateWidget()`, que nunca seta `.ViewportSize`. Cai no padrão:
  - `SViewport.h:113-115`: `virtual FVector2D ComputeDesiredSize(float) const override { return ViewportSize.Get(); }`
  - `SViewport.cpp:18-21`: `GetDefaultViewportSize() { return FVector2f(320.0f, 240.0f); }`
- Barra colapsada, throbber 20x20 dentro do overlay. Desejado efetivo do `UWebBrowser`: **320x240 unidades Slate**.
- Por que âncoras, stretch e fill funcionam: o desejado é só uma dica. A resolução vem da geometria no Tick, não do desejado.
  - Âncoras stretch no Canvas, offsets sem auto-size, slot Overlay/SizeBox em Fill: o pai dá qualquer tamanho, que vira `ViewportSize` no tick seguinte.
  - O desejado só conta onde o pai pede: `bAutoSize` no slot de Canvas, slots Overlay/Box com alinhamento Center/Left, slots Auto em HBox/VBox, `SizeBox` sem override. Aí dá 320x240.
- O argumento `ViewportSize` de `SWebBrowser`/`SWebBrowserView` não afeta a view principal. Só o `SViewport` do popup (`SWebBrowserView.cpp:713-725`).

## 4. Mouse: coordenada local do widget → pixel do browser

- `FWebBrowserViewport::OnMouse*` passa a `MyGeometry` do `SViewport` para `FCEFWebBrowserWindow::OnMouse*` (`WebBrowserViewport.cpp:52-102`; `CEFWebBrowserWindow.cpp:1505-1700`). Todo handler chama `GetCefMouseEvent` (`CEFWebBrowserWindow.cpp:2637-2656`):
  ```cpp
  FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
  // convert from game viewport coords to webview coords
  if (bIsPopup)
      LocalPos += FVector2D(PopupRect.Min) * ViewportScaleFactor;
  else
      LocalPos *= ViewportScaleFactor;
  CefMouseEvent Event; Event.x = LocalPos.X; Event.y = LocalPos.Y;
  ```
- `ViewportScaleFactor` é atualizado todo tick em `UpdateCachedGeometry` (`CEFWebBrowserWindow.cpp:2717-2726`), logo depois de `SetViewportSize`:
  ```cpp
  // Extract the game viewport scale factor from the global Scale, which also includes the DPI scale factor from the OS
  ViewportScaleFactor = AllottedGeometry.Scale / ViewportDPIScaleFactor;
  ```
- Resultado: coordenadas no espaço CSS/DIP, o mesmo de `ViewportSize`. `AbsoluteToLocal` desfaz toda a transformação, incluindo render transform e posição. Cliques corretos com qualquer âncora, alinhamento, offset e render transform.
- Input bloqueado no modo HWND direto (não-OSR) via `BlockInputInDirectHwndMode()` (615). O IME também recebe a geometria (`CEFTextInputMethodContext.cpp:35, 145, 214`).

## 5. Throttle e debounce no resize

- **Sem debounce nem throttle por tempo.** `SetViewportSize` roda todo tick. Único filtro: mudança + tamanho não-zero (`CEFWebBrowserWindow.cpp:644`). Zero em qualquer eixo é ignorado; mudança de DPI sempre passa.
- **Bug de resize do CEF ≥ 128** (`CEFWebBrowserWindow.cpp:248-252`):
  ```cpp
  #define UE_CEF_HAS_RESIZE_BUG (CEF_VERSION_MAJOR >= 128)
  // CEF is usually 1-2 frames behind our current viewport size when drag-resizing, ...
  #define MIN_FRAMES_FOR_CEF_RESIZE 3
  ```
  - `OnPaint` marca `bHasCorrectNativeCefBuffer = (FIntPoint(Width, Height) == ViewportRect.Size())`. Com buffer errado, mostra o último frame capturado (2029-2056, 2091-2099).
  - Um `WasResized` forçado é refeito se o buffer continua errado depois de 3 frames: `IsCefResizeNeeded` (2951-2959), chamado de `SWebBrowser::Tick` (`SWebBrowser.cpp:27-43`).
  - Esse retry está dentro de `#if WITH_EDITOR`: builds empacotados não rodam.
- **Smooth resizing:** argumento `UseSmoothResizing` de `SWebBrowser`, padrão true (`SWebBrowser.h:41,85`). Chama `SetCaptureCefFrames` (`SWebBrowser.cpp:182`, `CEFWebBrowserWindow.cpp:2925-2940`). No resize, o frame antigo é copiado 1:1 para a textura nova só se `NumFramesSinceViewportResize < 10` (692), para a imagem não esticar.
- **Taxa efetiva**: limitada pelo frame rate windowless do CEF: 24 fps padrão, 60 com paint acelerado.

## Não verificado

Caminhos Android/Apple (têm `ComputeDesiredSize` e `SetViewportSize` próprios), código de slot do `UWidget`/CanvasPanel, internals de `FGeometry::Scale`. A afirmação de que render transforms não entram em `Geometry.Scale` vem de conhecimento do engine, não foi checada neste código.
