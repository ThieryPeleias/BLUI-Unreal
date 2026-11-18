# BluBrowser Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar ao BLUI um widget UMG nativo `BluBrowser` que se ancora, centraliza e estica como o Web Browser do UE, com o browser CEF acompanhando o tamanho e o DPI, e cliques/teclado mapeados certo.

**Architecture:** `UBluBrowser : UWidget` cria e possui um `UBluEye`. Um `SBluBrowser : SLeafWidget` lê a geometria no `Tick`, chama `UBluEye::ResizeBrowserScaled`, desenha a textura esticada no `OnPaint` e repassa mouse/teclado. O caminho de render do `UBluEye` é corrigido para aceitar resize sem vazar textura nem ler além do buffer do CEF.

**Tech Stack:** Unreal Engine 5.8 (C++, Slate, UMG, Automation Tests), CEF 128 (off-screen rendering), Win64.

**Spec:** `docs/layout-research.md` (seções "Defeitos encontrados" e "Design da correção", decisões D1-D11).

## Global Constraints

- Engine: UE 5.8 em `C:\Program Files\Epic Games\UE_5.8`. Plataforma verificada: Win64 só.
- Módulo: tudo novo vai no módulo `Blu` (`Source/Blu`), macro de export `BLU_API`. Sem novas dependências em `Blu.Build.cs` (já tem `Slate`, `SlateCore`, `UMG`).
- Unidades (D3): tamanho CSS do browser = tamanho local Slate arredondado; `device_scale_factor` = `Geometry.Scale`; textura = CSS × escala arredondado para cima, mínimo 1 por eixo. O tamanho real da textura segue o buffer do CEF (D8).
- Compatibilidade: a assinatura de `UBluEye::ResizeBrowser(const int32, const int32)` e de todos os `UFUNCTION` existentes não muda. `BluiWidget.uasset` não é alterado.
- Escala padrão 1: quem não usa `BluBrowser` vê o mesmo comportamento de antes.
- Estilo: tabs, chaves em linha própria, como o resto de `Source/Blu`.

## Ambiente de verificação (uma vez, antes da Task 1)

Os binários do CEF não estão no git (`ThirdParty/cef/Win/*` está no `.gitignore`).

1. **O usuário** baixa o release v5.1.0 de https://github.com/getnamo/BLUI-Unreal/releases e copia `Plugins/BLUI/ThirdParty/cef/Win` do zip para `E:\@Virtuart\Claude\Projetos\BLUI\ThirdParty\cef\Win`. (Download: precisa da permissão do usuário.)
2. Tudo fica dentro de `E:\@Virtuart\Claude\Projetos\BLUI`. Projeto host Blueprint-only em `E:\@Virtuart\Claude\Projetos\BLUI\BLUIHost\BLUIHost.uproject`, fora do git (adicionar `BLUIHost/` ao `.gitignore`):
   ```json
   {"FileVersion":3,"EngineAssociation":"5.8","Plugins":[{"Name":"BLUI","Enabled":true}]}
   ```
   O plugin entra no host por junction para a raiz do repo (o UBT para de descer ao achar `BLUI.uplugin`, então não há recursão):
   ```powershell
   New-Item -ItemType Junction -Path "E:\@Virtuart\Claude\Projetos\BLUI\BLUIHost\Plugins\BLUI" -Target "E:\@Virtuart\Claude\Projetos\BLUI"
   ```
3. Comando de build (usado em toda task; compila os módulos do plugin para o editor, saída em `BLUI\Binaries`, já no `.gitignore`):
   ```bash
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" UnrealEditor Win64 Development -Project="E:\@Virtuart\Claude\Projetos\BLUI\BLUIHost\BLUIHost.uproject" -WaitMutex
   ```
   Passou: termina com `Result: Succeeded` e exit code 0.
4. Comando de teste:
   ```bash
   "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\@Virtuart\Claude\Projetos\BLUI\BLUIHost\BLUIHost.uproject" -ExecCmds="Automation RunTests Blu.Layout;Quit" -unattended -nullrhi -nosplash -log
   ```
   Passou: o log tem `Result={Success}` para cada teste `Blu.Layout.*` e nenhum `Result={Fail}`.

