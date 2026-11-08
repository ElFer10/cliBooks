#!/usr/bin/env python3
"""Builds tests/fixtures/sample.epub: a small EPUB 3 exercising the features cliBooks
supports (TOC, footnotes, images, tables, lists, quotes, code, links, CSS).
Usage: python3 tests/make_sample_epub.py [output.epub]"""
import struct
import sys
import zipfile
import zlib
from pathlib import Path


def png(width, height, pixel):
    raw = b""
    for y in range(height):
        raw += b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def gradient(x, y):
    return (int(255 * x / 199), int(255 * y / 119), 160)


def rings(x, y):
    d = ((x - 60) ** 2 + (y - 60) ** 2) ** 0.5
    return (30, 90, 200) if int(d / 8) % 2 else (240, 200, 60)


XHTML = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="es">
<head><title>{title}</title><link rel="stylesheet" type="text/css" href="../css/style.css"/></head>
<body>{body}</body></html>"""

CH1 = """
<section epub:type="chapter">
<h1 id="c1">Capítulo 1. El faro</h1>
<p class="first">El faro de la isla llevaba años apagado cuando Marta llegó con una maleta, una lámpara de queroseno y
una libreta llena de <em>preguntas</em> que nadie había querido contestar. La niebla subía por los acantilados como
una marea lenta, y el viento traía olor a sal y a madera mojada.</p>
<p>Subió los ciento doce peldaños contándolos en voz baja. Arriba, la linterna seguía en su sitio, cubierta de polvo
y de silencio. <strong>Nadie la había tocado</strong> desde la última tormenta<a epub:type="noteref" href="#n1">1</a>.</p>
<p>Durante la primera semana anotó cada ruido: el crujido de las vigas, las gaviotas, el golpe sordo de las olas contra
las rocas. Descubrió que el mecanismo de relojería estaba intacto y que solo faltaba una pieza, una rueda dentada del
tamaño de una moneda, sin la cual la luz no podía girar<a epub:type="noteref" href="#n2">2</a>.</p>
<aside epub:type="footnote" id="n1"><p>La tormenta de 1987, que según los registros del puerto duró tres días.</p></aside>
<aside epub:type="footnote" id="n2"><p>Los antiguos fareros llamaban a esta pieza «la corona».</p></aside>
<figure><img src="../images/gradient.png" alt="Atardecer sobre el mar"/><figcaption>Figura 1. El horizonte desde la galería.</figcaption></figure>
<p>Por las noches leía el diario del último farero. Las entradas eran breves y precisas, escritas con una caligrafía
apretada que se volvía más irregular a medida que avanzaban los meses. Hablaba del clima, de los barcos que pasaban y,
cada vez más a menudo, de una luz que veía en el agua y que no correspondía a ninguna embarcación conocida.</p>
<blockquote><p>«Esta noche la luz ha vuelto. Verde, baja, inmóvil. He apagado la linterna para verla mejor y ha
desaparecido. Mañana la encenderé de nuevo.»</p></blockquote>
<p>Marta cerró el diario y miró por la ventana. La niebla se había disipado. Muy lejos, sobre el agua negra, algo brillaba.
Continúa en el <a href="ch2.xhtml#c2">capítulo 2</a>.</p>
</section>"""

CH2 = """
<section epub:type="chapter">
<h1 id="c2">Capítulo 2. Inventario</h1>
<p>A la mañana siguiente hizo inventario de todo lo que había en el faro:</p>
<ul>
<li>Tres cajas de herramientas, una de ellas sin abrir.</li>
<li>Un barómetro de mercurio que todavía funcionaba.</li>
<li>Planos del mecanismo:
  <ol><li>alzado general,</li><li>detalle del engranaje,</li><li>esquema de la lente.</li></ol></li>
<li>Una radio de onda corta.</li>
</ul>
<h2 id="tabla">Registro de mareas</h2>
<table>
<tr><th>Día</th><th>Pleamar</th><th>Bajamar</th><th>Observaciones</th></tr>
<tr><td>Lunes</td><td>06:12</td><td>12:30</td><td>Niebla densa hasta el mediodía</td></tr>
<tr><td>Martes</td><td>06:58</td><td>13:15</td><td>Viento del norte, mar rizada</td></tr>
<tr><td>Miércoles</td><td>07:41</td><td>13:59</td><td>Luz verde a las 02:00</td></tr>
</table>
<p class="center">* * *</p>
<p>El esquema del mecanismo incluía, en una esquina, unas instrucciones escritas a máquina:</p>
<pre><code>1. Retirar la tapa lateral.
2. Comprobar   la tensión del muelle.
3. Insertar la corona en el eje B.</code></pre>
<p>Marta buscó en las cajas durante horas. Encontró tornillos, arandelas y una llave inglesa oxidada, pero ninguna
corona. Al final de la tarde se sentó en la escalera, cansada, y entonces la vio: sujeta con cinta adhesiva bajo el
último peldaño, envuelta en un trozo de tela encerada.</p>
<p><img src="../images/rings.png" alt="La corona"/></p>
<p>Más información sobre faros en <a href="https://example.org/faros">example.org</a>. Superíndices: E = mc<sup>2</sup>,
H<sub>2</sub>O.</p>
<hr/>
<p>Fin del relato de prueba.</p>
</section>"""

NAV = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Índice</title></head>
<body><nav epub:type="toc"><ol>
<li><a href="text/ch1.xhtml#c1">Capítulo 1. El faro</a></li>
<li><a href="text/ch2.xhtml#c2">Capítulo 2. Inventario</a><ol><li><a href="text/ch2.xhtml#tabla">Registro de mareas</a></li></ol></li>
</ol></nav>
<nav epub:type="page-list"><ol><li><a href="text/ch1.xhtml#c1">1</a></li><li><a href="text/ch2.xhtml#c2">9</a></li></ol></nav>
</body></html>"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="uid">urn:uuid:clibooks-sample-0001</dc:identifier>
<dc:title>El faro (EPUB de prueba)</dc:title><dc:creator>cliBooks</dc:creator><dc:language>es</dc:language>
<meta property="dcterms:modified">2026-11-07T00:00:00Z</meta>
</metadata>
<manifest>
<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
<item id="css" href="css/style.css" media-type="text/css"/>
<item id="ch1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
<item id="ch2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
<item id="img1" href="images/gradient.png" media-type="image/png" properties="cover-image"/>
<item id="img2" href="images/rings.png" media-type="image/png"/>
</manifest>
<spine><itemref idref="ch1"/><itemref idref="ch2"/></spine>
</package>"""

CSS = """p { text-indent: 1.5em; margin: 0; text-align: justify; }
p.first { text-indent: 0; }
p.center { text-align: center; text-indent: 0; }
h1 { text-align: center; }
figcaption { font-style: italic; }
"""


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "fixtures" / "sample.epub"
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>""")
        z.writestr("OEBPS/content.opf", OPF)
        z.writestr("OEBPS/nav.xhtml", NAV)
        z.writestr("OEBPS/css/style.css", CSS)
        z.writestr("OEBPS/text/ch1.xhtml", XHTML.format(title="Capítulo 1", body=CH1))
        z.writestr("OEBPS/text/ch2.xhtml", XHTML.format(title="Capítulo 2", body=CH2))
        z.writestr("OEBPS/images/gradient.png", png(200, 120, gradient))
        z.writestr("OEBPS/images/rings.png", png(120, 120, rings))
    print(out)


if __name__ == "__main__":
    main()
