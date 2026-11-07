# cliBooks

Lector de EPUB para la terminal, escrito en C++ moderno con ncurses, atajos
estilo VIM y soporte de imágenes en terminal.

## Compilar

Requisitos: CMake ≥ 3.20, un compilador C++20/23 (clang ≥ 15, gcc ≥ 11) y ncurses con
soporte wide-char (`ncursesw`). El resto de dependencias va incluido en `third_party/`
(miniz, pugixml, toml++, stb_image), así que compila sin conexión.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
./build/clipub libro.epub
```

Dependencias del sistema:

- **macOS**: nada (usa el ncurses del sistema) o `brew install ncurses`.
- **Debian/Ubuntu**: `sudo apt install build-essential cmake libncurses-dev pkg-config`.
- **Fedora**: `sudo dnf install gcc-c++ cmake ncurses-devel`.
- **Arch**: `sudo pacman -S base-devel cmake ncurses`.
- **Windows** (No lo he probado): MSYS2 con `mingw-w64-ucrt-x86_64-ncurses`, o PDCurses
  pasando `-DCURSES_INCLUDE_PATH=… -DCURSES_LIBRARY=…`. Imágenes: sixel en Windows Terminal ≥ 1.22.

Tests: `python3 tests/make_sample_epub.py && ./build/clipub_tests`. El script genera
`tests/fixtures/sample.epub`, un EPUB 3 de prueba con índice, notas al pie, imágenes, tabla,
listas, cita y código.

## Uso

```bash
clipub                      # biblioteca (libros recientes + carpetas de library_dirs)
clipub libro.epub           # abre el libro en la última posición leída
clipub carpeta-epub/        # también EPUB desempaquetados
clipub -t night -w 80 libro.epub
clipub --check libro.epub   # estadísticas de parseo/maquetación
clipub --dump libro.epub    # texto maquetado a stdout
clipub --write-config       # crea ~/.config/clipub/config.toml de ejemplo
```

## Atajos por defecto (vim)

Pulsa `g?` o `F1` dentro de la app para ver la tabla generada a partir de tu configuración.

| Lectura                           |                                                                         |
| --------------------------------- | ----------------------------------------------------------------------- |
| `Espacio` `l` `→` `Ctrl-f` `PgDn` | página siguiente                                                        |
| `b` `h` `←` `Ctrl-b` `PgUp` `BS`  | página anterior                                                         |
| `j` / `k`                         | línea abajo/arriba (en modo paginado, pasan página)                     |
| `Ctrl-d` / `Ctrl-u`               | media página                                                            |
| `]]` `}` / `[[` `{`               | capítulo siguiente / anterior                                           |
| `gg` / `G`, `{n}G`, `{n}%`        | inicio / final, página _n_, porcentaje _n_                              |
| `Ctrl-o` / `Tab`                  | atrás / adelante en el historial de saltos                              |
| `t`                               | índice (TOC)                                                            |
| `m`                               | alternar marcador en la página (★ en la cabecera)                       |
| `'` `M`                           | lista de marcadores                                                     |
| `A`                               | resaltados y notas · `]a` / `[a` siguiente / anterior                   |
| `/` `?` `n` `N`                   | buscar (sin distinguir mayúsculas ni tildes) · `g/` lista de resultados |
| `f`                               | etiquetas sobre los enlaces visibles; notas al pie en ventana emergente |
| `v`                               | modo visual (selección)                                                 |
| `C` / `gt`                        | siguiente tema / selector de temas                                      |
| `S`                               | alternar paginado ↔ scroll vertical                                     |
| `+` `-` `=`                       | ancho de columna (equivalente al tamaño de letra)                       |
| `J`                               | justificar sí/no                                                        |
| `gr`                              | leer en voz alta la página (otra vez para parar)                        |
| `Ctrl-g`                          | información del libro y del progreso                                    |
| `gl`                              | biblioteca                                                              |
| `:`                               | línea de comandos · `q` `ZZ` `:q` salir                                 |

| Visual                      |                                                              |
| --------------------------- | ------------------------------------------------------------ |
| `h j k l` `w b e` `0 $`     | mover el cursor (pasa de página automáticamente)             |
| `o`                         | cambiar el extremo de la selección                           |
| `H` `Enter` / `1`–`5` / `U` | resaltar (amarillo / verde, azul, rosa, morado / subrayado)  |
| `a` `n`                     | resaltar y añadir nota                                       |
| `y`                         | copiar (portapapeles del sistema + OSC 52, funciona por SSH) |
| `K`                         | diccionario · `gr` leer selección · `*` buscar selección     |

| Listas (TOC, marcadores, notas, biblioteca) |                                        |
| ------------------------------------------- | -------------------------------------- |
| `j k` `gg G` `Ctrl-d Ctrl-u`                | moverse                                |
| `Enter` `l` `o`                             | abrir / ir                             |
| `/`                                         | filtrar en vivo                        |
| `d` `x`                                     | borrar (pide confirmación)             |
| `e` / `c`                                   | editar nota / cambiar color (en notas) |
| `q` `Esc` `h`                               | cerrar                                 |

### Comandos

```
:q :q! :wq :x            salir (el progreso se guarda siempre)
:open <ruta>  :e <ruta>  abrir (Tab completa rutas)
:library                 biblioteca
:toc :bookmarks :notes   paneles
:theme <nombre> :themes  temas
:set width=80 justify! hyphenate! mode=scroll|paged columns=1|2|auto
:set paragraphs=auto|spaced|indented linespacing=1|2 images=kitty|iterm|sixel|blocks|none
:set header! status! bookstyles! lang=es|en
:123  :goto 45%          ir a página / porcentaje
:page <etiqueta>         número de página impresa (si el EPUB trae page-list)
:chapter <n>             capítulo n
:search <texto>          lista de resultados
:noh                     quitar el resaltado de búsqueda
:export [archivo.md]     exportar resaltados, notas y marcadores a Markdown
:define <palabra>        diccionario
:speak :stop             voz
:info :reload :reset :redraw :w
```

## Configuración

`~/.config/clipub/config.toml` (o `$XDG_CONFIG_HOME/clipub/config.toml`). Ver
[config.example.toml](config.example.toml). Lo que cambias dentro de la app (`:set`, tema,
ancho) se recuerda en `~/.local/share/clipub/prefs.toml` y tiene prioridad; `:reset` lo borra.

Atajos: cada acción admite una tecla o una lista, en notación vim. Asignar una tecla la quita
de cualquier otra acción del mismo modo; `unmap` elimina atajos.

```toml
[keys.reader]
page_down = ["<Space>", "l", "<C-f>"]
toc = ["<F2>", "gt"]
unmap = ["C"]

[keys.visual]
highlight = ["H", "<CR>"]
```

Nombres de acciones: `scroll_down scroll_up page_down page_up half_page_down half_page_up
next_chapter prev_chapter first last goto_percent back forward toc bookmarks annotations library
help info themes search_results toggle_bookmark search_forward search_backward search_next
search_prev command quit visual follow_link theme_next toggle_mode width_increase width_decrease
width_reset toggle_justify redraw speak next_annotation prev_annotation` (lectura);
`char_left char_right line_down line_up word_next word_back word_end line_start line_end
swap_anchor yank highlight color_1…color_5 underline add_note look_up speak_selection
search_selection exit` (visual); `line_down line_up half_page_down half_page_up first last select
close delete edit change_color filter command` (listas).

### Imágenes

`images = "auto"` detecta: **Kitty** (kitty, Ghostty, WezTerm, Konsole), **iTerm2** (iTerm2),
**Sixel** (foot, mlterm, Windows Terminal, VS Code) y en el resto **bloques Unicode** (▀ con
color de 24 bits o 256 colores), que funcionan en cualquier terminal y dentro de tmux.

### Guionado

Como `-webkit-hyphens: auto` de Books, se usan patrones Liang/TeX en el formato de
LibreOffice/hunspell (`hyph_es.dic`, `hyph_en_US.dic`…). Se buscan en
`~/.local/share/clipub/hyphen/`, `/usr/share/hyphen/` (paquete `hyphen-es`, `hyphen-en-us`…
en Debian/Ubuntu) y en las extensiones de LibreOffice. En macOS no vienen de serie: copia el
`.dic` del repositorio [LibreOffice/dictionaries](https://github.com/LibreOffice/dictionaries)
a `~/.local/share/clipub/hyphen/`. Sin diccionario se usan solo los guiones blandos (`&shy;`)
del propio libro.

### Diccionario y voz

- `dict_command` (p. ej. `sdcv -n --utf8-output %s`, `dict %s`). En macOS, sin comando
  configurado, `K` abre la palabra en Diccionario.app como hace “Consultar” en Books.
- `tts_command` recibe el texto por stdin: `say -f -` (macOS), `espeak-ng --stdin` (Linux).

## Datos

- `~/.local/share/clipub/books/<id>.toml`: posición, progreso, tiempo de lectura, marcadores,
  resaltados y notas de cada libro (texto plano, fácil de respaldar o sincronizar).
- `~/.local/share/clipub/prefs.toml`: preferencias cambiadas en la app.

## Estructura

```
src/util     UTF-8 (anchura, plegado de tildes), cadenas, plataforma (rutas XDG, portapapeles, procesos)
src/epub     ZIP/carpeta, OPF, nav/NCX, page-list, DRM, CSS mínimo, XHTML → bloques
src/layout   ajuste de línea, justificado, guionado Liang, listas, tablas, imágenes, paginación
src/image    stb_image + codificadores Kitty / iTerm2 / Sixel / bloques
src/app      configuración TOML, temas, keymap vim, persistencia, i18n (es/en)
src/ui       ncurses: terminal, lector (paginado/scroll/doble página), aplicación y comandos
```