## Review Focus

1. **Widget com tamanho 0** (Collapsed, primeiro tick antes do layout, slot de 0 px): não redimensiona, não cria textura, não crasha. Coberto por `Blu.Layout.ComputeSizes` (Task 1) e pelo checklist PIE (Task 3).
2. **Frame do CEF no tamanho antigo logo depois de um resize, ou com arredondamento diferente do nosso**: o antigo é descartado; o de arredondamento diferente recria a textura; nunca lê além do buffer, nunca fica preto. Coberto pela Task 2 e pelos checklists de resize e DPI 1.25 (Task 3).
3. **Widget removido e re-adicionado à tela** (`RebuildWidget` duas vezes): reaproveita o mesmo `UBluEye`, sem segundo browser. Checklist PIE (Task 3).
4. **Designer fora do PIE**: placeholder, nenhum processo CEF, nenhuma textura alocada por frame. Checklist (Task 3).
5. **Mudança de DPI** (curva de DPI do UMG, janela entre monitores): textura recriada na escala nova, tamanho CSS igual. Coberto por `Blu.Layout.ComputeSizes` (escala 1.5) e checklist (Task 3).

---

### Task 1: Cálculo de tamanhos + teste

**Files:**
- Create: `Source/Blu/Private/BluLayout.h`
- Create: `Source/Blu/Private/Tests/BluLayoutTests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace BluLayout
  {
  	struct FSizes { FIntPoint ViewSize; FIntPoint TextureSize; };
  	// false when LocalSize rounds to < 1 on either axis, or Scale <= 0
  	inline bool ComputeSizes(const FVector2D& LocalSize, float Scale, FSizes& Out);
  }
  ```
  Regra: `ViewSize = (RoundToInt(LocalSize.X), RoundToInt(LocalSize.Y))`; `TextureSize = (Max(1, CeilToInt(ViewSize.X * Scale)), Max(1, CeilToInt(ViewSize.Y * Scale)))` (ceil = mesmo arredondamento do Chromium).

- [ ] **Step 1: Montar o ambiente de verificação** (seção acima, itens 1-2). Confirmar que `ThirdParty\cef\Win\lib\blucef.lib` existe.

- [ ] **Step 2: Escrever o teste** `FBluLayoutComputeSizesTest`, nome `"Blu.Layout.ComputeSizes"`, flags `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`, dentro de `#if WITH_DEV_AUTOMATION_TESTS`. Asserções (`TestTrue`/`TestFalse`/`TestEqual`):
  ```cpp
  BluLayout::FSizes S;
  TestTrue (TEXT("800x600@1"),   BluLayout::ComputeSizes(FVector2D(800, 600), 1.f, S));
  TestEqual(TEXT("view"),        S.ViewSize,    FIntPoint(800, 600));
  TestEqual(TEXT("tex"),         S.TextureSize, FIntPoint(800, 600));
  TestTrue (TEXT("800x600@1.5"), BluLayout::ComputeSizes(FVector2D(800, 600), 1.5f, S));
  TestEqual(TEXT("view dpi"),    S.ViewSize,    FIntPoint(800, 600));
  TestEqual(TEXT("tex dpi"),     S.TextureSize, FIntPoint(1200, 900));
  TestTrue (TEXT("801x601@1.25"), BluLayout::ComputeSizes(FVector2D(801, 601), 1.25f, S));
  TestEqual(TEXT("tex ceil"),    S.TextureSize, FIntPoint(1002, 752));
  TestTrue (TEXT("rounding"),    BluLayout::ComputeSizes(FVector2D(799.6, 600.4), 1.f, S));
  TestEqual(TEXT("rounded"),     S.ViewSize,    FIntPoint(800, 600));
  TestFalse(TEXT("zero width"),  BluLayout::ComputeSizes(FVector2D(0, 600), 1.f, S));
  TestFalse(TEXT("sub-pixel"),   BluLayout::ComputeSizes(FVector2D(0.4, 600), 1.f, S));
  TestFalse(TEXT("zero scale"),  BluLayout::ComputeSizes(FVector2D(800, 600), 0.f, S));
  TestTrue (TEXT("tiny scale"),  BluLayout::ComputeSizes(FVector2D(1, 1), 0.1f, S));
  TestEqual(TEXT("min 1px"),     S.TextureSize, FIntPoint(1, 1));
  ```

