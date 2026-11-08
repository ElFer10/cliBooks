#include "app/i18n.hpp"

#include <cstdlib>
#include <unordered_map>

#include "util/strings.hpp"

namespace clibooks {
namespace {

std::string g_lang = "en";

using Table = std::unordered_map<std::string_view, const char*>;

const Table& spanish() {
    static const Table t{
        {"page", "Página"},
        {"of", "de"},
        {"pages_left_chapter", "pág. restantes en el capítulo"},
        {"last_page_chapter", "Última página del capítulo"},
        {"contents", "Contenido"},
        {"bookmarks", "Marcadores"},
        {"annotations", "Resaltados y notas"},
        {"search_results", "Resultados de búsqueda"},
        {"library", "Biblioteca"},
        {"themes", "Temas"},
        {"help", "Ayuda"},
        {"no_bookmarks", "Sin marcadores"},
        {"no_annotations", "Sin resaltados ni notas"},
        {"no_results", "Sin resultados"},
        {"no_toc", "Este libro no tiene tabla de contenidos"},
        {"library_empty", "La biblioteca está vacía. Abre un libro con :open <ruta> o configura library_dirs."},
        {"bookmark_added", "Marcador añadido"},
        {"bookmark_removed", "Marcador eliminado"},
        {"highlight_added", "Resaltado añadido"},
        {"highlight_removed", "Resaltado eliminado"},
        {"note_saved", "Nota guardada"},
        {"copied", "Copiado al portapapeles"},
        {"pattern_not_found", "Patrón no encontrado"},
        {"search_wrapped", "La búsqueda continuó desde el principio"},
        {"unknown_command", "Comando desconocido"},
        {"no_links", "No hay enlaces en esta página"},
        {"external_link", "Enlace externo (Enter abre, Esc cancela)"},
        {"footnote", "Nota"},
        {"note_prompt", "Nota: "},
        {"visual", "-- VISUAL --"},
        {"hints", "-- ENLACES --"},
        {"loading", "Cargando…"},
        {"error_open", "No se pudo abrir el libro"},
        {"speaking", "Leyendo en voz alta…"},
        {"speech_stopped", "Lectura detenida"},
        {"no_tts", "No hay comando de voz configurado (tts_command)"},
        {"no_dict", "No hay diccionario configurado (dict_command)"},
        {"exported", "Notas exportadas a"},
        {"settings_saved", "Preferencias guardadas"},
        {"beginning", "Inicio del libro"},
        {"end", "Fin del libro"},
        {"time_left", "restante"},
        {"author", "Autor"},
        {"publisher", "Editorial"},
        {"language", "Idioma"},
        {"identifier", "Identificador"},
        {"chapter", "Capítulo"},
        {"progress", "Progreso"},
        {"images", "Imágenes"},
        {"file", "Archivo"},
        {"filter", "Filtro: "},
        {"delete_confirm", "¿Eliminar? (y/n)"},
        {"opened", "Abierto"},
        {"never", "nunca"},
        {"print_page", "pág. impresa"},
        {"no_page_list", "Este libro no incluye números de página impresa"},
        {"jump_back", "Volver"},
        {"press_any_key", "Pulsa cualquier tecla"},
        {"color_yellow", "amarillo"},
        {"color_green", "verde"},
        {"color_blue", "azul"},
        {"color_pink", "rosa"},
        {"color_purple", "morado"},
        {"color_underline", "subrayado"},
        {"definition", "Definición"},
        {"info", "Información"},
    };
    return t;
}

const Table& english() {
    static const Table t{
        {"page", "Page"},
        {"of", "of"},
        {"pages_left_chapter", "pages left in chapter"},
        {"last_page_chapter", "Last page in this chapter"},
        {"contents", "Contents"},
        {"bookmarks", "Bookmarks"},
        {"annotations", "Highlights & Notes"},
        {"search_results", "Search results"},
        {"library", "Library"},
        {"themes", "Themes"},
        {"help", "Help"},
        {"no_bookmarks", "No Bookmarks"},
        {"no_annotations", "No Highlights or Notes"},
        {"no_results", "No results"},
        {"no_toc", "This book has no table of contents"},
        {"library_empty", "The library is empty. Open a book with :open <path> or set library_dirs."},
        {"bookmark_added", "Bookmark added"},
        {"bookmark_removed", "Bookmark removed"},
        {"highlight_added", "Highlight added"},
        {"highlight_removed", "Highlight removed"},
        {"note_saved", "Note saved"},
        {"copied", "Copied to clipboard"},
        {"pattern_not_found", "Pattern not found"},
        {"search_wrapped", "Search wrapped around"},
        {"unknown_command", "Unknown command"},
        {"no_links", "No links on this page"},
        {"external_link", "External link (Enter opens, Esc cancels)"},
        {"footnote", "Footnote"},
        {"note_prompt", "Note: "},
        {"visual", "-- VISUAL --"},
        {"hints", "-- LINKS --"},
        {"loading", "Loading…"},
        {"error_open", "Could not open the book"},
        {"speaking", "Reading aloud…"},
        {"speech_stopped", "Speech stopped"},
        {"no_tts", "No speech command configured (tts_command)"},
        {"no_dict", "No dictionary command configured (dict_command)"},
        {"exported", "Notes exported to"},
        {"settings_saved", "Preferences saved"},
        {"beginning", "Beginning of book"},
        {"end", "End of book"},
        {"time_left", "left"},
        {"author", "Author"},
        {"publisher", "Publisher"},
        {"language", "Language"},
        {"identifier", "Identifier"},
        {"chapter", "Chapter"},
        {"progress", "Progress"},
        {"images", "Images"},
        {"file", "File"},
        {"filter", "Filter: "},
        {"delete_confirm", "Delete? (y/n)"},
        {"opened", "Opened"},
        {"never", "never"},
        {"print_page", "print p."},
        {"no_page_list", "This book has no print page numbers"},
        {"jump_back", "Back"},
        {"press_any_key", "Press any key"},
        {"color_yellow", "yellow"},
        {"color_green", "green"},
        {"color_blue", "blue"},
        {"color_pink", "pink"},
        {"color_purple", "purple"},
        {"color_underline", "underline"},
        {"definition", "Definition"},
        {"info", "Information"},
    };
    return t;
}

}  // namespace

void setLanguage(const std::string& lang) {
    std::string l = str::lower(lang);
    if (l == "auto" || l.empty()) {
        const char* vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
        l = "en";
        for (const char* v : vars) {
            const char* val = std::getenv(v);
            if (val && *val) {
                l = str::lower(val);
                break;
            }
        }
    }
    g_lang = str::startsWith(l, "es") ? "es" : "en";
}

std::string currentLanguage() { return g_lang; }

const char* tr(std::string_view key) {
    const Table& t = g_lang == "es" ? spanish() : english();
    if (auto it = t.find(key); it != t.end()) return it->second;
    if (auto it = english().find(key); it != english().end()) return it->second;
    return key.data();
}

}  // namespace clibooks
