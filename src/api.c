#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include "cJSON.h"
#include "cluster.h"
#include "ranker.h"

/* ── Static result buffer (avoids per-call malloc/free from JS) ─────────── */

static char *result_buf   = NULL;
static size_t result_cap  = 0;

static int ensure_buf(size_t needed) {
    if (needed <= result_cap) return 1;
    char *nb = (char *)realloc(result_buf, needed);
    if (!nb) return 0;
    result_buf = nb;
    result_cap = needed;
    return 1;
}

/* ── JSON → ArticleList ──────────────────────────────────────────────────── */

static int parse_articles(const char *json, ArticleList *list) {
    list->count = 0;
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n && list->count < MAX_ARTICLES; i++) {
        cJSON *obj = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(obj)) continue;

        Article *a = &list->articles[list->count];
        memset(a, 0, sizeof(Article));

#define COPY_FIELD(field, key) \
        do { \
            cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key); \
            if (cJSON_IsString(v) && v->valuestring) { \
                strncpy(a->field, v->valuestring, sizeof(a->field) - 1); \
            } \
        } while (0)

        COPY_FIELD(title,       "title");
        COPY_FIELD(url,         "url");
        COPY_FIELD(description, "description");
        COPY_FIELD(source,      "source");
        COPY_FIELD(pub_date,    "pubDate");
#undef COPY_FIELD

        if (a->title[0] == '\0') continue; /* skip articles without title */
        list->count++;
    }

    cJSON_Delete(root);
    return list->count;
}

/* ── ArticleList + ClusterList → JSON string ─────────────────────────────── */

static const char *serialize_clusters(const ClusterList *cl,
                                      const ArticleList *articles) {
    cJSON *out = cJSON_CreateArray();
    if (!out) return "[]";

    for (int c = 0; c < cl->count; c++) {
        const Cluster *clust = &cl->clusters[c];
        cJSON *cobj = cJSON_CreateObject();

        cJSON_AddStringToObject(cobj, "representative", clust->representative);
        cJSON_AddNumberToObject(cobj, "avg_relevance",  clust->avg_relevance);
        cJSON_AddNumberToObject(cobj, "count",          clust->count);

        cJSON *arts = cJSON_CreateArray();
        for (int j = 0; j < clust->count; j++) {
            int idx = clust->article_indices[j];
            if (idx < 0 || idx >= articles->count) continue; /* bounds guard */
            const Article *a = &articles->articles[idx];
            cJSON *aobj = cJSON_CreateObject();
            cJSON_AddStringToObject(aobj, "title",       a->title);
            cJSON_AddStringToObject(aobj, "url",         a->url);
            cJSON_AddStringToObject(aobj, "description", a->description);
            cJSON_AddStringToObject(aobj, "source",      a->source);
            cJSON_AddStringToObject(aobj, "pubDate",     a->pub_date);
            cJSON_AddNumberToObject (aobj, "relevance",  a->relevance);
            cJSON_AddItemToArray(arts, aobj);
        }
        cJSON_AddItemToObject(cobj, "articles", arts);
        cJSON_AddItemToArray(out, cobj);
    }

    char *s = cJSON_PrintUnformatted(out);
    cJSON_Delete(out);
    if (!s) return "[]";

    size_t len = strlen(s) + 1;
    if (!ensure_buf(len)) { free(s); return "[]"; }
    memcpy(result_buf, s, len);
    free(s);
    return result_buf;
}

/* ── WASM entry point ────────────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
const char *process_articles(const char *articles_json, const char *query) {
    if (!articles_json || !query) return "[]";

    static ArticleList list;
    memset(&list, 0, sizeof(list));

    if (parse_articles(articles_json, &list) == 0) return "[]";

    score_articles(&list, query);

    ranker_set_article_list(&list);
    ClusterList *cl = cluster_articles(&list);
    if (!cl) return "[]";

    rank_clusters(cl, query);

    const char *result = serialize_clusters(cl, &list);
    free_cluster_list(cl);
    return result;
}
