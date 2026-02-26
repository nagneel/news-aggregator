#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cluster.h"

/* ── Stopwords ───────────────────────────────────────────────────────────── */

static const char *STOPWORDS[] = {
    "a","an","the","is","are","was","were","be","been","being",
    "in","of","for","by","to","at","on","as","it","its",
    "this","that","these","those","and","or","but","not","nor","so",
    "from","with","into","about","over","after","before","between",
    "has","have","had","do","does","did","will","would","could",
    "should","may","might","shall","can","than","then","if","when",
    "up","out","how","what","who","which","where","why","all","more",
    "new","say","says","said","one","two","us","no","he","she","we",
    NULL
};

static int is_stopword(const char *w) {
    for (int i = 0; STOPWORDS[i]; i++)
        if (strcmp(w, STOPWORDS[i]) == 0) return 1;
    return 0;
}

/* ── Tokenizer ───────────────────────────────────────────────────────────── */

int tokenize(const char *text, char words[][MAX_WORD_LEN], int max_words) {
    if (!text || !words || max_words <= 0) return 0;

    /* work on a local copy */
    char buf[MAX_TITLE_LEN + MAX_DESC_LEN];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* lowercase + replace non-alpha with space */
    for (int i = 0; buf[i]; i++) {
        if (isalpha((unsigned char)buf[i]))
            buf[i] = (char)tolower((unsigned char)buf[i]);
        else
            buf[i] = ' ';
    }

    int count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " ", &saveptr);
    while (tok && count < max_words) {
        if (strlen(tok) > 2 && !is_stopword(tok)) {
            strncpy(words[count], tok, MAX_WORD_LEN - 1);
            words[count][MAX_WORD_LEN - 1] = '\0';
            count++;
        }
        tok = strtok_r(NULL, " ", &saveptr);
    }
    return count;
}

/* ── Jaccard similarity ──────────────────────────────────────────────────── */

/* Deduplicate a word array in-place; returns new count.
   Required for correct set-semantics Jaccard (prevents duplicate words in
   the title from inflating the intersection count). */
static int dedup_words(char words[][MAX_WORD_LEN], int n) {
    int out = 0;
    for (int i = 0; i < n; i++) {
        int dup = 0;
        for (int j = 0; j < out; j++)
            if (strcmp(words[i], words[j]) == 0) { dup = 1; break; }
        if (!dup) {
            if (i != out) memcpy(words[out], words[i], MAX_WORD_LEN);
            out++;
        }
    }
    return out;
}

double headline_similarity(const char *a, const char *b) {
    /* static: avoids ~16 KB of stack in a function called in a tight loop */
    static char wa[MAX_WORDS][MAX_WORD_LEN];
    static char wb[MAX_WORDS][MAX_WORD_LEN];
    int na = tokenize(a, wa, MAX_WORDS);
    int nb = tokenize(b, wb, MAX_WORDS);

    if (na == 0 && nb == 0) return 1.0;
    if (na == 0 || nb == 0) return 0.0;

    /* deduplicate both sets so Jaccard reflects set intersection, not multiset */
    na = dedup_words(wa, na);
    nb = dedup_words(wb, nb);

    /* count matches; mark wb entry empty after use to avoid double-counting */
    int intersection = 0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (wb[j][0] != '\0' && strcmp(wa[i], wb[j]) == 0) {
                intersection++;
                wb[j][0] = '\0'; /* consumed */
                break;
            }

    int union_size = na + nb - intersection;
    return (union_size > 0) ? (double)intersection / (double)union_size : 0.0;
}

/* ── Greedy clustering ───────────────────────────────────────────────────── */

ClusterList *cluster_articles(const ArticleList *list) {
    if (!list) return NULL;

    ClusterList *cl = (ClusterList *)calloc(1, sizeof(ClusterList));
    if (!cl) return NULL;

    for (int i = 0; i < list->count; i++) {
        const Article *art = &list->articles[i];
        int best_cluster = -1;
        double best_sim = CLUSTER_THRESHOLD;

        /* find the closest existing cluster */
        for (int c = 0; c < cl->count; c++) {
            const char *rep = cl->clusters[c].representative;
            double sim = headline_similarity(art->title, rep);
            if (sim > best_sim) {
                best_sim = sim;
                best_cluster = c;
            }
        }

        if (best_cluster >= 0) {
            Cluster *clust = &cl->clusters[best_cluster];
            if (clust->count < MAX_ARTICLES) {
                clust->article_indices[clust->count++] = i;
            }
        } else {
            /* create a new cluster */
            if (cl->count < MAX_CLUSTERS) {
                Cluster *clust = &cl->clusters[cl->count++];
                clust->article_indices[0] = i;
                clust->count = 1;
                strncpy(clust->representative, art->title,
                        MAX_TITLE_LEN - 1);
                clust->representative[MAX_TITLE_LEN - 1] = '\0';
                clust->avg_relevance = 0.0;
            }
        }
    }

    return cl;
}

void free_cluster_list(ClusterList *cl) {
    free(cl);
}
