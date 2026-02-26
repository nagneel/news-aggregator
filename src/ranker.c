#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ranker.h"
#include "cluster.h"

/* ── Relevance scoring ───────────────────────────────────────────────────── */

double compute_relevance(const char *title, const char *description,
                         const char *query) {
    if (!title || !query || query[0] == '\0') return 0.0;

    /* static: avoids ~24 KB of stack per call (dangerous in WASM).
       Safe because Emscripten is single-threaded. */
    static char qtoks[MAX_WORDS][MAX_WORD_LEN];
    static char ttoks[MAX_WORDS][MAX_WORD_LEN];
    static char dtoks[MAX_WORDS][MAX_WORD_LEN];

    int nq = tokenize(query, qtoks, MAX_WORDS);
    int nt = tokenize(title, ttoks, MAX_WORDS);
    int nd = description ? tokenize(description, dtoks, MAX_WORDS) : 0;

    if (nq == 0) return 0.0;

    int title_hits = 0, desc_hits = 0;

    for (int qi = 0; qi < nq; qi++) {
        int found_title = 0, found_desc = 0;

        for (int ti = 0; ti < nt && !found_title; ti++)
            if (strcmp(qtoks[qi], ttoks[ti]) == 0) found_title = 1;

        for (int di = 0; di < nd && !found_desc; di++)
            if (strcmp(qtoks[qi], dtoks[di]) == 0) found_desc = 1;

        if (found_title) title_hits++;
        if (found_desc)  desc_hits++;
    }

    double title_score = (nt > 0) ? (double)title_hits / (double)nq : 0.0;
    double desc_score  = (nd > 0) ? (double)desc_hits  / (double)nq : 0.0;

    double score = 0.7 * title_score + 0.3 * desc_score;
    return (score > 1.0) ? 1.0 : score;
}

void score_articles(ArticleList *list, const char *query) {
    if (!list || !query) return;
    for (int i = 0; i < list->count; i++) {
        Article *a = &list->articles[i];
        a->relevance = compute_relevance(a->title, a->description, query);
    }
}

/* ── qsort comparators ───────────────────────────────────────────────────── */

static const ArticleList *g_list = NULL; /* used by article comparator */

static int cmp_clusters(const void *a, const void *b) {
    const Cluster *ca = (const Cluster *)a;
    const Cluster *cb = (const Cluster *)b;
    if (cb->avg_relevance > ca->avg_relevance) return  1;
    if (cb->avg_relevance < ca->avg_relevance) return -1;
    return (cb->count > ca->count) - (cb->count < ca->count);
}

static int cmp_articles_by_relevance(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    double ra = g_list->articles[ia].relevance;
    double rb = g_list->articles[ib].relevance;
    if (rb > ra) return  1;
    if (rb < ra) return -1;
    return 0;
}

/* ── Rank clusters + articles within each ───────────────────────────────── */

void rank_clusters(ClusterList *cl, const char *query) {
    if (!cl || !g_list) return;
    (void)query; /* used indirectly via score_articles */

    for (int c = 0; c < cl->count; c++) {
        Cluster *clust = &cl->clusters[c];
        if (clust->count == 0) continue;

        /* sort articles within cluster */
        qsort(clust->article_indices, clust->count, sizeof(int),
              cmp_articles_by_relevance);

        /* compute avg relevance */
        double sum = 0.0;
        for (int i = 0; i < clust->count; i++)
            sum += g_list->articles[clust->article_indices[i]].relevance;
        clust->avg_relevance = sum / (double)clust->count;

        /* representative = most relevant article title */
        const Article *top =
            &g_list->articles[clust->article_indices[0]];
        strncpy(clust->representative, top->title, MAX_TITLE_LEN - 1);
        clust->representative[MAX_TITLE_LEN - 1] = '\0';
    }

    /* sort clusters */
    qsort(cl->clusters, cl->count, sizeof(Cluster), cmp_clusters);
}

/* Allow api.c to set the global article list pointer before calling
   rank_clusters (needed by the article comparator). */
void ranker_set_article_list(const ArticleList *list) {
    g_list = list;
}