- [ ] **Step 3: Build** (comando 3). Esperado: FALHA de compilação (`Result: Failed`), `BluLayout.h` não existe.

- [ ] **Step 4: Implementar `BluLayout::ComputeSizes`** em `Source/Blu/Private/BluLayout.h` (`#pragma once`, `#include "CoreMinimal.h"`), pela regra do bloco Interfaces.

- [ ] **Step 5: Build + teste** (comandos 3 e 4). Esperado: `Result: Succeeded`, `Blu.Layout.ComputeSizes` com `Result={Success}`.

- [ ] **Step 6: Commit**
  ```bash
  git add Source/Blu/Private/BluLayout.h Source/Blu/Private/Tests/BluLayoutTests.cpp
  git commit -m "feat(blu): add layout size computation with automation test"
  ```

### Task 2: Corrigir resize e caminho de render do UBluEye

Corrige os defeitos 1-7 do spec (D7, D8, D9).

**Files:**
- Modify: `Source/Blu/Public/RenderHandler.h:9-28`
- Modify: `Source/Blu/Private/RenderHandler.cpp:12-34`
- Modify: `Source/Blu/Public/BluEye.h:215-223` (API pública), `:242` (`ResetTextureToSize`), `:262-265` (`StaleBufferSize`)
- Modify: `Source/Blu/Private/BluEye.cpp:110-130` (`ResetTexture`), `:167-210` (`TextureUpdate`), `:336-375` (`ResizeBrowser`)

**Interfaces:**
- Consumes: `BluLayout::ComputeSizes` (Task 1), para o tamanho da textura.
- Produces:
  ```cpp
  // RenderHandler
  float ScaleFactor = 1.f;
  virtual bool GetScreenInfo(CefRefPtr<CefBrowser> Browser, CefScreenInfo& ScreenInfo) override;

  // UBluEye (public)
  UFUNCTION(BlueprintCallable, Category = "Blu")
  UTexture2D* ResizeBrowserScaled(const int32 NewWidth, const int32 NewHeight, const float NewScale = 1.f);
  UTexture2D* GetTextureIfValid() const;   // nullptr until Init created a texture; never allocates
  void TextureUpdate(const void* Buffer, FUpdateTextureRegion2D* UpdateRegions, uint32 RegionCount, int32 BufferWidth, int32 BufferHeight);
  ```

- [ ] **Step 1: `RenderHandler`**
  - `GetScreenInfo`: `ScreenInfo.device_scale_factor = ScaleFactor; return true;`
  - `OnPaint`: se `Type != PET_VIEW`, retorna antes do `Malloc`. Passa `InWidth, InHeight` para `TextureUpdate`.

- [ ] **Step 2: `UBluEye::ResetTexture`** passa a chamar o novo `protected: void ResetTextureToSize(int32 PixelWidth, int32 PixelHeight)` com o `TextureSize` de `BluLayout::ComputeSizes(Settings.ViewSize, Renderer ? Renderer->ScaleFactor : 1.f, ...)`. `ResetTextureToSize` é o corpo atual de `ResetTexture` (destrói a antiga, cria, `AddToRoot`, `UpdateResource`, `ResetMatInstance`) com o tamanho recebido. Se `ComputeSizes` falhar, não cria textura (`Init` já recusa ViewSize ≤ 0).

