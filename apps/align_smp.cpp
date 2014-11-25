/**
 * @file align_smp
 *
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 *
 * Reads packed fasta file of N sequences.
 * Indexes input to learn length and end locations of each sequence.
 * Creates SA, LCP, and BWT.
 * Runs maximal pairs algorithm with the given minimum cutoff length.
 * For each sequence pair, performs semi-global alignment.
 *
 * Note about the input file. It is expected to be a packed fasta file
 * with each sequence delimited by the '$' sentinal. For example,
 * "banana$mississippi$foo$bar$".
 */
#include "config.h"

#include <sys/time.h>

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <stack>
#include <utility>
#include <vector>

#include "align/align.h"
#include "blosum/blosum62.h"
#if HAVE_SSE2
#include "align/align_scan_128_16.h"
#endif

#include "sais.h"
#include "timer_real.h"

using ::std::make_pair;
using ::std::pair;
using ::std::set;
using ::std::size_t;
using ::std::stack;
using ::std::vector;

typedef pair<int,int> Pair;

typedef set<Pair> PairSet;

struct quad {
    int lcp;
    int lb;
    int rb;
    vector<quad> children;

    quad()
        : lcp(0), lb(0), rb(INT_MAX), children() {}
    quad(int lcp, int lb, int rb)
        : lcp(lcp), lb(lb), rb(rb), children() {}
    quad(int lcp, int lb, int rb, vector<quad> children)
        : lcp(lcp), lb(lb), rb(rb), children(children) {}

    bool empty() { return rb == INT_MAX; }
};

inline static void pair_check(
        int &count_generated,
        PairSet &pairs,
        const int &i,
        const int &j,
        const int * const restrict SA,
        const unsigned char * const restrict BWT,
        const int * const restrict SID,
        const char &sentinal);

inline static void process(
        int &count_generated,
        PairSet &pairs,
        const quad &q,
        const int * const restrict SA,
        const unsigned char * const restrict BWT,
        const int * const restrict SID,
        const char &sentinal,
        const int &cutoff);

static void print_help(const char *progname, int status) {
    fprintf(stdout, "usage: %s [-c cutoff>=1] FILE\n\n", progname);
    exit(status);
}

