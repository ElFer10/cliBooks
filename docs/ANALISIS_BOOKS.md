# Análisis de Apple Books (macOS) con REA

Versión analizada: Books 7.6 (build 6387), `com.apple.iBooksX`, `/System/Applications/Books.app`.
Herramientas: REA (`open_bundle_resources`, `inspect_plist`, `open_binary` + `get_objc_classes`
sobre `AEBookPlugins.framework`) y lectura de recursos del bundle (hojas de estilo, plist,
tablas de localización).

## Arquitectura

Books es una app Catalyst (`UIUserInterfaceIdiomMac`) dividida en frameworks:

| Framework | Papel |
|---|---|
| `AEBookPlugins` | el lector: plugins EPUB / PDF / audiolibro / picture book |
| `BookEPUB` | modelo EPUB, temas (`BookTheme.momd`), caché de paginación (`PaginationCache.momd`) |
| `BookCore` | anotaciones (`AEAnnotation`, `AEHighlight`, editores de notas), localizaciones |
| `BKLibrary`, `BooksUI`, `BookStoreUI` | biblioteca, UI general, tienda |
| `BKAudiobooks` | audiolibros |

El contenido EPUB se renderiza con **WebKit** (`BKWK2WebViewLoader`, `BookEPUBWebProcessPlugin.bundle`),
y la apariencia se controla inyectando hojas de estilo de usuario generadas a partir de plantillas
(`user_stylesheet_*.css.tmpl`, motor propio `AEMinimalTemplate`).

## Funciones del lector identificadas (clases ObjC en AEBookPlugins)

- **Paginación**: `BKPaginationController`, `BKPaginationJob/BatchJob/LayoutOperation`,
  `BKFlowingBookLayoutConfiguration` — pagina todo el libro en segundo plano y guarda una caché.
- **Modos de lectura**: `BKPageCurl*` (pasar página), `BKPageScrollerViewController`,
  “Vertical Scrolling” / “Horizontal Scrolling” (cadenas localizadas).
- **Ubicación**: `BKEpubCFILocation`, `BKAnchorLocation`, `BKPageLocation`, `BKNavigationHistory`
  (“Back to page %@”).
- **Índice**: `BKTOCViewController`, `BKTOCWebViewController2`, `BKTOCBookmarksDescription`.
- **Marcadores**: “Bookmark Page”, `BKBookmarkThumbnailDirectory`.
- **Resaltados y notas**: `AEHighlight`, `AEHighlightColorEditorController`,
  `AENotePopoverEditorController`; colores amarillo, verde, azul, rosa, morado y subrayado.
- **Búsqueda**: `BKSearchController`, `BKWK2EpubSearchOperation`, `BKTextIndex`.
- **Notas al pie**: `BKFootnoteViewController2` + plantilla `footnote.xhtml` (ventana emergente).
- **Imágenes**: `BKExpandedImageContentViewController` (ampliar), `BKImageResizerHelper`.
- **Apariencia**: `BKAppearanceViewController`, `BKAppearanceFontViewController`, `BKStyleManager`,
  `BKFontCache`; `FontPresets*.plist` con 12 tamaños (1.0×–6.7×) y fuentes por idioma.
- **Lectura en voz alta**: `BKAudioController`, `BKMediaOverlayElement` (EPUB Media Overlays),
  `BKReadAloudEventHandler`.
- **Números de página impresa**: `BKPhysicalPage`, `BKPhysicalPageCount`, `BKLandmarkInfo`.
- **Otros**: `BKBrightnessController`, `BKThumbnailScrubber` (barra de páginas con miniaturas),
  `BKFinishedContentViewController` (pantalla de libro terminado), “Look Up”, “Search Web”.

## Hojas de estilo inyectadas (lo que Books impone al libro)

- `user_stylesheet_fonts.css.tmpl`: `text-align` (justificar), `-webkit-hyphens`
  con límites 3/3/2 líneas, familia tipográfica forzada con fallbacks, `a { text-decoration:none }`.
- `user_stylesheet_flowable.css.tmpl`: `p { widows: 2; orphans: 2 }`, imágenes
  `max-height: 95%; max-width: 100%; page-break-inside: avoid`, oculta
  `aside[epub|type~="footnote"]`, márgenes según modo (scroll/páginas).
- `user_stylesheet_colors.css.tmpl`: temas que fuerzan color de texto/fondo y enlaces,
  invirtiendo contenido en temas oscuros.
- Temas (BookEPUB): **Original, Quiet, Paper, Bold, Calm, Focus** (+ “Customized”),
  “Auto-Night Theme”.

## Cómo se trasladó cada idea a cliBooks

| Books | cliBooks |
|---|---|
| WebKit + CSS inyectado | parser XHTML propio + subconjunto CSS + motor de maquetación en rejilla |
| Paginación en segundo plano | maquetación completa al abrir (≤0,1 s en libros de 2000 páginas, build Release) |
| widows/orphans 2, imágenes sin partir | mismas reglas en `paginate()` |
| aside footnote oculto + popover | idem: oculto en el flujo y mostrado en ventana emergente con `f` |
| CFI / anchor locations | `Position {capítulo, offset de bytes}` estable ante cambios de ancho |
| Temas Original/Quiet/Paper/Bold/Calm/Focus | mismos nombres (+ sepia, night); Original respeta los colores del terminal |
| 12 tamaños de letra | ancho de columna `+`/`-` (la letra la decide el terminal) |
| Justificar / guiones | justificado propio + guionado Liang con diccionarios hunspell |
| Back to page | `Ctrl-o` / `Tab` |
| Páginas restantes en el capítulo | barra de estado |
| Page list impresa | `(pág. impresa N)` y `:page N` |
| Look Up / Speak | `K` (dict o Diccionario.app) / `gr` (say, espeak-ng) |
| Compartir notas | `:export` a Markdown |