- [ ] **Step 3: `UBluEye::TextureUpdate`**, nesta ordem:
  - Toda saída antecipada faz `FMemory::Free(UpdateRegions)`.
  - Novo membro privado `FIntPoint StaleBufferSize = FIntPoint::ZeroValue;` (gravado na Step 4). Se o buffer é `StaleBufferSize`: libera e retorna sem log (frame do tamanho antigo, esperado por 1-2 frames depois de resize).
  - Se o buffer difere do tamanho da textura: `ResetTextureToSize(BufferWidth, BufferHeight)` e troca as regiões por uma só, do buffer inteiro (`0, 0, BufferWidth, BufferHeight`), porque a textura nova está vazia. Comentário de uma linha: cobre a diferença de arredondamento com o Chromium.
  - Ao processar um frame, zera `StaleBufferSize`.
  - `SrcPitch = BufferWidth * 4`; tamanho da cópia `SrcPitch * BufferHeight`.

- [ ] **Step 4: `UBluEye::ResizeBrowserScaled`**
  - Se `NewWidth <= 0 || NewHeight <= 0 || NewScale <= 0`: log como hoje, retorna `Texture`.
  - Grava `Settings.ViewSize`. Se `!Renderer || !Browser` (antes de `Init`), retorna `Texture`.
  - Se tamanho e escala iguais aos do `Renderer`, retorna `Texture` sem fazer nada.
  - Guarda se a escala mudou; `StaleBufferSize` = tamanho atual da textura (ou zero se não há textura); atualiza `Renderer->Width/Height/ScaleFactor`.
  - `ResetTexture()` (já destrói a antiga e chama `ResetMatInstance`). A textura nova já existe quando a função retorna, então quem usa o retorno em Blueprint continua funcionando.
  - Se a escala mudou: `Browser->GetHost()->NotifyScreenInfoChanged()`.
  - `Browser->GetHost()->WasResized(); Browser->GetHost()->Invalidate(PET_VIEW);`
  - Não mexe em `bEnabled`. Retorna `Texture`.
- [ ] **Step 5: `UBluEye::ResizeBrowser(W, H)`** vira `return ResizeBrowserScaled(W, H, Renderer ? Renderer->ScaleFactor : 1.f);`. `GetTextureIfValid`: `return bValidTexture ? Texture : nullptr;`.

- [ ] **Step 6: Grep de chamadores** de `TextureUpdate` e `ResizeBrowser`:
  ```bash
  grep -rn "TextureUpdate\|ResizeBrowser" Source
  ```
  Esperado: só `RenderHandler.cpp` chama `TextureUpdate`; `ResizeBrowser` só no próprio `BluEye`. Atualizar qualquer outro.

- [ ] **Step 7: Build + teste** (comandos 3 e 4). Esperado: `Result: Succeeded`, `Blu.Layout.ComputeSizes` passa.

- [ ] **Step 8: Commit**
  ```bash
  git add Source/Blu/Public/RenderHandler.h Source/Blu/Private/RenderHandler.cpp Source/Blu/Public/BluEye.h Source/Blu/Private/BluEye.cpp
  git commit -m "fix(blu): resize without texture leak or CEF buffer over-read, add DPI scale"
  ```

### Task 3: Widget BluBrowser (layout e desenho)

D1-D6, D11.

**Files:**
- Create: `Source/Blu/Public/BluBrowser.h`, `Source/Blu/Private/BluBrowser.cpp`
- Create: `Source/Blu/Private/SBluBrowser.h`, `Source/Blu/Private/SBluBrowser.cpp`
- Modify: `README.md` (nova seção "BluBrowser widget" logo depois da seção de instalação)

