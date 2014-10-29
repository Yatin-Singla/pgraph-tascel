#include "config.h"

#include <stdlib.h>

#include <emmintrin.h>

#ifdef ALIGN_EXTRA
#include "align/align_wozniak_128_16_debug.h"
#else
#include "align/align_wozniak_128_16.h"
#endif
#include "blosum/blosum_map.h"


/* shift given vector v, insert val, return shifted val */
static inline __m128i vshift16(const __m128i v, const int val)
{
    __m128i ret = _mm_srli_si128(v, 2);
    ret = _mm_insert_epi16(ret, val, 7);
    return ret;
}


#ifdef ALIGN_EXTRA
static inline void arr_store_si128(
        int *array,
        __m128i vWscore,
        int i,
        int s1Len,
        int j,
        int s2Len)
{
    if (0 <= i+0 && i+0 < s1Len && 0 <= j-0 && j-0 < s2Len) {
        array[(i+0)*s2Len + (j-0)] = (int16_t)_mm_extract_epi16(vWscore, 7);
    }
    if (0 <= i+1 && i+1 < s1Len && 0 <= j-1 && j-1 < s2Len) {
        array[(i+1)*s2Len + (j-1)] = (int16_t)_mm_extract_epi16(vWscore, 6);
    }
    if (0 <= i+2 && i+2 < s1Len && 0 <= j-2 && j-2 < s2Len) {
        array[(i+2)*s2Len + (j-2)] = (int16_t)_mm_extract_epi16(vWscore, 5);
    }
    if (0 <= i+3 && i+3 < s1Len && 0 <= j-3 && j-3 < s2Len) {
        array[(i+3)*s2Len + (j-3)] = (int16_t)_mm_extract_epi16(vWscore, 4);
    }
    if (0 <= i+4 && i+4 < s1Len && 0 <= j-4 && j-4 < s2Len) {
        array[(i+4)*s2Len + (j-4)] = (int16_t)_mm_extract_epi16(vWscore, 3);
    }
    if (0 <= i+5 && i+5 < s1Len && 0 <= j-5 && j-5 < s2Len) {
        array[(i+5)*s2Len + (j-5)] = (int16_t)_mm_extract_epi16(vWscore, 2);
    }
    if (0 <= i+6 && i+6 < s1Len && 0 <= j-6 && j-6 < s2Len) {
        array[(i+6)*s2Len + (j-6)] = (int16_t)_mm_extract_epi16(vWscore, 1);
    }
    if (0 <= i+7 && i+7 < s1Len && 0 <= j-7 && j-7 < s2Len) {
        array[(i+7)*s2Len + (j-7)] = (int16_t)_mm_extract_epi16(vWscore, 0);
    }
}
#endif


