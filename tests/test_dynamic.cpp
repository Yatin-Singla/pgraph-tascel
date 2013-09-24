/**
 * @file test_dynamic.c
 *
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 */
#include "config.h"

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "alignment.hpp"
#include "constants.h"
#include "csequence.h"
#include "param.hpp"
#include "timer.h"

using namespace pgraph;

static const char seq0[] = 
"MMVLEIRNVAVIGAGSMGHAIAEVVAIHGFNVKLMDVSEDQLKRAMEKIEEGLRKSYERGYISEDPEKVLKRIEATADLIEVAKDADLVIEAIPEIFDLKKKVFSEIEQYCPDHTIFATNTSSLSITKLAEATKRPEKFIGMHFFNPPKILKLLEIVWGEKTSEETIRIVEDFARKIDRIIIHVRKDVPGFIVNRIFVTMSNEASWAVEMGEGTIEEIDSAVKYRLGLPMGLFELHDVLGGGSVDVSYHVLEYYRQTLGESYRPSPLFERLFKAGHYGKKTGKGFYDWSEGKTNEVPLRAGANFDLLRLVAPAVNEAAWLIEKGVASAEEIDLAVLHGLNYPRGLLRMADDFGIDSIVKKLNELYEKYNGEERYKVNPVLQKMVEEGKLGRTTGEGFYKYGD";

static const char seq19[] =
"MKVKKVCVLGAGAMGSGIAQVCATAGYEVWVRDIKQEFLDRGKAAIEKNLQKAVSKGKMTEEKAKEILSRIHFTLDMEEAVKDADLVIEAVPEIMDLKKQVFAEVQKYAKPECIFASNTSGLSITELGNATDRPEKFLGLHFFNPPPVMALVEVIKGEKTSDETIKFGVEFVKSLGKVPVVVKKDVAGFIVNRILVPYLVLAIDDVEKGVATKEEIDATMMYKYGFPMGPIELSDFVGLDILYHASQQWDIVPQSKLLEEKFKANELGMKTGKGFYDWSAGRPKIPQELAGKYDAIRLIAPMVNIAADLIAMGVADAKDIDTAMKLGTNMPKGPCELGDEIGLDVILAKVEELYKEKGFEILKPSEHLKKMVSEGKLGEKSGEGFYSYGK";

static const char seq47[] = 
"MLLLPSRGKLYVVGIGPGKEELMTLKAKRAIEEADYIVGYQTYVDRISHLIEGKKVVTTPMRKELDRVKIALELAKEHVVALISGGDPSIYGILPLVIEYAVEKKVDVEIEAIPGVTAASAASSLLGSAISGDFAVVSLSDLLVPWSVVEKRLLYALSGDFVVAIYNPSSRRRKENFRKAMEIVRRFRGDAWVGVVRNAGREGQQVEIRRVSEVDEVDMNTILIVGNSETKVVDGKMFTPRGYSNKYNIG";

static const char seq1576[] =
"DQADMVRRVKNYENGFINNPIVISPTTTVGEAKSMKEKYGFAGFPVTADGKRNAKLVGAITSRDIQFVEDNSLLVQDVMTKNPVTGAQGITLSEGNEILKKIKKGRLLVVDEKGNLVSMLSRTDLMKNQKYPLASKSANTKQLLWGASIGTMDADKERLRLLVKAGLDVVILDS";

static const char seq1800[] =
"NPIVISPTTTVGEAKSMKERFGFSGFPVTEDGKRNGKLMGIVTSRDIQFVEDNSLLVQDVMTKNPVTGAQGITLSEGNEILKKIKKGKLLIVDDNGNLVSMLSRTDLMKN";

static const char seq_original1[] =
"MGDSSKKVKDSFDTISEPDSFDEPKGVPISMEPVFSTAAGIRIDVKQESIDKSKKMLNSDLKSKSSSKGG"
"FSSPLVRKNNGSSAFVSPFRREGTSSTTTKRPASGGFEDFEAPPAKKSTSSSSKKSKKHSKKEKKKEFKE"
"IHADVLRVSRIYEKDKFRIILQESSSTPLILATCSYNRGSDIKFGDRIHVDAEVCKKSSSGDVTEIYIDR"
"VLKNKENGAKSGIRRHSIAKKPFCIKPRFIHELSDTKIKKTVVQVNLLDLNLDFYAGCSKCKHSLPEAAN"
"QCEFCKDSQGKSELSMYSRVRVMDFSGQMFINVTTKNMKKLLDLLGYEGFDNWFRFKDPQERQNYVFRPV"
"MVEIEKSNDEWECTDVAEVDWKDFGSYLKHKEDKKKRRSKKKHP";

