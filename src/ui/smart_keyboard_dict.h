#pragma once
/* MoonX360 Smart Virtual Keyboard — Offline Multi-Language Autocomplete Engine.
 * Zero heap allocations, deterministic prefix + accent-folding matching,
 * frequency-ordered PT-BR / EN / ES dictionaries, next-word bigrams, and
 * bounded opt-in local learned-word history. All UTF-8 bytes are explicit. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SK_MAX_SUGGESTIONS   3
#define SK_WORD_MAX_BYTES    40
#define SK_USER_DICT_MAX     32

enum sk_lang { SK_LANG_EN = 0, SK_LANG_PT_BR = 1, SK_LANG_ES = 2, SK_LANG_COUNT = 3 };

struct sk_suggestions {
    char items[SK_MAX_SUGGESTIONS][SK_WORD_MAX_BYTES];
    int count;
};

struct sk_user_word {
    char word[SK_WORD_MAX_BYTES];
    uint16_t uses;
    bool used;
};

struct sk_user_dict {
    struct sk_user_word entries[SK_USER_DICT_MAX];
    bool enabled;
};

struct sk_bigram {
    const char *prev;
    const char *next[3];
};

/* Accent-folding ASCII normalizer for UTF-8 Spanish/Portuguese/English text.
 * Maps uppercase ASCII and common Latin-1 accented UTF-8 sequences to lowercase ASCII. */