#ifdef ALIGN_EXTRA
#define FNAME sg_stats_wozniak_128_16_debug
#else
#define FNAME sg_stats_wozniak_128_16
#endif
int FNAME(
        const char * const restrict _s1, const int s1Len,
        const char * const restrict _s2, const int s2Len,
        const int open, const int gap,
        const int matrix[24][24],
        int * const restrict _matches, int * const restrict _length,
        int * const restrict _tbl_pr, int * const restrict _del_pr,
        int * const restrict _mch_pr, int * const restrict _len_pr
#ifdef ALIGN_EXTRA
        , int * const restrict score_table
        , int * const restrict match_table
        , int * const restrict length_table
#endif
        )
{
    const int N = 8; /* number of values in vector */
    const int PAD = N-1; /* N 16-byte values in vector, so N - 1 */
    const int PAD2 = PAD*2;
    int * const restrict s1 = (int * const restrict)malloc(sizeof(int)*(s1Len+PAD));
    int * const restrict s2B= (int * const restrict)malloc(sizeof(int)*(s2Len+PAD2));
    int * const restrict s2 = s2B+PAD; /* will allow later for negative indices */
    int i = 0;
    int j = 0;
    int score = NEG_INF_16;
    int match = NEG_INF_16;
    int length = NEG_INF_16;
    int * const restrict tbl_pr = _tbl_pr+PAD;
    int * const restrict del_pr = _del_pr+PAD;
    int * const restrict mch_pr = _mch_pr+PAD;
    int * const restrict len_pr = _len_pr+PAD;
    __m128i vNegInf = _mm_set1_epi16(NEG_INF_16);
    __m128i vNegInf0 = _mm_srli_si128(vNegInf, 2); /* shift in a 0 */
    __m128i vOpen = _mm_set1_epi16(open);
    __m128i vGap  = _mm_set1_epi16(gap);
    __m128i vZero = _mm_set1_epi16(0);
    __m128i vOne = _mm_set1_epi16(1);
    __m128i vN = _mm_set1_epi16(N);
    __m128i vNegOne = _mm_set1_epi16(-1);
    __m128i vI = _mm_set_epi16(0,1,2,3,4,5,6,7);
    __m128i vJreset = _mm_set_epi16(0,-1,-2,-3,-4,-5,-6,-7);
    __m128i vMaxScore = vNegInf;
    __m128i vMaxMatch = vNegInf;
    __m128i vMaxLength = vNegInf;
    __m128i vILimit = _mm_set1_epi16(s1Len);
    __m128i vILimit1 = _mm_sub_epi16(vILimit, vOne);
    __m128i vJLimit = _mm_set1_epi16(s2Len);
    __m128i vJLimit1 = _mm_sub_epi16(vJLimit, vOne);

    /* convert _s1 from char to int in range 0-23 */
    for (i=0; i<s1Len; ++i) {
        s1[i] = MAP_BLOSUM_[(unsigned char)_s1[i]];
    }
    /* pad back of s1 with dummy values */
    for (i=s1Len; i<s1Len+PAD; ++i) {
        s1[i] = 0; /* point to first matrix row because we don't care */
    }

    /* convert _s2 from char to int in range 0-23 */
    for (j=0; j<s2Len; ++j) {
        s2[j] = MAP_BLOSUM_[(unsigned char)_s2[j]];
    }
    /* pad front of s2 with dummy values */
    for (j=-PAD; j<0; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }
    /* pad back of s2 with dummy values */
    for (j=s2Len; j<s2Len+PAD; ++j) {
        s2[j] = 0; /* point to first matrix row because we don't care */
    }

    /* set initial values for stored row */
    for (j=0; j<s2Len; ++j) {
        tbl_pr[j] = 0;
        del_pr[j] = NEG_INF_16;
        mch_pr[j] = 0;
        len_pr[j] = 0;
    }
    /* pad front of stored row values */
    for (j=-PAD; j<0; ++j) {
        tbl_pr[j] = NEG_INF_16;
        del_pr[j] = NEG_INF_16;
        mch_pr[j] = 0;
        len_pr[j] = 0;
    }
    /* pad back of stored row values */
    for (j=s2Len; j<s2Len+PAD; ++j) {
        tbl_pr[j] = NEG_INF_16;
        del_pr[j] = NEG_INF_16;
        mch_pr[j] = 0;
        len_pr[j] = 0;
    }

    /* iterate over query sequence */
    for (i=0; i<s1Len-N; i+=N) {
        __m128i vNscore = vNegInf0;
        __m128i vNmatch = vZero;
        __m128i vNlength = vZero;
        __m128i vWscore = vNegInf0;
        __m128i vWmatch = vZero;
        __m128i vWlength = vZero;
        __m128i vIns = vNegInf;
        __m128i vDel = vNegInf;
        __m128i vJ = vJreset;
        __m128i vs1 = _mm_set_epi16(
                s1[i+0],
                s1[i+1],
                s1[i+2],
                s1[i+3],
                s1[i+4],
                s1[i+5],
                s1[i+6],
                s1[i+7]);
        __m128i vs2 = vNegInf;
        const int * const restrict matrow0 = matrix[s1[i+0]];
        const int * const restrict matrow1 = matrix[s1[i+1]];
        const int * const restrict matrow2 = matrix[s1[i+2]];
        const int * const restrict matrow3 = matrix[s1[i+3]];
        const int * const restrict matrow4 = matrix[s1[i+4]];
        const int * const restrict matrow5 = matrix[s1[i+5]];
        const int * const restrict matrow6 = matrix[s1[i+6]];
        const int * const restrict matrow7 = matrix[s1[i+7]];
        /* iterate over database sequence */
        for (j=0; j<N; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                __m128i cond = _mm_cmpeq_epi16(vJ,vNegOne);
                vWscore = _mm_andnot_si128(cond, vWscore);
                vWmatch = _mm_andnot_si128(cond, vWmatch);
                vWlength = _mm_andnot_si128(cond, vWlength);
                vDel = _mm_andnot_si128(cond, vDel);
                vDel = _mm_or_si128(vDel, _mm_and_si128(cond, vNegInf));
                vIns = _mm_andnot_si128(cond, vIns);
                vIns = _mm_or_si128(vIns, _mm_and_si128(cond, vNegInf));
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            vJ = _mm_add_epi16(vJ, vOne);
        }
        for (j=N; j<s2Len-1; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            vJ = _mm_add_epi16(vJ, vOne);
        }
        for (j=s2Len-1; j<s2Len+PAD; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            /* as minor diagonal vector passes across the j limit
             * boundary, extract the last value of the row */
            {
                __m128i cond_j = _mm_cmpeq_epi16(vJ, vJLimit1);
                __m128i cond_max = _mm_cmpgt_epi16(vWscore, vMaxScore);
                __m128i cond_all = _mm_and_si128(cond_max, cond_j);
                vMaxScore = _mm_andnot_si128(cond_all, vMaxScore);
                vMaxScore = _mm_or_si128(vMaxScore,
                        _mm_and_si128(cond_all, vWscore));
                vMaxMatch = _mm_andnot_si128(cond_all, vMaxMatch);
                vMaxMatch = _mm_or_si128(vMaxMatch,
                        _mm_and_si128(cond_all, vWmatch));
                vMaxLength = _mm_andnot_si128(cond_all, vMaxLength);
                vMaxLength = _mm_or_si128(vMaxLength,
                        _mm_and_si128(cond_all, vWlength));
            }
            vJ = _mm_add_epi16(vJ, vOne);
        }
        vI = _mm_add_epi16(vI, vN);
    }
    for (/*i=?*/; i<s1Len; i+=N) {
        __m128i vNscore = vNegInf0;
        __m128i vNmatch = vZero;
        __m128i vNlength = vZero;
        __m128i vWscore = vNegInf0;
        __m128i vWmatch = vZero;
        __m128i vWlength = vZero;
        __m128i vIns = vNegInf;
        __m128i vDel = vNegInf;
        __m128i vJ = vJreset;
        __m128i vs1 = _mm_set_epi16(
                s1[i+0],
                s1[i+1],
                s1[i+2],
                s1[i+3],
                s1[i+4],
                s1[i+5],
                s1[i+6],
                s1[i+7]);
        __m128i vs2 = vNegInf;
        const int * const restrict matrow0 = matrix[s1[i+0]];
        const int * const restrict matrow1 = matrix[s1[i+1]];
        const int * const restrict matrow2 = matrix[s1[i+2]];
        const int * const restrict matrow3 = matrix[s1[i+3]];
        const int * const restrict matrow4 = matrix[s1[i+4]];
        const int * const restrict matrow5 = matrix[s1[i+5]];
        const int * const restrict matrow6 = matrix[s1[i+6]];
        const int * const restrict matrow7 = matrix[s1[i+7]];
        __m128i vIltLimit = _mm_cmplt_epi16(vI, vILimit);
        __m128i vIeqLimit1 = _mm_cmpeq_epi16(vI, vILimit1);
        /* iterate over database sequence */
        for (j=0; j<N; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
            /* as minor diagonal vector passes across the j=-1 boundary,
             * assign the appropriate boundary conditions */
            {
                __m128i cond = _mm_cmpeq_epi16(vJ,vNegOne);
                vWscore = _mm_andnot_si128(cond, vWscore);
                vWmatch = _mm_andnot_si128(cond, vWmatch);
                vWlength = _mm_andnot_si128(cond, vWlength);
                vDel = _mm_andnot_si128(cond, vDel);
                vDel = _mm_or_si128(vDel, _mm_and_si128(cond, vNegInf));
                vIns = _mm_andnot_si128(cond, vIns);
                vIns = _mm_or_si128(vIns, _mm_and_si128(cond, vNegInf));
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            /* as minor diagonal vector passes across the i limit
             * boundary, extract the last value of the column */
            {
                __m128i cond_i = _mm_and_si128(
                        vIeqLimit1,
                        _mm_cmpgt_epi16(vJ, vNegOne));
                __m128i cond_max = _mm_cmpgt_epi16(vWscore, vMaxScore);
                __m128i cond_all = _mm_and_si128(cond_max, cond_i);
                vMaxScore = _mm_andnot_si128(cond_all, vMaxScore);
                vMaxScore = _mm_or_si128(vMaxScore,
                        _mm_and_si128(cond_all, vWscore));
                vMaxMatch = _mm_andnot_si128(cond_all, vMaxMatch);
                vMaxMatch = _mm_or_si128(vMaxMatch,
                        _mm_and_si128(cond_all, vWmatch));
                vMaxLength = _mm_andnot_si128(cond_all, vMaxLength);
                vMaxLength = _mm_or_si128(vMaxLength,
                        _mm_and_si128(cond_all, vWlength));
            }
            vJ = _mm_add_epi16(vJ, vOne);
        }
        for (j=N; j<s2Len-1; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            /* as minor diagonal vector passes across the i limit
             * boundary, extract the last value of the column */
            {
                __m128i cond_i = vIeqLimit1;
                __m128i cond_max = _mm_cmpgt_epi16(vWscore, vMaxScore);
                __m128i cond_all = _mm_and_si128(cond_max, cond_i);
                vMaxScore = _mm_andnot_si128(cond_all, vMaxScore);
                vMaxScore = _mm_or_si128(vMaxScore,
                        _mm_and_si128(cond_all, vWscore));
                vMaxMatch = _mm_andnot_si128(cond_all, vMaxMatch);
                vMaxMatch = _mm_or_si128(vMaxMatch,
                        _mm_and_si128(cond_all, vWmatch));
                vMaxLength = _mm_andnot_si128(cond_all, vMaxLength);
                vMaxLength = _mm_or_si128(vMaxLength,
                        _mm_and_si128(cond_all, vWlength));
            }
            vJ = _mm_add_epi16(vJ, vOne);
        }
        for (j=s2Len-1; j<s2Len+PAD; ++j) {
            __m128i vMat;
            __m128i vNWscore = vNscore;
            __m128i vNWmatch = vNmatch;
            __m128i vNWlength = vNlength;
            vNscore = vshift16(vWscore, tbl_pr[j]);
            vNmatch = vshift16(vWmatch, mch_pr[j]);
            vNlength = vshift16(vWlength, len_pr[j]);
            vDel = vshift16(vDel, del_pr[j]);
            vDel = _mm_max_epi16(
                    _mm_sub_epi16(vNscore, vOpen),
                    _mm_sub_epi16(vDel, vGap));
            vIns = _mm_max_epi16(
                    _mm_sub_epi16(vWscore, vOpen),
                    _mm_sub_epi16(vIns, vGap));
            vs2 = vshift16(vs2, s2[j]);
            vMat = _mm_set_epi16(
                    matrow0[s2[j-0]],
                    matrow1[s2[j-1]],
                    matrow2[s2[j-2]],
                    matrow3[s2[j-3]],
                    matrow4[s2[j-4]],
                    matrow5[s2[j-5]],
                    matrow6[s2[j-6]],
                    matrow7[s2[j-7]]
                    );
            vNWscore = _mm_add_epi16(vNWscore, vMat);
            vWscore = _mm_max_epi16(vNWscore, vIns);
            vWscore = _mm_max_epi16(vWscore, vDel);
            /* conditional block */
            {
                __m128i case1not;
                __m128i case2not;
                __m128i case2;
                __m128i case3;
                __m128i vCmatch;
                __m128i vClength;
                case1not = _mm_or_si128(
                        _mm_cmplt_epi16(vNWscore,vDel),
                        _mm_cmplt_epi16(vNWscore,vIns));
                case2not = _mm_cmplt_epi16(vDel,vIns);
                case2 = _mm_andnot_si128(case2not,case1not);
                case3 = _mm_and_si128(case1not,case2not);
                vCmatch = _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWmatch, _mm_and_si128(
                                _mm_cmpeq_epi16(vs1,vs2),vOne)));
                vClength= _mm_andnot_si128(case1not,
                        _mm_add_epi16(vNWlength, vOne));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case2, vNmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case2,
                            _mm_add_epi16(vNlength, vOne)));
                vCmatch = _mm_or_si128(vCmatch, _mm_and_si128(case3, vWmatch));
                vClength= _mm_or_si128(vClength,_mm_and_si128(case3,
                            _mm_add_epi16(vWlength, vOne)));
                vWmatch = vCmatch;
                vWlength = vClength;
            }
