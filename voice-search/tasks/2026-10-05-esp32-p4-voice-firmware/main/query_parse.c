/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "query_parse.h"

#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "esp_log.h"

static const char *TAG = "query_parse";

/*
 * Words that are never a product. Three groups:
 *   - question/filler words ordinary speech is full of
 *   - stock-enquiry words ("available", "left", "stock") which describe the question,
 *     not the thing, and would otherwise match product descriptions
 *   - wake-word debris. Whisper does not see the wake word cleanly because WakeNet
 *     consumes it on-device; measured transcripts included "Hi SP", "I-E-S-P" and
 *     "I think I can see either", so those fragments get dropped too.
 */
static const char *STOPWORDS[] = {
    "where", "what", "which", "who", "how", "why", "when",
    "is", "are", "was", "were", "be", "been", "am",
    "the", "a", "an", "any", "some", "all",
    "do", "does", "did", "can", "could", "would", "will", "shall", "should",
    "we", "i", "you", "it", "they", "me", "my", "our", "us",
    "have", "has", "had", "got", "get",
    "find", "search", "look", "show", "tell", "give", "need", "want", "check",
    "for", "of", "in", "on", "at", "to", "from", "with", "there", "here",
    "many", "much", "left", "available", "stock", "inventory", "store", "stores",
    "please", "thanks", "thank", "ok", "okay", "yes", "no", "not",
    "and", "or", "but", "that", "this", "these", "those",
    /* wake-word debris */
    "hi", "hey", "hello", "esp", "sp", "e", "s", "p", "iesp",
    NULL
};

static bool is_stopword(const char *w)
{
    for (int i = 0; STOPWORDS[i]; i++) {
        if (strcmp(w, STOPWORDS[i]) == 0) {
            return true;
        }
    }
    return false;
}

size_t query_parse(const char *text, qp_candidates_t *out)
{
    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (!text || !text[0]) {
        return 0;
    }

    /* Collect lowercase alphanumeric words. Digits are kept: "esp32" and "6050" are
     * real product tokens even though MultiNet could never have handled them -- this
     * path goes through Whisper, which has no such restriction. */
    char words[16][QP_TERM_LEN];
    size_t wc = 0;
    size_t wl = 0;
    char cur[QP_TERM_LEN];

    for (const char *p = text; ; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) {
            if (wl < QP_TERM_LEN - 1) {
                cur[wl++] = (char)tolower(c);
            }
        } else {
            if (wl) {
                cur[wl] = '\0';
                /* Single letters and two-letter words are noise at this length. */
                if (wl >= 3 && !is_stopword(cur) && wc < 16) {
                    /* Skip duplicates -- hallucinated loops repeat the same word. */
                    bool dup = false;
                    for (size_t i = 0; i < wc; i++) {
                        if (strcmp(words[i], cur) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        strcpy(words[wc++], cur);
                    }
                }
                wl = 0;
            }
            if (!*p) {
                break;
            }
        }
    }

    if (wc == 0) {
        ESP_LOGW(TAG, "\"%s\" contained no product-like word", text);
        return 0;
    }

    /* Longest first: a more specific word is a better search term. */
    for (size_t i = 0; i + 1 < wc; i++) {
        for (size_t j = i + 1; j < wc; j++) {
            if (strlen(words[j]) > strlen(words[i])) {
                char t[QP_TERM_LEN];
                strcpy(t, words[i]);
                strcpy(words[i], words[j]);
                strcpy(words[j], t);
            }
        }
    }

    for (size_t i = 0; i < wc && out->n < QP_MAX_CANDIDATES; i++) {
        strcpy(out->terms[out->n++], words[i]);
    }

    char joined[160] = "";
    for (size_t i = 0; i < out->n; i++) {
        strncat(joined, out->terms[i], sizeof(joined) - strlen(joined) - 2);
        if (i + 1 < out->n) {
            strncat(joined, ", ", sizeof(joined) - strlen(joined) - 1);
        }
    }
    ESP_LOGI(TAG, "\"%s\" -> candidates: %s", text, joined);
    return out->n;
}
