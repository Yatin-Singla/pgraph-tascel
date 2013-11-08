/**
 * @file SuffixTree.cpp
 *
 * @author andy.cj.wu@gmail.com
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2010 Washington State University. All rights reserved.
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 */
#include "config.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>

using std::make_pair;
using std::pair;
using std::set;

#include "constants.h"
//#include "alignment.hpp"
#include "Parameters.hpp"
#include "Sequence.hpp"
#include "SequenceDatabase.hpp"
#include "SuffixBuckets.hpp"
#include "SuffixTree.hpp"

namespace pgraph {

/**
 * compute lset according to the left character of pid to
 * ensure the left maximality. Mark the special case of
 * whole sequence. Use BEGIN='O' as the special character.
 *
 * @param suffixes -
 * @param seqs - all seqs info
 * @param lset -
 */
static inline void
compute_lset(Suffix *suffixes, SequenceDatabase *seqs, Suffix **lset)
{
    Suffix *p = NULL;
    Suffix *q = NULL;
    int lIndex;

    for (p = suffixes; p != NULL; p = q) {
        q = p->next;
        if (p->pid == 0) {
            lIndex = BEGIN - 'A';
            p->next = lset[lIndex];
            lset[lIndex] = p;
        }
        else {
            lIndex = (*seqs)[p->sid][p->pid - 1] - 'A';
            p->next = lset[lIndex];
            lset[lIndex] = p;
        }
    }
}


/**
 * find the next different position of suffixes.
 * NOTE: if all suffixes end at '$', then report it as
 *       isLeaf = YES
 *
 * return -2: all dollar ending
 *        -1: error, cannot happen
 *         i: next diff position from starting point
 *
 * @param[in] seqs - all fasta seqs
 * @param[in] suffixes - bucket in linked list way
 * @param[in] depth - depth since root, which has 0 depth
 * @param[in] window_size - slide window size for bucketing
 * ---------------------------------------------------*/
static inline int
nextDiffPos(SequenceDatabase *seqs, Suffix *suffixes, int depth, int window_size)
{
    int i;
    Suffix *p = NULL;
    char pCh;
    char cCh;

    assert(suffixes != NULL);
    i = depth; /* position checked until this point */
    p = suffixes;

    while (1) {
        i++; /* step forward one more char for comparison */
        //std::cout << "i+p->pid=" << i+p->pid << " <? p->sid.size=" << (*seqs)[p->sid].get_sequence_length() << std::endl;
        assert((i + p->pid) < (*seqs)[p->sid].get_sequence_length());
        pCh = (*seqs)[p->sid][p->pid + i];

        for (p = p->next; p != NULL; p = p->next) {
            assert((i + p->pid) < (*seqs)[p->sid].get_sequence_length());
            cCh = (*seqs)[p->sid][p->pid + i];
            if (cCh != pCh) {
                return i;
            }
        }

        /* reset suffixes to next round of character comparison */
        p = suffixes;

        /* all suffixes ending in '$' */
        if (pCh == DOLLAR) {
            return DOL_END;
        }
    }
}


/**
 * TODO
 */
static inline size_t
build_tree_recursive(
    SequenceDatabase *sequences, size_t n_sequences,
    Suffix *suffixes, int depth, int window_size,
    SuffixTreeNode *st_nodes, size_t *st_index,
    double &size_internal, double &fanout,
    double &avgdepth, double &deepest,
    double &suffix_avg_length, double &suffix_max_length)
{
    int diffPos;
    size_t i;
    int j; /* store the index of the internal node */
    Suffix **heads = NULL;
    Suffix *p = NULL;
    Suffix *q = NULL;
    int hIndex;
    size_t rLeaf = SIZE_MAX;

    diffPos = nextDiffPos(sequences, suffixes, depth, window_size);
    if (diffPos == ERROR) { /* can never happen */
        fprintf(stderr, "wrong when exploring diff chars!");
        exit(EXIT_FAILURE);
    }
    else if (diffPos == DOL_END) { /* leaf node */
        Sequence &seq = (*sequences)[suffixes[0].sid];

        /* depth also includes the '$' */
        st_nodes[*st_index].depth = (seq.get_sequence_length() - 1) - suffixes[0].pid;
        st_nodes[*st_index].rLeaf = *st_index; /* point to itself */

        compute_lset(suffixes, sequences, st_nodes[*st_index].lset);

        return (*st_index)++;
    }
    else {  /* internal node */

        j = *st_index; /* store st_index in the stack */
        (*st_index)++;
        st_nodes[j].depth = diffPos - 1 ;

        avgdepth += st_nodes[j].depth;
        if (st_nodes[j].depth > deepest) {
            deepest = st_nodes[j].depth;
        }

        heads = st_nodes[j].lset;
        for (i = 0; i < SIGMA; i++) {
            heads[i] = NULL;
        }

#ifdef DEBUG
        printf("============================\n");
        print_suffixes(suffixes);
        printf("============================\n");
#endif


        /* partition suffixes into SIGMA sub-buckets */
        for (p = suffixes; p != NULL; p = q) {
            q = p->next;
            hIndex = (*sequences)[p->sid][p->pid + diffPos] - 'A';
            assert(hIndex >= 0 && (unsigned)hIndex < SIGMA);

            p->next = heads[hIndex];
            heads[hIndex] = p;
        }

#ifdef DEBUG
        for (i = 0; i < SIGMA; i++) {
            if (heads[i]) {
                print_suffixes(heads[i]);
            }
        }
#endif
        /* compute fanout */
        size_internal += 1.0;
        for (i = 0; i < SIGMA; i++) {
            if (heads[i]) {
                fanout += 1.0;
            }
        }

        /* recursively construct the tree in DFS way */
        for (i = 0; i < SIGMA; i++) {
            if (heads[i]) {
                st_nodes[j].nset[i] = &st_nodes[*st_index];
                /* branching with '$' */
                if (i == (DOLLAR - 'A')) {
                    st_nodes[*st_index].depth =
                        ((*sequences)[heads[i]->sid].get_sequence_length() - 1 - heads[i]->pid);
                    st_nodes[*st_index].rLeaf = *st_index;

                    compute_lset(heads[i], sequences, st_nodes[*st_index].lset);
                    rLeaf = (*st_index)++;
                }
                else {
                    rLeaf = build_tree_recursive(
                            sequences, n_sequences, heads[i], diffPos,
                            window_size, st_nodes, st_index,
                            size_internal, fanout,
                            avgdepth, deepest,
                            suffix_avg_length, suffix_max_length);
                }

                /* put it back into NULL */
                heads[i] = NULL;
            }
        }

        /* store the right most leaf in the internal node */
        st_nodes[j].rLeaf = rLeaf;
        return st_nodes[j].rLeaf;
    }
}


SuffixTree::SuffixTree(
        SequenceDatabase *sequences, Bucket *bucket, const Parameters &param)
    :   sequences(sequences)
    ,   bucket(bucket)
    ,   param(param)
    ,   nodes(NULL)
    ,   size(0)
    ,   lset_array(NULL)
    ,   nset_array(NULL)
    ,   fanout(0.0)
    ,   size_internal(0.0)
    ,   avgdepth(0.0)
    ,   deepest(0.0)
    ,   suffix_avg_length(0.0)
    ,   suffix_max_length(0.0)
{
    create();
}


void SuffixTree::create()
{
    size_t n_nodes = 0;
    size_t i = 0;

    /* allocate tree nodes */
    n_nodes = 2 * bucket->size;
    this->nodes = new SuffixTreeNode[n_nodes];
    if (NULL == this->nodes) {
        perror("build_tree: malloc tree nodes");
        exit(EXIT_FAILURE);
    }

    this->size = 0;

    /* allocate lset pointer memory for tree nodes */
    this->lset_array = new Suffix*[SIGMA * n_nodes];
    if (NULL == this->lset_array) {
        perror("build_tree: malloc lset array");
        exit(EXIT_FAILURE);
    }
    /* initialize lset pointer memory */
    for (i = 0; i < SIGMA * n_nodes; ++i) {
        this->lset_array[i] = NULL;
    }

    /* allocate nset pointer memory for tree nodes */
    this->nset_array = new SuffixTreeNode*[SIGMA * n_nodes];
    if (NULL == this->nset_array) {
        perror("build_tree: malloc nset array");
        exit(EXIT_FAILURE);
    }
    /* initialize nset pointer memory */
    for (i = 0; i < SIGMA * n_nodes; ++i) {
        this->nset_array[i] = NULL;
    }

    /* initialize tree nodes */
    for (i = 0; i < n_nodes; ++i) {
        this->nodes[i].depth = 0;
        this->nodes[i].rLeaf = 0;
        this->nodes[i].lset = &(this->lset_array[i*SIGMA]);
        this->nodes[i].nset = &(this->nset_array[i*SIGMA]);
    }

    /* gather suffix statistics */
    Suffix *p = NULL;
    Suffix *q = NULL;
    for (p = bucket->suffixes; p != NULL; p = q) {
        q = p->next;
        Sequence &seq = (*sequences)[p->sid];
        /* depth also includes the '$' */
        double len = (seq.get_sequence_length() - 1) - p->pid;
        this->suffix_avg_length += len;
        if (len > this->suffix_max_length) {
            this->suffix_max_length = len;
        }
    }
    if (this->suffix_avg_length > 0 && this->bucket->size > 0) {
        this->suffix_avg_length /= double(this->bucket->size);
    }

    build_tree_recursive(
            sequences, sequences->get_global_count(),
            bucket->suffixes, param.window_size - 1, param.window_size,
            this->nodes, &(this->size),
            this->size_internal, this->fanout,
            this->avgdepth, this->deepest,
            this->suffix_avg_length, this->suffix_max_length);

    if (this->fanout > 0 && this->size_internal > 0) {
        this->fanout /= this->size_internal;
    }
    if (this->avgdepth > 0 && this->size_internal > 0) {
        this->avgdepth /= this->size_internal;
    }
}


SuffixTree::~SuffixTree()
{
    delete [] this->nodes;
    delete [] this->lset_array;
    delete [] this->nset_array;
}


#if 0
static inline int
is_candidate(SequenceDatabase *seqs, size_t nSeqs,
             Suffix *p, Suffix *q,
             cell_t **tbl, int **ins, int **del, Parameters param)
{
    size_t s1Len = 0;
    size_t s2Len = 0;
    int f1 = 0;
    int f2 = 0;
    int cutOff = param.AOL * param.SIM;
    char result = FALSE;

    f1 = p->sid;
    f2 = q->sid;
    assert(f1 < nSeqs);
    assert(f2 < nSeqs);

    if (f1 == f2) {
        result = FALSE;
    }
    else {
        if (f1 > f2) {
            int swap = f1;
            f1 = f2;
            f2 = swap;
        }
        s1Len = (*seqs)[f1].get_sequence_length() - 1;
        s2Len = (*seqs)[f2].get_sequence_length() - 1;
        if (s1Len <= s2Len) {
            if (100 * s1Len < cutOff * s2Len) {
                result = FALSE;
            }
            else {
                result = TRUE;
            }
        }
        else {
            if (100 * s2Len < cutOff * s1Len) {
                result = FALSE;
            }
            else {
                result = TRUE;
            }
        }
    }

    return result;
}
#endif


#if 0
/**
 * counting sort based on the depth of the tree nodes.
 *
 * @param stNodes -
 * @param srtIndex -
 * @param nStNodes -
 * @param maxSeqLen -
 * -----------------------------------------------------------*/
static inline void
count_sort(SuffixTreeNode *stNodes, int *srtIndex, size_t nStNodes, size_t maxSeqLen)
{
    size_t i;
    int *cnt = NULL;
    int depth;

    cnt = new int[maxSeqLen];
    if (NULL == cnt) {
        perror("count_sort: malloc cnt");
        exit(EXIT_FAILURE);
    }

    /* init counters */
    for (i = 0; i < maxSeqLen; i++) {
        cnt[i] = 0;
    }

    /* fill in counters */
    for (i = 0; i < nStNodes; i++) {
        depth = stNodes[i].depth; /* depth starts from 0 */
        assert(depth < maxSeqLen);
        cnt[depth]++;
    }

    /* suffix sum to sort in a reverse way */
    for (i = maxSeqLen - 2; i > 0; i--) {
        cnt[i] += cnt[i + 1];
    }
    cnt[0] += cnt[1];

    /* store the sorted index into srtIndex */
    for (i = 0; i < nStNodes; i++) {
        depth = stNodes[i].depth;
        srtIndex[cnt[depth] - 1] = i;
        cnt[depth]--;
    }

    delete [] cnt;
}
#endif


static inline void
procLeaf(Suffix **lset, SequenceDatabase *seqs, int nSeqs, cell_t **tbl, int **ins, int **del, Parameters param, set<pair<size_t,size_t> > &pairs)
{
    size_t i;
    size_t j;
    Suffix *p = NULL;
    Suffix *q = NULL;
    int cutOff;

    cutOff = param.AOL * param.SIM;

    for (i = 0; i < SIGMA; i++) {
        if (lset[i]) {
            if (i == BEGIN - 'A') { /* inter cross */
                for (p = lset[i]; p != NULL; p = p->next) {
                    for (q = p->next; q != NULL; q = q->next) {
                        if (TRUE == is_candidate(seqs, nSeqs, p, q, tbl, ins, del, param)) {
                            //printf("edge:%s#%s\n", seqs[p->sid].gid, seqs[q->sid].gid);
                            if (p->sid > q->sid) {
                                //printf("edge\t%zu\t%zu\n", q->sid, p->sid);
                                pairs.insert(make_pair(q->sid, p->sid));
                            }
                            else {
                                //printf("edge\t%zu\t%zu\n", p->sid, q->sid);
                                pairs.insert(make_pair(p->sid, q->sid));
                            }
                        }
                    }
                }
            }

            /* intra cross */
            for (j = i + 1; j < SIGMA; j++) {
                if (lset[j]) {
                    for (p = lset[i]; p != NULL; p = p->next) {
                        for (q = lset[j]; q != NULL; q = q->next) {
                            if (TRUE == is_candidate(seqs, nSeqs, p, q, tbl, ins, del, param)) {
                                //printf("edge:%s#%s\n", seqs[p->sid].gid, seqs[q->sid].gid);
                                if (p->sid > q->sid) {
                                    //printf("edge\t%zu\t%zu\n", q->sid, p->sid);
                                    pairs.insert(make_pair(q->sid, p->sid));
                                }
                                else {
                                    //printf("edge\t%zu\t%zu\n", p->sid, q->sid);
                                    pairs.insert(make_pair(p->sid, q->sid));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}



void SuffixTree::generate_pairs(set<pair<size_t,size_t> > &pairs)
{
    SuffixTreeNode *stNodes = NULL;
    int *srtIndex = NULL;
    size_t nStNodes = 0;
    int nSeqs = 0;
    int maxSeqLen = 0;
    size_t i = 0;
    int j = 0;
    SuffixTreeNode *stnode = NULL;
    int sIndex;
    int eIndex;
    cell_t **tbl = NULL;
    int **del = NULL;
    int **ins = NULL;
    size_t m;
    size_t n;
    size_t s;
    size_t t;
    Suffix *p = NULL;
    Suffix *q = NULL;
    int EM;
    int cutOff; /* cut off value of filter 1 */

    srtIndex = new int[this->size];
    count_sort(this->nodes, srtIndex, this->size, sequences->get_max_length());
    stNodes = this->nodes;
    nStNodes = this->size;
    nSeqs = sequences->get_global_count();
    maxSeqLen = sequences->get_max_length();

    /* only two rows are allocated */
    assert(NROW == 2);

    EM = param.exact_match_len;
    cutOff = param.AOL * param.SIM;

    tbl = allocate_cell_table(NROW, maxSeqLen);
    del = allocate_int_table(NROW, maxSeqLen);
    ins = allocate_int_table(NROW, maxSeqLen);


    /* srtIndex maintain an order of NON-increasing depth of stNodes[] */
    for (i = 0; i < nStNodes; i++) {
        sIndex = srtIndex[i];
        stnode = &stNodes[sIndex];

#ifdef DEBUG
        printf("stNode->depth=%d, stnode->rLeaf=%ld, sIndex=%ld\n", stnode->depth, stnode->rLeaf, sIndex);
#endif

        if (stnode->depth >= EM - 1) {
            if (stnode->rLeaf == sIndex) { /* leaf node */
                procLeaf(stnode->lset, sequences, nSeqs, tbl, del, ins, param, pairs);
            }
            else {                       /* internal node */
                eIndex = stnode->rLeaf;

                /* pairs generation loop for internal node */
#if 0
                for (m = sIndex + 1; m < eIndex; m++) {
                    for (n = m + 1; n <= eIndex; n++) {
                        for (s = 0; s < SIGMA; s++) {
                            for (t = 0; t < SIGMA; t++) {
                                if (s != t) {
                                    for (p = stNodes[m].lset[s]; p != NULL; p = p->next) {
                                        for (q = stNodes[n].lset[t]; q != NULL; q = q->next) {
                                            if (TRUE == is_candidate(
                                                        sequences, nSeqs, p, q,
                                                        tbl, ins, del,
                                                        param)) {
                                                //printf("edge:%s#%s\n",
                                                //(*sequences)[p->sid].gid,
                                                //(*sequences)[q->sid].gid);
                                                if (p->sid > q->sid) {
                                                    //printf("edge\t%zu\t%zu\n",
                                                    //        q->sid, p->sid);
                                                    pairs.insert(make_pair(q->sid, p->sid));
                                                }
                                                else {
                                                    //printf("edge\t%zu\t%zu\n",
                                                    //        p->sid, q->sid);
                                                    pairs.insert(make_pair(p->sid, q->sid));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
#else
                /* only loop over immediate children nodes */
                SuffixTreeNode *current_node = &stNodes[sIndex];
                for (m = 0; m < SIGMA; m++) {
                    SuffixTreeNode *x = current_node->nset[m];
                    if (NULL == x) {
                        continue;
                    }
                    for (n = m + 1; n < SIGMA; n++) {
                        SuffixTreeNode *y = current_node->nset[n];
                        if (NULL == y) {
                            continue;
                        }
                        for (s = 0; s < SIGMA; s++) {
                            for (t = 0; t < SIGMA; t++) {
                                if (s != t) {
                                    for (p = x->lset[s]; p != NULL; p = p->next) {
                                        for (q = y->lset[t]; q != NULL; q = q->next) {
                                            if (TRUE == is_candidate(
                                                        sequences, nSeqs, p, q,
                                                        tbl, ins, del,
                                                        param)) {
                                                if (p->sid > q->sid) {
                                                    pairs.insert(make_pair(q->sid, p->sid));
                                                }
                                                else {
                                                    pairs.insert(make_pair(p->sid, q->sid));
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
#endif

                /* merge the lsets of subtree */
                for (m = 0; m < SIGMA; m++) {
                    p = NULL;
                    for (j = sIndex + 1; j <= eIndex; j++) {
                        if ((q = stNodes[j].lset[m])) {

                            /* empty the subtree's ptrs array */
                            stNodes[j].lset[m] = NULL;
                            if (p == NULL) {
                                p = q;
                                stNodes[sIndex].lset[m] = q;
                            }
                            else {
                                p->next = q;
                            }

                            /* walk to the end */
                            while (p->next) {
                                p = p->next;
                            }
                        }
                    }
                }
            }
        }
        else {
            /* stnodes are sorted, so later part
             * will not satisfy EM cutoff */
            break;
        }
    }

    /* free */
    delete [] srtIndex;
    free_cell_table(tbl, NROW);
    free_int_table(del, NROW);
    free_int_table(ins, NROW);
}

}; /* namespace pgraph */

