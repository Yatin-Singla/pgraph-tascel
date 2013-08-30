/**
 * @file Parameters.hpp
 *
 * @author andy.cj.wu@gmail.com
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2010 Washington State University. All rights reserved.
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 */
#ifndef _PGRAPH_PARAMETERS_H_
#define _PGRAPH_PARAMETERS_H_

#include <mpi.h>

namespace pgraph {

/**
 * Parameters for various parts of the pgraph software.
 */
class Parameters
{
public:

    /**
     * Constructs empty (default) parameters.
     */
    Parameters();

    /**
     * Constructs and parses the given parameters file.
     *
     * @param[in] parameters_file the parameters file
     * @param[in] comm the communicator
     */
    Parameters(const char *parameters_file, MPI_Comm comm);

    /**
     * Parses the given parameters file.
     *
     * @param[in] parameters_file the parameters file
     */
    void parse(const char *parameters_file, MPI_Comm comm);

    int AOL;            /**< AlignOverLongerSeq */
    int SIM;            /**< MatchSimilarity */
    int OS;             /**< OptimalScoreOverSelfScore */
    int exact_match_len;/**< exact match length cutoff */
    int window_size;    /**< slide window size */
    int open;           /**< open penalty for affine gap alignment */
    int gap;            /**< gap extension penalty for affine gap alignment */
};

}; /* namespace pgraph */

#endif /* _PGRAPH_PARAMETERS_H_ */