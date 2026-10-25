#ifndef KHOME_KINDLETIMES_H
#define KHOME_KINDLETIMES_H

#include "canvas.h"
#include "text.h"

/* The text edition fetch.sh installs under /mnt/us/newspaper/text/current.
 * index.tsv is importance ordered; every article has a body file. */
#define KT_STORY_MAX 30
#define KT_PAGE_MAX 16
#define KT_INDEX_MAX 5
#define KT_ID_MAX 64
#define KT_SECTION_MAX 32
#define KT_SOURCE_MAX 64
#define KT_TITLE_MAX 512
#define KT_LEAD_MAX 1024
#define KT_PATH_MAX 512
#define KT_BODY_MAX 32768
#define KT_TEXT_SIZE_COUNT 3
#define KT_TEXT_SIZE_DEFAULT 30
#define KT_ARTICLE_PAGE_MAX 128

typedef enum {
    KT_ROLE_TOP = 0,
    KT_ROLE_BRIEF
} KtRole;

typedef struct {
    char id[KT_ID_MAX];
    char section[KT_SECTION_MAX];
    char source[KT_SOURCE_MAX];
    char published[16];
    int chars;
    char title[KT_TITLE_MAX];
    char lead[KT_LEAD_MAX];
} KtStory;

/* The edition stamp from edition.tsv: id is YYYYMMDD-HHMM. */
typedef struct {
    char id[KT_ID_MAX];
    char generated[48];
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int morning; /* the edition time is before 12:00 */
    int issue;   /* 1 on 2026-10-01, one more each day */
} KtEdition;

/* What the paper needs from one story after wrapping its head and summary. */
typedef struct {
    int head_count;
    int lead_count;
    int compact_lead_count;
    int top_height;     /* content height when the story leads page 1 */
    int brief_height;   /* content height when the story is a brief */
    int compact_height; /* brief with its summary cut to one line, or 0 */
} KtFit;

typedef struct {
    int story;
    KtRole role;
    int content_y; /* top of the meta line, panel pixels */
    int content_h; /* meta line to the end of the summary or lead */
    int rule_y;
    int head_count;
    int lead_count;
    int compact; /* the summary is the one-line compact cut */
} KtPlanItem;

typedef struct {
    int first; /* index into items */
    int count;
} KtPlanPage;

/* Pure paper pagination.  The plan keeps stories in index order, never splits
 * one, and leaves the bottom of page 1 for the index box. */
typedef struct {
    KtPlanItem items[KT_STORY_MAX];
    int item_count;
    KtPlanPage pages[KT_PAGE_MAX];
    int page_count;
    int story_page[KT_STORY_MAX]; /* 0-based page, -1 when not placed */
    int index_story[KT_INDEX_MAX];
    int index_count;
    int index_top;
} KtPaperLayout;

/* Wrapped head and summary for the paper.  The lines index into head/summary,
 * which hold the ellipsized text actually drawn. */
typedef struct {
    char head[KT_TITLE_MAX];
    char summary[KT_LEAD_MAX];
    char compact[KT_LEAD_MAX];
    TextLine head_lines[4];
    TextLine summary_lines[4];
    TextLine compact_lines[2];
    int head_count;
    int summary_count;
    int compact_count;
} KtStoryText;

typedef struct {
    KtEdition edition;
    KtStory stories[KT_STORY_MAX];
    KtStoryText text[KT_STORY_MAX];
    int story_count;
    KtPaperLayout layout;
    char dir[KT_PATH_MAX];
} KtPaper;

/* One reader page: the first body line and how many lines it holds. */
typedef struct {
    int first;
    int count;
} KtArticlePage;

/* The open article.  The body and the full headline are wrapped once on open
 * (and again only when the reader picks another type size). */
typedef struct {
    int story;
    int size; /* 26, 30 or 36 */
    int size_index;
    int line_height;
    int body_top;
    int has_next;
    int page;
    int page_count;
    int line_count;
    KtArticlePage pages[KT_ARTICLE_PAGE_MAX];
    char headline[KT_TITLE_MAX];
    char head[KT_TITLE_MAX];
    char source[KT_SOURCE_MAX];
    char published[16];
    char *raw;
    char *body;
    TextLine *lines;
    TextLine *head_lines;
    int head_count;
    int head_size;
    int head_leading;
} KtArticle;

int kt_edition_id_parse(const char *id, KtEdition *out);
int kt_issue_number(int year, int month, int day);

/* Pure: fit[] comes from kt_paper_build's measurements.  count is at most
 * KT_STORY_MAX.  Returns 0 when there is nothing to lay out. */
int kt_paper_plan(const KtFit *fit, int count, KtPaperLayout *out);
/* Pure: split line_count body lines into reader pages.  first_fill and fill
 * are the capacities when the page is not the last; first_last and last_fill
 * are the smaller capacities that leave room for the next-article box.  Every
 * page but the last is filled; the last holds 1..last_fill lines. */
int kt_article_paginate(int line_count, int first_fill, int first_last,
                        int fill, int last_fill, KtArticlePage *pages,
                        int max_pages);
/* Pure: pages needed when page 1 takes first_capacity and later pages take
 * capacity.  No extra reserve on the last page. */
int kt_article_page_count(int line_count, int first_capacity, int capacity);

int kt_paper_load(KtPaper *paper, const char *dir);
void kt_paper_build(KtPaper *paper, const Fonts *fonts);
int kt_paper_page_of(const KtPaper *paper, int story);
/* Story index under a panel point, or -1.  item_at looks at heads and leads,
 * index_at at the rows of the page-1 index box. */
int kt_paper_item_at(const KtPaper *paper, int page, int x, int y);
int kt_paper_index_at(const KtPaper *paper, int page, int x, int y);

int kt_article_open(KtArticle *article, const KtPaper *paper,
                    const Fonts *fonts, int story, int size);
int kt_article_set_size(KtArticle *article, const KtPaper *paper,
                        const Fonts *fonts, int size);
void kt_article_close(KtArticle *article);
/* 1 when a panel point hits the next-article box on the last page. */
int kt_article_next_at(const KtArticle *article, int x, int y);
/* 1 when a panel point hits the small Aa at the header's right edge. */
int kt_article_font_at(int x, int y);

/* Panel-pixel layout scaled by s: 1.0 for the 758x1024 framebuffer, and
 * 1072/758 for the design-scale renderer. */
void kt_draw_paper(Canvas *canvas, const Fonts *fonts, const KtPaper *paper,
                   int page, float s);
void kt_draw_article(Canvas *canvas, const Fonts *fonts, const KtPaper *paper,
                     const KtArticle *article, int page, int next_story,
                     float s);

#endif
