#ifndef CLUSTER_H
#define CLUSTER_H

#define MAX_ARTICLES     1000
#define MAX_CLUSTERS     200
#define MAX_WORDS        128
#define MAX_WORD_LEN     64
#define MAX_TITLE_LEN    512
#define MAX_DESC_LEN     1024
#define MAX_URL_LEN      512
#define MAX_SOURCE_LEN   128
#define MAX_DATE_LEN     64

#define CLUSTER_THRESHOLD 0.25

typedef struct {
    char title[MAX_TITLE_LEN];
    char url[MAX_URL_LEN];
    char description[MAX_DESC_LEN];
    char source[MAX_SOURCE_LEN];
    char pub_date[MAX_DATE_LEN];
    double relevance;
} Article;

typedef struct {
    Article articles[MAX_ARTICLES];
    int count;
} ArticleList;

typedef struct {
    int article_indices[MAX_ARTICLES];
    int count;
    char representative[MAX_TITLE_LEN];
    double avg_relevance;
} Cluster;

typedef struct {
    Cluster clusters[MAX_CLUSTERS];
    int count;
} ClusterList;

/* Tokenize text into words array; returns word count */
int tokenize(const char *text, char words[][MAX_WORD_LEN], int max_words);

/* Jaccard similarity between two headlines [0.0, 1.0] */
double headline_similarity(const char *a, const char *b);

/* Greedy clustering; caller owns returned ClusterList* */
ClusterList *cluster_articles(const ArticleList *list);

/* Free a ClusterList */
void free_cluster_list(ClusterList *cl);

#endif /* CLUSTER_H */