static const char seq_original2[] =
"MADVALRITETVARLQKELKCGICCSTYKDPILSTCFHIFCRSCINACFERKRKVQCPICRSVLDKRSCR"
"DTYQITMAVQNYLKLSEAFKKDIENMNTFKSLPPEKMFMESQMPLDITIIPENDGKRCAPDFAIPFLPVR"
"RKRPSRPQPPSAFAEEPAEPVEPPEPATKQPVELQSRVFPLEKLKKDVETSTETYKISREELKNVDIEEY"
"INTLRENSTEIDEIDALFQLMPTMRQFLRNNINQLMEKFHVAPPKKSEKPANRRVSFASSQDLENIKIMT"
"ASESLETPPEPIQKLAQKPEVFKSTQNLIDLNLNTAVKKPVVVASDDDEVVEDSEGELQIDEDDLANVTC"
"ATSVRNIGKSLCAEYIREGRSISQKSTAYLYAIARKCVIVGRQWLVDCITTGLLLSEADYTITSCSSTIP"
"VKIPPSIGSEMGWLRSRNDEHGKLFAGRRFMILRKFTMNPYFDYKQLIELVQQCGGEILSCYENLSPEKL"
"YIIFSKHSKAIEESKNIENLYKCDVVTMEWVLDSISEYLILPTQPYKAVDSIGCLQD";

int test(const char *seq1, const char *seq2)
{
    size_t i = 0;
    cell_t **tbl = NULL;
    int **del = NULL;
    int **ins = NULL;
    size_t seq1_len = strlen(seq1);
    size_t seq2_len = strlen(seq2);
    int max_seq_len = seq1_len > seq2_len ? seq1_len : seq2_len;
    cell_t result;
    param_t param;
    bool is_edge_answer = false;
    int sscore;
    size_t maxLen;

    assert(NROW == 2);
    timer_init();
    tbl = allocate_cell_table(NROW, max_seq_len);
    del = allocate_int_table(NROW, max_seq_len);
    ins = allocate_int_table(NROW, max_seq_len);

    result = affine_gap_align_blosum(
            seq1, seq1_len,
            seq2, seq2_len,
            tbl, del, ins,
            -10, -1);
    param.AOL = 8;
    param.SIM = 4;
    param.OS = 3;
    is_edge_answer = is_edge_blosum(result,
            seq1, seq1_len,
            seq2, seq2_len,
            param.AOL, param.SIM, param.OS,
            sscore, maxLen);
    printf("---------------------------------------------------\n");
    printf("result->score=%d\n", result.score);
    printf("result->matches=%d\n", result.matches);
    printf("result->length=%d\n", result.length);
    printf("is_edge_answer=%d\n", is_edge_answer);
    printf("length/maxLen=%f\n", 1.0*result.length/maxLen);
    printf("nmatch/length=%f\n", 1.0*result.matches/result.length);
    printf("score/sscore=%f\n", 1.0*result.score/sscore);
    printf("timing 10000 calls to pgraph_affine_gap_align\n");
    unsigned long long t = timer_start();
    for (i = 0; i < 10000U; ++i) {
        result = affine_gap_align_blosum(
                seq1, seq1_len,
                seq2, seq2_len,
                tbl, del, ins,
                -10, -1);
    }
    t = timer_end(t);
    printf("%s timer took %llu units\n", timer_name(), t);

    free_cell_table(tbl, NROW);
    free_int_table(del, NROW);
    free_int_table(ins, NROW);

    return 0;
}


int main(int argc, char **argv)
{
    //test(seq_original1, seq_original2);
    //test(seq0, seq19);
    //test(seq0, seq47);
    test(seq1576, seq1800);
    return 0;
}