static int sk_fold_utf8(const char *src, char *out, int cap)
{
    int n = 0;
    if (!src || !out || cap <= 0) return 0;
    while (*src && n + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (c >= 'A' && c <= 'Z') {
            out[n++] = (char)(c + ('a' - 'A'));
        } else if (c == 0xC3 && *src) {
            unsigned char b = (unsigned char)*src++;
            char folded = 0;
            if ((b >= 0x80 && b <= 0x85) || (b >= 0xA0 && b <= 0xA5)) folded = 'a';
            else if (b == 0x87 || b == 0xA7) folded = 'c';
            else if ((b >= 0x88 && b <= 0x8B) || (b >= 0xA8 && b <= 0xAB)) folded = 'e';
            else if ((b >= 0x8C && b <= 0x8F) || (b >= 0xAC && b <= 0xAF)) folded = 'i';
            else if (b == 0x91 || b == 0xB1) folded = 'n';
            else if ((b >= 0x92 && b <= 0x96) || (b >= 0xB2 && b <= 0xB6)) folded = 'o';
            else if ((b >= 0x99 && b <= 0x9C) || (b >= 0xB9 && b <= 0xBC)) folded = 'u';
            else folded = '?';
            out[n++] = folded;
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
    return n;
}

static bool sk_prefix_match(const char *candidate, const char *folded_prefix, int prefix_len)
{
    char folded[SK_WORD_MAX_BYTES];
    int clen = sk_fold_utf8(candidate, folded, (int)sizeof(folded));
    if (clen < prefix_len) return false;
    return memcmp(folded, folded_prefix, (size_t)prefix_len) == 0;
}

static bool sk_suggestions_has(const struct sk_suggestions *s, const char *word)
{
    for (int i = 0; i < s->count; i++)
        if (!strcmp(s->items[i], word)) return true;
    return false;
}

static void sk_suggestions_push(struct sk_suggestions *s, const char *word)
{
    if (!word || !word[0] || s->count >= SK_MAX_SUGGESTIONS) return;
    if (sk_suggestions_has(s, word)) return;
    size_t len = strlen(word);
    if (len >= SK_WORD_MAX_BYTES) len = SK_WORD_MAX_BYTES - 1;
    memcpy(s->items[s->count], word, len);
    s->items[s->count][len] = '\0';
    s->count++;
}

/* Compact frequency-ordered PT-BR dictionary (explicit UTF-8 bytes, split before hex digits). */
static const char *const sk_dict_pt_br[] = {
    "configura\xc3\xa7\xc3\xa3o", "configurar", "configura\xc3\xa7\xc3\xb5" "es",
    "conex\xc3\xa3o", "conectar", "conectado", "computador", "computadores",
    "controle", "controles", "resolu\xc3\xa7\xc3\xa3o", "v\xc3\xad" "deo", "\xc3\xa1udio",
    "biblioteca", "buscar", "busca", "perfil", "perfis", "jogo", "jogos",
    "jogar", "iniciar", "continuar", "pausar", "voltar", "sair", "fechar",
    "salvar", "cancelar", "confirmar", "apagar", "excluir", "renomear",
    "tela", "cheia", "janela", "modo", "desktop", "mouse", "teclado",
    "rede", "local", "endere\xc3\xa7o", "porta", "servidor", "pareamento", "parear",
    "qualidade", "desempenho", "lat\xc3\xaa" "ncia", "est\xc3\xa1vel", "r\xc3\xa1pido", "suave",
    "competitivo", "equilibrado", "personalizado", "padr\xc3\xa3o", "avan\xc3\xa7" "ado",
    "sistema", "interface", "idioma", "portugu\xc3\xaa" "s", "ingl\xc3\xaa" "s", "espanhol",
    "apar\xc3\xaa" "ncia", "tema", "verde", "azul", "dourado", "violeta", "fundo",
    "papel", "parede", "anima\xc3\xa7\xc3\xa3o", "anima\xc3\xa7\xc3\xb5" "es", "som", "volume",
    "est\xc3\xa9reo", "mudo", "brilho", "contraste", "nitidez", "filtro",
    "sess\xc3\xa3o", "sess\xc3\xb5" "es", "hist\xc3\xb3rico", "recentes", "favoritos",
    "atualizar", "recarregar", "reiniciar", "desligar", "diagn\xc3\xb3stico",
    "atalho", "atalhos", "pasta", "arquivo", "arquivos", "disco", "mem\xc3\xb3ria",
    "usu\xc3\xa1rio", "senha", "acesso", "permiss\xc3\xa3o", "erro", "aviso",
    "ol\xc3\xa1", "obrigado", "ajuda", "sim", "n\xc3\xa3o", "agora", "depois",
    "sempre", "nunca", "melhor", "novo", "nova", "abrir", "enviar", "texto",
    "xbox", "moonlight", "sunshine", "apollo", "steam", "aurora", "youtube"
};

/* Compact frequency-ordered English dictionary. */
static const char *const sk_dict_en[] = {
    "configuration", "configure", "connection", "connect", "connected",
    "computer", "computers", "controller", "controllers", "resolution",
    "video", "audio", "library", "search", "profile", "profiles", "game", "games",
    "play", "playing", "start", "continue", "resume", "pause", "back", "exit", "close",
    "save", "saved", "cancel", "confirm", "delete", "remove", "rename",
    "screen", "fullscreen", "window", "mode", "desktop", "mouse", "keyboard",
    "network", "local", "address", "port", "server", "host", "pairing", "pair",
    "quality", "performance", "latency", "smooth", "fast", "stable",
    "competitive", "balanced", "custom", "default", "advanced", "settings",
    "system", "interface", "language", "english", "portuguese", "spanish",
    "appearance", "theme", "green", "blue", "gold", "violet", "background",
    "wallpaper", "motion", "animation", "sound", "volume", "stereo", "muted",
    "session", "sessions", "history", "recent", "favorites", "refresh",
    "reload", "restart", "shutdown", "diagnostics", "shortcut", "shortcuts",
    "folder", "file", "files", "storage", "memory", "user", "password",
    "hello", "thanks", "help", "yes", "open", "send", "text", "stream", "streaming",
    "xbox", "moonlight", "sunshine", "apollo", "steam", "aurora", "youtube"
};

/* Compact frequency-ordered Spanish dictionary (explicit UTF-8 bytes). */
static const char *const sk_dict_es[] = {
    "configuraci\xc3\xb3n", "configurar", "configuraciones",
    "conexi\xc3\xb3n", "conectar", "conectado", "equipo", "equipos",
    "control", "controles", "resoluci\xc3\xb3n", "v\xc3\xad" "deo", "audio",
    "biblioteca", "buscar", "b\xc3\xbasqueda", "perfil", "perfiles", "juego", "juegos",
    "jugar", "iniciar", "continuar", "pausar", "volver", "salir", "cerrar",
    "guardar", "cancelar", "confirmar", "borrar", "eliminar", "renombrar",
    "pantalla", "completa", "ventana", "modo", "escritorio", "rat\xc3\xb3n", "teclado",
    "red", "local", "direcci\xc3\xb3n", "puerto", "servidor", "emparejar", "emparejamiento",
    "calidad", "rendimiento", "latencia", "estable", "r\xc3\xa1pido", "suave",
    "competitivo", "equilibrado", "personalizado", "predeterminado", "avanzado",
    "sistema", "interfaz", "idioma", "espa\xc3\xb1ol", "portugu\xc3\xa9s", "ingl\xc3\xa9s",
    "apariencia", "tema", "verde", "azul", "dorado", "violeta", "fondo",
    "animaci\xc3\xb3n", "animaciones", "sonido", "volumen", "est\xc3\xa9reo", "silencio",
    "sesi\xc3\xb3n", "sesiones", "historial", "recientes", "favoritos",
    "actualizar", "recargar", "reiniciar", "apagar", "diagn\xc3\xb3stico",
    "acceso", "carpeta", "archivo", "archivos", "memoria", "usuario", "contrase\xc3\xb1" "a",
    "hola", "gracias", "ayuda", "s\xc3\xad", "ahora", "abrir", "enviar", "texto",
    "xbox", "moonlight", "sunshine", "apollo", "steam", "aurora", "youtube"
};

static const struct sk_bigram sk_bigrams_pt[] = {
    {"xbox", {"360", "series", "game"}},
    {"modo", {"desktop", "jogo", "suave"}},
    {"configuracao", {"de", "do", "atual"}},
    {"perfil", {"padrao", "competitivo", "suave"}},
    {"stream", {"profile", "720p", "60fps"}},
    {"meu", {"perfil", "computador", "jogo"}}
};

static const struct sk_bigram sk_bigrams_en[] = {
    {"xbox", {"360", "series", "live"}},
    {"stream", {"profile", "settings", "quality"}},
    {"desktop", {"mode", "keyboard", "actions"}},
    {"my", {"profile", "library", "games"}},
    {"fast", {"decode", "profile", "mode"}}
};

static const struct sk_bigram sk_bigrams_es[] = {
    {"xbox", {"360", "series", "live"}},
    {"modo", {"escritorio", "juego", "suave"}},
    {"perfil", {"predeterminado", "competitivo", "suave"}},
    {"mi", {"perfil", "biblioteca", "equipo"}}
};

static void sk_user_dict_learn(struct sk_user_dict *ud, const char *word)
{
    if (!ud || !ud->enabled || !word) return;
    size_t len = strlen(word);
    if (len < 2 || len >= SK_WORD_MAX_BYTES) return;
    int empty = -1, lowest = 0;
    for (int i = 0; i < SK_USER_DICT_MAX; i++) {
        if (ud->entries[i].used && !strcmp(ud->entries[i].word, word)) {
            if (ud->entries[i].uses < 65000) ud->entries[i].uses++;
            return;
        }
        if (!ud->entries[i].used && empty < 0) empty = i;
        if (ud->entries[i].used && ud->entries[i].uses < ud->entries[lowest].uses) lowest = i;
    }
    int slot = empty >= 0 ? empty : lowest;
    memset(&ud->entries[slot], 0, sizeof(ud->entries[slot]));
    memcpy(ud->entries[slot].word, word, len + 1);
    ud->entries[slot].uses = 1;
    ud->entries[slot].used = true;
}

static size_t sk_dict_memory_bytes(void)
{
    return sizeof(sk_dict_pt_br) + sizeof(sk_dict_en) + sizeof(sk_dict_es) +
           sizeof(sk_bigrams_pt) + sizeof(sk_bigrams_en) + sizeof(sk_bigrams_es);
}

/* Compute up to 3 stable suggestions for the current word prefix or previous word. */
static void sk_suggest(int lang, const struct sk_user_dict *ud,
                       const char *prefix, const char *prev_word,
                       struct sk_suggestions *out)
{
    memset(out, 0, sizeof(*out));
    if (lang < 0 || lang >= SK_LANG_COUNT) lang = SK_LANG_EN;

    /* Empty prefix after space: offer next-word bigram suggestions if available. */
    if (!prefix || !prefix[0]) {
        if (!prev_word || !prev_word[0]) return;
        char folded_prev[SK_WORD_MAX_BYTES];
        sk_fold_utf8(prev_word, folded_prev, (int)sizeof(folded_prev));
        const struct sk_bigram *table = lang == SK_LANG_PT_BR ? sk_bigrams_pt :
                                        lang == SK_LANG_ES ? sk_bigrams_es : sk_bigrams_en;
        int count = lang == SK_LANG_PT_BR ? (int)(sizeof(sk_bigrams_pt)/sizeof(sk_bigrams_pt[0])) :
                    lang == SK_LANG_ES ? (int)(sizeof(sk_bigrams_es)/sizeof(sk_bigrams_es[0])) :
                    (int)(sizeof(sk_bigrams_en)/sizeof(sk_bigrams_en[0]));
        for (int i = 0; i < count; i++) {
            if (!strcmp(table[i].prev, folded_prev)) {
                for (int k = 0; k < 3; k++) sk_suggestions_push(out, table[i].next[k]);
                return;
            }
        }
        return;
    }

    char folded_prefix[SK_WORD_MAX_BYTES];
    int plen = sk_fold_utf8(prefix, folded_prefix, (int)sizeof(folded_prefix));
    if (plen <= 0) return;

    /* 1. User learned words first (when enabled), ordered by use count. */
    if (ud && ud->enabled) {
        for (int pass = 0; pass < SK_MAX_SUGGESTIONS && out->count < SK_MAX_SUGGESTIONS; pass++) {
            int best = -1;
            for (int i = 0; i < SK_USER_DICT_MAX; i++) {
                if (!ud->entries[i].used) continue;
                if (sk_suggestions_has(out, ud->entries[i].word)) continue;
                if (!sk_prefix_match(ud->entries[i].word, folded_prefix, plen)) continue;
                if (best < 0 || ud->entries[i].uses > ud->entries[best].uses) best = i;
            }
            if (best >= 0) sk_suggestions_push(out, ud->entries[best].word);
            else break;
        }
    }

    /* 2. Frequency-ordered static dictionary for the active keyboard language. */
    const char *const *dict = lang == SK_LANG_PT_BR ? sk_dict_pt_br :
                              lang == SK_LANG_ES ? sk_dict_es : sk_dict_en;
    int dcount = lang == SK_LANG_PT_BR ? (int)(sizeof(sk_dict_pt_br)/sizeof(sk_dict_pt_br[0])) :
                 lang == SK_LANG_ES ? (int)(sizeof(sk_dict_es)/sizeof(sk_dict_es[0])) :
                 (int)(sizeof(sk_dict_en)/sizeof(sk_dict_en[0]));
    for (int i = 0; i < dcount && out->count < SK_MAX_SUGGESTIONS; i++) {
        if (sk_prefix_match(dict[i], folded_prefix, plen))
            sk_suggestions_push(out, dict[i]);
    }
}