**Interfaces:**
- Consumes: `UBluEye::ResizeBrowserScaled`, `UBluEye::GetTextureIfValid` (Task 2); `BluLayout::ComputeSizes` (Task 1).
- Produces:
  ```cpp
  UCLASS(ClassGroup = Blu, meta = (DisplayName = "BLUI Browser"))
  class BLU_API UBluBrowser : public UWidget
  {
  public:
  	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blu") FString InitialURL;
  	// ViewSize here is only the desired size; the real size follows the layout
  	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blu") FBluEyeSettings Settings;
  	UFUNCTION(BlueprintPure, Category = "Blu") UBluEye* GetBluEye() const;
  protected:
  	virtual TSharedRef<SWidget> RebuildWidget() override;
  	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
  #if WITH_EDITOR
  	virtual const FText GetPaletteCategory() override;   // "BLUI"
  #endif
  	UPROPERTY(Transient) TObjectPtr<UBluEye> BluEye;
  	TSharedPtr<class SBluBrowser> SlateBrowser;
  };

  class SBluBrowser : public SLeafWidget
  {
  	SLATE_BEGIN_ARGS(SBluBrowser) {}
  		SLATE_ARGUMENT(TWeakObjectPtr<UBluEye>, BluEye)
  		SLATE_ARGUMENT(FVector2D, DesiredSize)
  	SLATE_END_ARGS()
  	void Construct(const FArguments& InArgs);
  	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
  	virtual int32 OnPaint(...) const override;
  	virtual FVector2D ComputeDesiredSize(float) const override;   // returns DesiredSize
  };
  ```

- [ ] **Step 1: `UBluBrowser::RebuildWidget`**
  - Designer (`IsDesignTime()`) ou editor fora do PIE: retorna `SNew(SBox)` centralizado com `STextBlock` "BLUI Browser". Não cria `UBluEye`.
  - Runtime: se `BluEye` é nulo, `NewObject<UBluEye>(this)`, copia `Settings` e `InitialURL` para `Settings`/`DefaultURL`, `bEnabled = true`, `Init()`. Se já existe, reaproveita (D6).
  - Cria `SlateBrowser = SNew(SBluBrowser).BluEye(BluEye).DesiredSize(Settings.ViewSize)` e retorna.
  - `ReleaseSlateResources`: `Super`, depois `SlateBrowser.Reset()`. Não chama `CloseBrowser` (o GC fecha o browser via `BeginDestroy`).

- [ ] **Step 2: `SBluBrowser::Tick`**: `ComputeSizes(AllottedGeometry.GetLocalSize(), AllottedGeometry.Scale, S)`; se `false`, não faz nada; se `S.ViewSize`/escala diferem do último aplicado, `BluEye->ResizeBrowserScaled(S.ViewSize.X, S.ViewSize.Y, AllottedGeometry.Scale)` e guarda os valores. `Tick` exige `SetCanTick(true)` no `Construct`.

- [ ] **Step 3: `SBluBrowser::OnPaint`**: pega `GetTextureIfValid()`; se nulo, retorna `LayerId` sem desenhar. Senão, `Brush.SetResourceObject(Texture)` num `mutable FSlateBrush Brush` (`DrawAs = ESlateBrushDrawType::Image`) e `FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Brush, ESlateDrawEffect::None, InWidgetStyle.GetColorAndOpacityTint())`. A textura é relida a cada frame porque o resize troca o objeto.

- [ ] **Step 4: README**: seção "BluBrowser widget" com: arrastar "BLUI Browser" da paleta (categoria BLUI); usar âncora/Fill; `Settings.ViewSize` só vale quando o slot usa "Size To Content"; `GetBluEye()` para `ExecuteJS`, `LoadURL` e eventos; mouse e teclado já são repassados (Task 4); `BluiWidget` antigo continua funcionando.

- [ ] **Step 5: Build + teste** (comandos 3 e 4). Esperado: `Result: Succeeded`, teste passa.

