# Qué puede y qué no puede hacer cliBooks (frente a Apple Books)

## ✅ Lo que hace

**Formato y lectura**
- EPUB 2 y EPUB 3, comprimidos (`.epub`) o desempaquetados (carpeta).
- Índice desde `nav.xhtml` (EPUB 3) o `toc.ncx` (EPUB 2); si no hay, lo genera con los títulos.
- Números de página impresa (`page-list` / `pageList`).
- Metadatos: título, autores, idioma, editorial, fecha, descripción, portada.
- Detección de DRM (FairPlay, Adobe) con mensaje claro.

**Tipografía en terminal**
- Negrita, cursiva (si el terminal la soporta), subrayado, código, versalitas (→ mayúsculas),
  superíndices y subíndices numéricos con caracteres Unicode (¹²³, ₂).
- Justificado con reparto equilibrado de espacios, alineación centrada/derecha del CSS.
- Guionado automático con patrones Liang (diccionarios hunspell) y guiones blandos del libro.
- Párrafos con sangría o separados según el CSS del libro (`paragraphs = "auto"`), o forzados.
- Interlineado 1 o 2, ancho de columna ajustable, márgenes superior/inferior.
- Encabezados, listas anidadas numeradas/viñetas, citas con barra lateral, `pre` respetando
  espacios, separadores `<hr>`, tablas con bordes (o apiladas si no caben), ruby (`漢字(かんじ)`),
  MathML mediante `alttext` o texto plano.
- CJK y emoji con anchura doble correcta.
- Subconjunto de CSS: `font-weight`, `font-style`, `text-decoration`, `text-align`, `display:none`,
  `font-variant: small-caps`, `text-transform: uppercase`, `vertical-align`, `margin-left`,
  `text-indent`, `page-break-before`, `white-space: pre`. Se puede desactivar (`bookstyles`).

**Imágenes**
- PNG, JPEG, GIF (primer fotograma), BMP, TGA.
- Kitty graphics, iTerm2 inline images, Sixel o bloques Unicode con color de 24 bits/256.
- Escaladas al ancho de columna y al alto de página, sin partirse entre páginas,
  recorte correcto en modo scroll.

**Navegación (vim)**
- Paginado o scroll vertical continuo entre capítulos; doble página en terminales anchas.
- Conteos (`5j`, `3]]`, `120G`, `40%`), secuencias (`gg`, `]]`), historial de saltos.
- Índice, marcadores, resaltados/notas, resultados de búsqueda en listas filtrables.
- Enlaces internos, notas al pie en ventana emergente, enlaces externos abiertos en el navegador.
- Búsqueda en todo el libro sin distinguir mayúsculas ni tildes (smartcase), con resaltado.

**Anotaciones**
- Modo visual con movimientos vim; resaltado en 5 colores + subrayado, notas, fusión de
  resaltados solapados, copiar (portapapeles + OSC 52), exportar a Markdown.

**Otros**
- Temas Original/Quiet/Paper/Bold/Calm/Focus/Sepia/Night y temas propios en TOML.
- Atajos 100 % configurables por modo; interfaz en español o inglés.
- Persistencia automática de posición, progreso, tiempo de lectura y anotaciones.
- Biblioteca con libros recientes y carpetas escaneadas.
- Diccionario (`sdcv`, `dict`… o Diccionario.app en macOS) y lectura en voz alta (`say`, `espeak-ng`).

## ⚠️ Lo que hace de forma aproximada

| Función de Books | Aproximación en cliBooks |
|---|---|
| Elegir fuente y tamaño | La fuente la decide el terminal; el “tamaño” se emula con el ancho de columna. |
| Colores exactos de tema | En terminales de 256 colores se usa el color más cercano. |
| Tablas complejas | Sin `colspan`/`rowspan`; tablas muy anchas se muestran apiladas. |
| Imágenes en tmux/screen | Se usan bloques Unicode (menos resolución). |
| Picture books / maquetación fija | Se muestran las imágenes y el texto en flujo, no la composición original. |
| CSS | Solo el subconjunto anterior; selectores evaluados por su último compuesto. |
| Paginación | Número de páginas propio, depende del tamaño de la terminal (como Books depende de la ventana). |
| “Consultar” (Look Up) | Comando externo o Diccionario.app; sin panel integrado de Wikipedia/web. |
| Leer en voz alta | Lee la página/selección actual con TTS del sistema; sin resaltar palabra a palabra. |

## ❌ Lo que no puede hacer (descartado)

- **Libros con DRM** (comprados en Apple Books con FairPlay, Adobe ADEPT): ilegibles por diseño.
- **Fuentes incrustadas, kerning, ligaduras, tamaños de letra mixtos**: imposible en una rejilla de celdas.
- **Animaciones de pasar página (page curl), brillo de pantalla**.
- **JavaScript en EPUB, contenido interactivo, widgets de iBooks Author (.ibooks)**.
- **Audio y vídeo incrustados, EPUB Media Overlays (narración sincronizada)**: solo se indica `[audio]` / `[video]`.
- **SVG vectorial** (solo se muestran las `<image>` dentro de un SVG) y **MathML renderizado**.
- **Escritura vertical (japonés tategaki) y maquetación RTL completa** (el texto árabe/hebreo
  depende del soporte bidi del terminal).
- **PDF y audiolibros** (Books los abre; cliBooks solo EPUB).
- **Tienda, sincronización con iCloud, colecciones, objetivos de lectura, “Want to Read”**.
- **Miniaturas de páginas en la barra de desplazamiento**.
- **Zoom/ampliar imagen a pantalla completa** con gestos (posible mejora futura con un visor dedicado).
