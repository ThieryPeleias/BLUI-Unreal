# BLUI: layout, tamanho, render e input (relatório bruto)

Relatório do subagente de exploração, 2026-11-17. Base: `E:\@Virtuart\Claude\Projetos\BLUI` no commit `9da6439` (v5.1.0). Resumo e decisões em `../layout-research.md`.

O BLUI não tem widget Slate nem UMG em C++. O browser é um render off-screen do CEF escrito num `UTexture2D` de tamanho fixo. O Blueprint `BluiWidget` coloca essa textura num `Image` dentro de um `CanvasPanel` e ajusta o slot do canvas para o tamanho em pixels do browser. Por isso a view fica no canto superior esquerdo, em tamanho fixo.

## 1. Classes

- **`UBluEye`** (`Source/Blu/Public/BluEye.h:10`, `UObject`, não widget). Dono do browser CEF, do `RenderHandler`, do `UTexture2D* Texture` (h:258) e de um `UMaterialInstanceDynamic` opcional (h:260), criado a partir de `BaseMaterial` com o parâmetro `BluTexture`. Expõe input, JS, navegação e `ResizeBrowser`.
- **`RenderHandler : CefRenderHandler`** (`Source/Blu/Public/RenderHandler.h:9`). Guarda `int32 Width, Height` (h:14-15). Implementa `GetViewRect` e `OnPaint`.
- **`BrowserClient`** (RenderHandler.h:31). Lifespan, downloads, console e eventos JS. Nada de layout.
- **`FBluEyeSettings`** (`Source/Blu/Public/BluTypes.h:61`). `FVector2D ViewSize`, "Width(X) and Height(Y) of the view resolution" (h:72-74). Padrão 1280x720 (`BluEye.cpp:12-13`).
- **`UBluBlueprintFunctionLibrary`**. `NewBluEye` só faz `NewObject<UBluEye>` (`BluBluprintFunctionLibrary.cpp:11-18`), mais helpers de JSON e string.
- **`BluManager` / `Blu.cpp`**. Inicializa o CEF com `windowless_rendering_enabled = true` (`Blu.cpp:42`) e switches de linha de comando (`BluManager.cpp:20-44`). Nenhum switch de device scale factor.
- **`Blu.Build.cs`** depende de UMG, Slate e SlateCore, mas nenhum código C++ usa. Grep não acha `SWidget`, `UWidget`, `UUserWidget`, `UImage` nem `FSlateBrush`.

## 2. Fluxo do tamanho

A única fonte é `Settings.ViewSize`: padrão do struct, `SetProperties(SetWidth, SetHeight, …)` (`BluEye.cpp:416-417`) ou edição de `Settings` em Blueprint. Nunca vem de widget nem de geometria. Todos os usos:

- **Guard do Init:** `if (Settings.ViewSize.X <= 0 || Settings.ViewSize.Y <= 0) … return;` (cpp:46-50).
- **Tamanho no CEF:** `Renderer = new RenderHandler(Settings.ViewSize.X, Settings.ViewSize.Y, this);` (cpp:75). `GetViewRect` devolve `Rect = CefRect(0, 0, Width, Height);` (`RenderHandler.cpp:12-15`). `GetScreenInfo` e `GetScreenPoint` não são sobrescritos: device scale factor sempre 1, sem DPI.
- **Tamanho da textura:** `Texture = UTexture2D::CreateTransient(Settings.ViewSize.X, Settings.ViewSize.Y, PF_B8G8R8A8);` em `ResetTexture` (cpp:121), `ResizeBrowser` (cpp:359), `CropWindow` (cpp:393) e `GetTexture` quando a textura é nula (cpp:686, cria uma textura nova sem root a cada chamada).
- **Cópia do paint:** `OnPaint` (`RenderHandler.cpp:17-34`) transforma os dirty rects em `FUpdateTextureRegion2D`. Ignora `Type` (popup ou view) e `InWidth`/`InHeight`. `TextureUpdate` copia um buffer dimensionado pelo `Settings`, não pelo CEF:
  ```cpp
  RegionData->SrcPitch = int32(Settings.ViewSize.X) * 4;
  RegionData->SrcData.SetNumUninitialized(RegionData->SrcPitch * int32(Settings.ViewSize.Y));
  FPlatformMemory::Memcpy(RegionData->SrcData.GetData(), Buffer, RegionData->SrcData.Num());
  ```
  (cpp:187-192). Depois enfileira `UpdateTexture2D` por região na render thread (cpp:194-204).