- [ ] **Step 6: Checklist PIE** (manual, no editor do `BLUI/BLUIHost`; usar `https://example.com` como `InitialURL`). Num Widget Blueprint adicionado ao viewport no BeginPlay:
  - [ ] Designer mostra "BLUI Browser"; Gerenciador de Tarefas não mostra processo `BLUBrowser`/CEF novo enquanto não há PIE.
  - [ ] Canvas, âncora central, alinhamento 0.5/0.5, Size To Content: página centralizada em `Settings.ViewSize`.
  - [ ] Canvas, âncora stretch total, offsets 0: página ocupa a tela; redimensionar a janela do PIE redimensiona a página (texto reflui, sem esticar depois de 1-2 frames).
  - [ ] Dentro de `Overlay` com alinhamento Fill: igual ao anterior.
  - [ ] `Visibility = Collapsed` e de volta `Visible`: sem crash, página volta.
  - [ ] Remover do parent e `AddToViewport` de novo: `LogBlu` mostra só um "Component Initialized".
  - [ ] DPI Scale do projeto com escala 1.25 e largura ímpar do widget: página aparece (nunca fica preta).
  - [ ] DPI Scale do projeto (Project Settings → User Interface) com regra que dê escala 1.5: texto nítido; `window.innerWidth` (via `GetBluEye()->ExecuteJS("console.log(window.innerWidth)")` + `LogEventEmitter`) igual à largura local do widget.
  - [ ] Abrir um `<select>` numa página com dropdown: a textura principal não fica corrompida.

- [ ] **Step 7: Commit**
  ```bash
  git add Source/Blu/Public/BluBrowser.h Source/Blu/Private/BluBrowser.cpp Source/Blu/Private/SBluBrowser.h Source/Blu/Private/SBluBrowser.cpp README.md
  git commit -m "feat(blu): add BluBrowser UMG widget that follows layout size"
  ```

### Task 4: Repasse de mouse e teclado

D10.

**Files:**
- Modify: `Source/Blu/Private/SBluBrowser.h`, `Source/Blu/Private/SBluBrowser.cpp`

**Interfaces:**
- Consumes: `UBluEye::TriggerMouseMove/TriggerLeftMouseDown/TriggerLeftMouseUp/TriggerRightMouseDown/TriggerRightMouseUp/TriggerMouseWheel(…, Scale = 1)`, `KeyDown`, `KeyUp`, `CharKeyInput` (existentes).
- Produces (overrides em `SBluBrowser`): `OnMouseMove`, `OnMouseButtonDown`, `OnMouseButtonUp`, `OnMouseWheel`, `OnKeyDown`, `OnKeyUp`, `OnKeyChar`, `SupportsKeyboardFocus() const { return true; }`.

- [ ] **Step 1: Posição**: em todo evento de mouse, `const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());` e passar `Local` com escala 1 (D3: posição local = pixel CSS). Se o `BluEye` não é válido, `FReply::Unhandled()`.

- [ ] **Step 2: Botões**: down → `Trigger{Left,Right}MouseDown`, `FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this))`. Up → `Trigger{Left,Right}MouseUp`, `FReply::Handled().ReleaseMouseCapture()`. Outros botões: `Unhandled`. Move → `TriggerMouseMove`, `Handled`. Wheel → `TriggerMouseWheel(MouseEvent.GetWheelDelta(), Local, 1.f)`, `Handled`.

- [ ] **Step 3: Teclado**: `OnKeyDown` → `KeyDown(InKeyEvent)`; `OnKeyUp` → `KeyUp(InKeyEvent)`; `OnKeyChar` → `CharKeyInput(InCharacterEvent)`; todos `Handled`.

- [ ] **Step 4: Build + teste** (comandos 3 e 4). Esperado: `Result: Succeeded`, teste passa.

- [ ] **Step 5: Checklist PIE** (mesmo Widget Blueprint da Task 3, `InitialURL` = página com link, botão e campo de texto, ex. `https://www.google.com`):
  - [ ] Widget centralizado: hover e clique caem exatamente sob o cursor.
  - [ ] Widget esticado em tela cheia e com janela redimensionada: idem.
  - [ ] Escala de DPI 1.5: idem.
  - [ ] Arrastar para selecionar texto continua funcionando com o mouse saindo do widget (captura).
  - [ ] Roda do mouse rola a página.
  - [ ] Clicar no campo e digitar: texto aparece; Backspace e setas funcionam.

- [ ] **Step 6: Commit**
  ```bash
  git add Source/Blu/Private/SBluBrowser.h Source/Blu/Private/SBluBrowser.cpp
  git commit -m "feat(blu): forward mouse and keyboard input in BluBrowser"
  ```