#ifdef ALIGN_EXTRA
            arr_store_si128(score_table, vWscore, i, s1Len, j, s2Len);
            arr_store_si128(match_table, vWmatch, i, s1Len, j, s2Len);
            arr_store_si128(length_table, vWlength, i, s1Len, j, s2Len);
#endif
            tbl_pr[j-7] = (int16_t)_mm_extract_epi16(vWscore,0);
            mch_pr[j-7] = (int16_t)_mm_extract_epi16(vWmatch,0);
            len_pr[j-7] = (int16_t)_mm_extract_epi16(vWlength,0);
            del_pr[j-7] = (int16_t)_mm_extract_epi16(vDel,0);
            /* as minor diagonal vector passes across the i or j limit
             * boundary, extract the last value of the column or row */
            {
                __m128i cond_j = _mm_and_si128(
                        vIltLimit,
                        _mm_cmpeq_epi16(vJ, vJLimit1));
                __m128i cond_i = _mm_and_si128(
                        vIeqLimit1,
                        _mm_cmplt_epi16(vJ, vJLimit));
                __m128i cond_max = _mm_cmpgt_epi16(vWscore, vMaxScore);
                __m128i cond_all = _mm_and_si128(cond_max,
                        _mm_or_si128(cond_i, cond_j));
                vMaxScore = _mm_andnot_si128(cond_all, vMaxScore);
                vMaxScore = _mm_or_si128(vMaxScore,
                        _mm_and_si128(cond_all, vWscore));
                vMaxMatch = _mm_andnot_si128(cond_all, vMaxMatch);
                vMaxMatch = _mm_or_si128(vMaxMatch,
                        _mm_and_si128(cond_all, vWmatch));
                vMaxLength = _mm_andnot_si128(cond_all, vMaxLength);
                vMaxLength = _mm_or_si128(vMaxLength,
                        _mm_and_si128(cond_all, vWlength));
            }
            vJ = _mm_add_epi16(vJ, vOne);
        }
        vI = _mm_add_epi16(vI, vN);
    }

    /* max in vMaxScore */
    for (i=0; i<N; ++i) {
        int16_t value;
        value = (int16_t) _mm_extract_epi16(vMaxScore, 7);
        if (value > score) {
            score = value;
            match = (int16_t) _mm_extract_epi16(vMaxMatch, 7);
            length= (int16_t) _mm_extract_epi16(vMaxLength,7);
        }
        vMaxScore = _mm_slli_si128(vMaxScore, 2);
        vMaxMatch = _mm_slli_si128(vMaxMatch, 2);
        vMaxLength = _mm_slli_si128(vMaxLength, 2);
    }

    free(s1);
    free(s2B);

    *_matches = match;
    *_length = length;
    return score;
}