- **Input:** não usa `ViewSize`. Coordenadas vão em pixels crus (seção 5).

## 3. Como a textura chega na tela

Não há widget C++. O único caminho são os Blueprints em `Content/`:

- `BluiWidget.uasset`
- `BluiWorldWidgetActorExample.uasset`
- `InteractableBluiWidgetActor.uasset`

Os grafos não foram abertos. O que segue vem de strings dentro dos binários.

- **`BluiWidget`**: `UUserWidget` com raiz `CanvasPanel` contendo um `Image` chamado `HtmlImage`. A função `InitBluEye(url, ViewSize)` chama `NewBluEye`, `GetTexture` e `SetBrushFromTexture` (flag `bMatchSize` presente). Também chama `SlotAsCanvasSlot` e `SetSize` num `CanvasPanelSlot` usando `ViewSize` / `GetSize`.
  - Também aparecem `GetDrawSize`, `SetWidgetDrawSize` e o comentário "draw size in settings gets overwritten".
  - Há `AnchorData` e `Offsets`, o que sugere slot de canvas padrão ancorado no canto superior esquerdo.
  - Nenhuma string de mouse, geometria, `AbsoluteToLocal` ou `OnMouse*`: o widget não repassa input.
- **`BluiWorldWidgetActorExample`**: ator com `WidgetComponent`. Tem `InitBluiWidget`, `InitBluEyeSettings`, `BluEyeSettings:ViewSize` e `SetDrawSize`. Liga o DrawSize do componente ao `ViewSize`.
- **`InteractableBluiWidgetActor`**: evento de input SpaceBar que chama `TriggerLeftClick(MakeVector2D(...))` e `RawCharKeyPress`. Só demo, sem hit-test real.
- **README.md:23-25:** "embeds a BLU texture as an image brush… Call `InitBluEye` with starting url and browser window size".
- **README.md:58-59:** "By default the actor has a BLUI resolution of 1000x1000, you can change this by just changing the user widget draw size." Refere-se ao WidgetComponent no mundo, não ao UMG em tela.
- **README.md:85,93:** "grab its material instance, then you can paint it on anything".
- `builder/README.md` só cobre build e isolamento do CEF. Nada de UMG ou resize.

Então o tamanho na tela é decidido pelo layout UMG do usuário (slot de canvas = `ViewSize`, ou DrawSize do WidgetComponent). A resolução do browser e da textura é decidida só por `ViewSize`. Nada leva a geometria real do widget de volta para `ViewSize`.

## 4. Resize que já existe