int main(int argc, const char *argv[]) {
    FILE *fp = NULL;
    const char *fname = NULL;
    unsigned char *T = NULL;
    int *tbl_pr = NULL;
    int *del_pr = NULL;
    int *SA = NULL;
    int *LCP = NULL;
    unsigned char *BWT = NULL;
    int *SID = NULL;
    vector<int> BEG;
    vector<int> END;
    int n = 0;
    double start = 0;
    double finish = 0;
    int i = 0;
    int longest = 0;
    int sid = 0;
    char sentinal = 0;
    int cutoff = 1;
    PairSet pairs;
    int count_generated = 0;

    /* Check arguments. */
    i = 1;
    while (i < argc) {
        if ((strncmp(argv[i], "-h", 2) == 0)
                || (strncmp(argv[i], "--help", 6) == 0)) {
            fprintf(stdout, "help requested\n");
            print_help(argv[0], EXIT_SUCCESS);
        }
        else if (strncmp(argv[i], "-c", 2) == 0) {
            if (i+1 < argc) {
                cutoff = atoi(argv[i+1]);
                if (cutoff <= 0) {
                    print_help(argv[0], EXIT_FAILURE);
                }
                ++i;
            }
            else {
                if (i+1 >= argc) {
                    fprintf(stdout, "-c takes a parameter\n");
                }
                else {
                    fprintf(stdout, "bad argument to -c: %s\n", argv[i+1]);
                }
                print_help(argv[0], EXIT_FAILURE);
            }
        }
        else if (strncmp(argv[i], "-", 1) != 0) {
            /* filename */
            if (i+1 != argc) { /* last argument */
                print_help(argv[0], EXIT_FAILURE);
            }
            fname = argv[i];
        }
        else {
            print_help(argv[0], EXIT_FAILURE);
        }
        ++i;
    }
    if (fname == NULL) {
        fprintf(stdout, "missing input file\n");
        print_help(argv[0], EXIT_FAILURE);
    }

    /* Open a file for reading. */
    if((fp = fopen(fname, "rb")) == NULL) {
        fprintf(stdout, "%s: Cannot open file `%s': ", argv[0], fname);
        perror(NULL);
        exit(EXIT_FAILURE);
    }

    /* Get the file size. */
    if(fseek(fp, 0, SEEK_END) == 0) {
        n = ftell(fp);
        rewind(fp);
        if(n < 0) {
            fprintf(stdout, "%s: Cannot ftell `%s': ", argv[0], fname);
            perror(NULL);
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stdout, "%s: Cannot fseek `%s': ", argv[0], fname);
        perror(NULL);
        exit(EXIT_FAILURE);
    }

    /* Allocate 9n bytes of memory. */
    T = (unsigned char *)malloc((size_t)(n+1) * sizeof(unsigned char));
    SA = (int *)malloc((size_t)(n+1) * sizeof(int)); /* +1 for computing LCP */
    LCP = (int *)malloc((size_t)(n+1) * sizeof(int)); /* +1 for lcp tree */
    BWT = (unsigned char *)malloc((size_t)(n+1) * sizeof(unsigned char));
    SID = (int *)malloc((size_t)n * sizeof(int));
    if((T == NULL)
            || (SA == NULL)
            || (LCP == NULL)
            || (BWT == NULL)
            || (SID == NULL))
    {
        fprintf(stdout, "%s: Cannot allocate memory.\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Read n bytes of data. */
    if(fread(T, sizeof(unsigned char), (size_t)n, fp) != (size_t)n) {
        fprintf(stdout, "%s: %s `%s': ",
                argv[0],
                (ferror(fp) || !feof(fp)) ? "Cannot read from" : "Unexpected EOF in",
                fname);
        perror(NULL);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    T[n]='\0'; /* so we can print it */

    /* determine sentinal */
    if (sentinal == 0) {
        int off = 0;
        while (!isgraph(T[n-off])) {
            ++off;
        }
        sentinal = T[n-off];
    }

    /* determine actual end of file (last char) */
    {
        int off = 0;
        while (!isgraph(T[n-off])) {
            ++off;
        }
        n = n - off + 1;
    }

    /* scan T from left to build sequence ID and end index */
    longest = 0;
    sid = 0;
    BEG.push_back(0);
    for (i=0; i<n; ++i) {
        SID[i] = sid;
        if (T[i] == sentinal) {
            int len = i-BEG[sid];
            longest = (len>longest) ? len : longest;
            END.push_back(i);
            BEG.push_back(i+1);
            ++sid;
        }
    }
    longest += 1;
    assert(BEG.size()==END.size()+1);
    if (0 == sid) { /* no sentinal found */
        fprintf(stdout, "no sentinal(%c) found in input\n", sentinal);
        exit(EXIT_FAILURE);
    }

    /* Construct the suffix and LCP arrays.
     * The following sais routine is from Fischer, with bugs fixed. */
    fprintf(stdout, "%s: %d bytes ... \n", fname, n);
    start = timer_real();
    if(sais(T, SA, LCP, (int)n) != 0) {
        fprintf(stdout, "%s: Cannot allocate memory.\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    finish = timer_real();
    fprintf(stdout, "induced SA: %.4f sec\n", finish-start);

    /* construct naive BWT: */
    start = timer_real();
    for (i = 0; i < n; ++i) {
        BWT[i] = (SA[i] > 0) ? T[SA[i]-1] : sentinal;
    }
    finish = timer_real();
    fprintf(stdout, " naive BWT: %.4f sec\n", finish-start);

    /* "fix" the LCP array to clamp LCP's that are too long */
    start = timer_real();
    for (i = 0; i < n; ++i) {
        int len = END[SID[SA[i]]] - SA[i]; /* don't include sentinal */
        if (LCP[i] > len) LCP[i] = len;
    }
    finish = timer_real();
    fprintf(stdout, " clamp LCP: %.4f sec\n", finish-start);

    /* The GSA we create will put all sentinals either at the beginning
     * or end of the SA. We don't want to count all of the terminals,
     * nor do we want to process them in our bottom-up traversal. */
    /* do the sentinals appear at the beginning or end of SA? */
    int bup_start = 1;
    int bup_stop = n;
    if (T[SA[0]] == sentinal) {
        /* sentinals at beginning */
        bup_start = sid+1;
        bup_stop = n;
    }
    else if (T[SA[n-1]] == sentinal) {
        /* sentinals at end */
        bup_start = 1;
        bup_stop = n-sid;
    }
    else {
        fprintf(stdout, "sentinals not found at beginning or end of SA\n");
    }

    /* DFS of enhanced SA, from Abouelhoda et al */
    start = timer_real();
    count_generated = 0;
    LCP[n] = 0; /* doesn't really exist, but for the root */
    {
        stack<quad> the_stack;
        quad last_interval;
        the_stack.push(quad());
        for (i = bup_start; i <= bup_stop; ++i) {
            int lb = i - 1;
            while (LCP[i] < the_stack.top().lcp) {
                the_stack.top().rb = i - 1;
                last_interval = the_stack.top();
                the_stack.pop();
                process(count_generated, pairs, last_interval, SA, BWT, SID, sentinal, cutoff);
                lb = last_interval.lb;
                if (LCP[i] <= the_stack.top().lcp) {
                    last_interval.children.clear();
                    the_stack.top().children.push_back(last_interval);
                    last_interval = quad();
                }
            }
            if (LCP[i] > the_stack.top().lcp) {
                if (!last_interval.empty()) {
                    last_interval.children.clear();
                    the_stack.push(quad(LCP[i],lb,INT_MAX,vector<quad>(1, last_interval)));
                    last_interval = quad();
                }
                else {
                    the_stack.push(quad(LCP[i],lb,INT_MAX));
                }
            }
        }
        the_stack.top().rb = bup_stop - 1;
        process(count_generated, pairs, the_stack.top(), SA, BWT, SID, sentinal, cutoff);
    }
    finish = timer_real();
    fprintf(stdout, "processing: %.4f sec\n", finish-start);
    fprintf(stdout, "generated pairs = %d\n", count_generated);
    fprintf(stdout, "   unique pairs = %zu\n", pairs.size());

    /* Deallocate memory. */
    free(SA);
    free(LCP);
    free(BWT);
    free(SID);

#if HAVE_SSE2
#else
    /* allocate table memory */
    tbl_pr = (int*)malloc(sizeof(int) * longest);
    del_pr = (int*)malloc(sizeof(int) * longest);
#endif

#ifdef _OPENMP
    start = timer_real();
    vector<Pair> vpairs(pairs.begin(), pairs.end());
    finish = timer_real();
    fprintf(stdout, "omp pairs: %.4f sec\n", finish-start);
#endif

    /* align pairs */
    start = timer_real();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
    for (int index=0; index<vpairs.size(); ++index) {
        int i = vpairs[index].first;
        int j = vpairs[index].second;
#else
    for (PairSet::const_iterator it=pairs.begin(); it!=pairs.end(); ++it) {
        int i = it->first;
        int j = it->second;
#endif
        int i_beg = BEG[i];
        int i_end = END[i];
        int i_len = i_end-i_beg;
        int j_beg = BEG[j];
        int j_end = END[j];
        int j_len = j_end-j_beg;
#if HAVE_SSE2
        int score = sg_scan_128_16(
                (const char*)&T[i_beg], i_len,
                (const char*)&T[j_beg], j_len,
                10, 1, blosum62__);
#else
        int score = sg_scan(
                (const char*)&T[i_beg], i_len,
                (const char*)&T[j_beg], j_len,
                10, 1, blosum62, tbl_pr, del_pr);
#endif
    }
    finish = timer_real();
    fprintf(stdout, "alignments: %.4f sec\n", finish-start);

    /* Deallocate table memory. */
    free(tbl_pr);
    free(del_pr);

    /* Done with input text. */
    free(T);

    return 0;
}


inline static void pair_check(
        int &count_generated,
        PairSet &pairs,
        const int &i,
        const int &j,
        const int * const restrict SA,
        const unsigned char * const restrict BWT,
        const int * const restrict SID,
        const char &sentinal)
{
    const int &sidi = SID[SA[i]];
    const int &sidj = SID[SA[j]];
    if (BWT[i] != BWT[j] || BWT[i] == sentinal) {
        if (sidi != sidj) {
            ++count_generated;
            if (sidi < sidj) {
                pairs.insert(make_pair(sidi,sidj));
            }
            else {
                pairs.insert(make_pair(sidj,sidi));
            }
        }
    }
}

/* try to reduce number of duplicate pairs generated */
/* we observe that l-intervals (i.e. internal nodes) always have at
 * least two children, but these children could be singleton
 * l-intervals, e.g., [i..j]=[1..1], in addition to l-intervals with
 * non-singleton ranges/quads. For each l-interval, we take the cross
 * product of its child l-intervals. Naively, we could take the cross
 * product of the entire lb/rb range of the l-interval, but this
 * generates too many duplicate pairs. Instead, the complexity should be
 * bounded by the number of exact matches...
 */
inline static void process(
        int &count_generated,
        PairSet &pairs,
        const quad &q,
        const int * const restrict SA,
        const unsigned char * const restrict BWT,
        const int * const restrict SID,
        const char &sentinal,
        const int &cutoff)
{
    static int used_cutoff = 0;
    const int n_children = q.children.size();
    int child_index = 0;

    if (q.lcp < cutoff) return;

    if (n_children) {
        for (int i=q.lb; i<=q.rb; ++i) {
            int j = i+1;
            if (child_index < n_children) {
                if (i >= q.children[child_index].lb) {
                    j = q.children[child_index].rb+1;
                    if (i >= q.children[child_index].rb) {
                        ++child_index;
                    }
                }
            }
            for (/*nope*/; j<=q.rb; ++j) {
                pair_check(count_generated, pairs, i, j, SA, BWT, SID, sentinal);
            }
        }
    }
    else {
        for (int i=q.lb; i<=q.rb; ++i) {
            for (int j=i+1; j<=q.rb; ++j) {
                pair_check(count_generated, pairs, i, j, SA, BWT, SID, sentinal);
            }
        }
    }
}

