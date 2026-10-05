/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Turn a spoken sentence into candidate search terms.
 *
 * No NLP is needed or wanted. The transcript is one short question, the catalogue is
 * matched by substring, and the useful signal is simply "which word is a product
 * word". So: drop the words that are never product names (question words, filler,
 * and the wake word, which Whisper mangles into things like "I-E-S-P" or "Hi SP"),
 * then try what is left, longest first.
 *
 * Longest-first matters: in "where is the esp32 devkit", trying `devkit` before `esp`
 * finds the specific product rather than every ESP board in the building.
 */

#pragma once

#include <stddef.h>

#define QP_MAX_CANDIDATES 6
#define QP_TERM_LEN       32

typedef struct {
    char   terms[QP_MAX_CANDIDATES][QP_TERM_LEN];
    size_t n;
} qp_candidates_t;

/**
 * Extract candidate search terms from a transcript, longest word first.
 *
 * @param text  the transcript, e.g. "Hi SP, where is Arduino?"
 * @param out   receives the candidates, e.g. {"arduino"}
 * @return number of candidates found (0 if the sentence was all filler)
 */
size_t query_parse(const char *text, qp_candidates_t *out);
