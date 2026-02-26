#ifndef RANKER_H
#define RANKER_H

#include "cluster.h"

/* Compute relevance of title+description against query [0.0, 1.0] */
double compute_relevance(const char *title, const char *description,
                         const char *query);

/* Set relevance field on every article in list */
void score_articles(ArticleList *list, const char *query);

/* Sort clusters (and articles within each) by relevance desc */
void rank_clusters(ClusterList *cl, const char *query);

/* Set the global article list pointer used by internal comparators */
void ranker_set_article_list(const ArticleList *list);

#endif /* RANKER_H */