`ResizeBrowser(NewWidth, NewHeight)` devolve `UTexture2D*` e é Blueprint-callable (h:216-217, cpp:336-375):
```cpp
bEnabled = false;
Settings.ViewSize.X = NewWidth;  Settings.ViewSize.Y = NewHeight;
Renderer->Width = NewWidth;      Renderer->Height = NewHeight;
bValidTexture = false;
Texture = UTexture2D::CreateTransient(Settings.ViewSize.X, Settings.ViewSize.Y, PF_B8G8R8A8);
Texture->AddToRoot();  Texture->UpdateResource();
bValidTexture = true;
Browser->GetHost()->WasResized();
bEnabled = true;
```
Custo e lacunas:
- **Textura:** toda chamada aloca uma textura transient e um recurso RHI novos.
- **Textura antiga nunca liberada:** continua com root. `DestroyTexture` (cpp:132-165) não é chamado: cada resize vaza uma textura.
- **Material e brush não atualizados:** `ResetMatInstance` não é chamado, o material dinâmico continua apontando para a textura antiga. Um brush de `UImage` também, a menos que quem chamou reaplique o retorno (ex. `SetBrushFromTexture` de novo).
- **CEF:** um `WasResized()` por resize; o CEF consulta `GetViewRect` de novo e repinta tudo. `Invalidate` não é chamado.
- **Sem proteção de resize em andamento:** `bEnabled` desliga e liga na mesma chamada. Um `OnPaint` no tamanho antigo roda `TextureUpdate` com o pitch e altura novos do `Settings`, enquanto o buffer do CEF é do tamanho antigo (`InWidth`/`InHeight` ignorados). Pode ler além do buffer ou gravar regiões erradas.

`CropWindow` (cpp:377-405) está fora da API Blueprint ("doesn't work atm", h:219-221). Faz a mesma realocação sem `WasResized`.

Nenhum Blueprint de exemplo chama `ResizeBrowser` (não aparece nas strings dos assets).

## 5. Coordenadas de input

Todas as chamadas de mouse recebem `const FVector2D& pos, const float scale = 1` (h:99-128) e mandam `pos / scale` direto ao CEF, em pixels do browser. Exemplo cpp:431-438:
```cpp
MouseEvent.x = Pos.X / Scale;
MouseEvent.y = Pos.Y / Scale;
Browser->GetHost()->SetFocus(true);
Browser->GetHost()->SendMouseMoveEvent(MouseEvent, false);
```
Mesmo padrão em `TriggerLeft/RightMouseDown/Up` (cpp:454-484), `TriggerLeft/RightClick` (cpp:442-452) e `TriggerMouseWheel` (cpp:486-492, delta ×10).
- Coordenadas em pixels da view do CEF (0..ViewSize), não UV 0-1.
- Sem conversão automática de coordenada local ou absoluta. `Scale` é um divisor único, sem X/Y separados e sem offset.
- Quem chama converte, ex. `LocalPos * ViewSize / WidgetSize`.
- Teclado (`KeyDown`/`KeyUp`/`CharKeyInput`/`RawCharKeyPress`/`SpecialKeyPress`, cpp:494-625) não tem posição.

## 6. Por que o usuário fica preso no canto, em tamanho fixo

1. Resolução do browser e da textura vem só de `Settings.ViewSize`, digitado antes do `Init` (cpp:46, 75, 121). Nada lê geometria UMG, tamanho do viewport ou DPI.
2. `BluiWidget` põe o `Image` num slot de `CanvasPanel` e chama `SetSize(ViewSize)`, com `SetBrushFromTexture` podendo casar o tamanho da textura. Slot padrão ancorado no canto superior esquerdo com offset 0,0: a imagem sai exatamente em ViewSize pixels no canto. Não centraliza nem preenche como o `WebBrowser` do engine, que é um widget Slate de verdade e segue a geometria.
3. Nenhum widget reage a mudança de tamanho (sem checagem de geometria no `Tick`, sem override de `OnPaint`/`ComputeDesiredSize`). Âncoras ou stretch só escalariam a textura, sem re-renderizar na resolução nova.
4. O mouse espera pixels do browser com um divisor único. Qualquer stretch ou centralização no UMG exigiria offset manual e remapeamento por eixo, e o widget distribuído não faz nada disso.
5. `ResizeBrowser` existe mas não está ligado a nenhum widget. Vaza a textura antiga e não atualiza material nem brush, então mesmo chamando à mão é preciso reaplicar a textura devolvida.

## Não verificado

Grafos dos três `.uasset` (âncoras, ligações exatas e se `bMatchSize` é true vêm de strings do binário). Headers do CEF em `ThirdParty` e `BluJsonObj` não foram lidos (irrelevantes).
