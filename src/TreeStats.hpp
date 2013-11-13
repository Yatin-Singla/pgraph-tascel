/**
 * @file TreeStats.hpp
 *
 * @author jeff.daily@pnnl.gov
 *
 * Copyright 2012 Pacific Northwest National Laboratory. All rights reserved.
 */
#ifndef TREESTATS_H_
#define TREESTATS_H_

#include <iomanip>
#include <iostream>
#include <string>

using std::fixed;
using std::ostream;
using std::setw;
using std::showpoint;
using std::string;

#define MIN(x, y) (((x)<(y))? (x) : (y))
#define MAX(x, y) (((x)>(y))? (x) : (y))

class TreeStats
{
    public:
        double trees;
        double suffixes;
        double times;
        double size_internal;
        double fanout;
        double avgdepth;
        double deepest;
        double suffix_avg_length;
        double suffix_max_length;
        double time_build;
        double time_process;

        TreeStats()
            : trees(0.0)
            , suffixes(0.0)
            , times(0.0)
            , size_internal(0.0)
            , fanout(0.0)
            , avgdepth(0.0)
            , deepest(0.0)
            , suffix_avg_length(0.0)
            , suffix_max_length(0.0)
            , time_build(0.0)
            , time_process(0.0)
        { }

        TreeStats &operator >(const TreeStats &other) {
            trees = MAX(trees, other.trees);
            suffixes = MAX(suffixes, other.suffixes);
            times = MAX(times, other.times);
            size_internal = MAX(size_internal, other.size_internal);
            fanout = MAX(fanout, other.fanout);
            avgdepth = MAX(avgdepth, other.avgdepth);
            deepest = MAX(deepest, other.deepest);
            suffix_avg_length = MAX(suffix_avg_length, other.suffix_avg_length);
            suffix_max_length = MAX(suffix_max_length, other.suffix_max_length);
            time_build = MAX(time_build, other.time_build);
            time_process = MAX(time_process, other.time_process);
            return *this;
        }

        TreeStats &operator <(const TreeStats &other) {
            trees = MIN(trees, other.trees);
            suffixes = MIN(suffixes, other.suffixes);
            times = MIN(times, other.times);
            size_internal = MIN(size_internal, other.size_internal);
            fanout = MIN(fanout, other.fanout);
            avgdepth = MIN(avgdepth, other.avgdepth);
            deepest = MIN(deepest, other.deepest);
            suffix_avg_length = MIN(suffix_avg_length, other.suffix_avg_length);
            suffix_max_length = MIN(suffix_max_length, other.suffix_max_length);
            time_build = MIN(time_build, other.time_build);
            time_process = MIN(time_process, other.time_process);
            return *this;
        }

        TreeStats operator /(const int &other) const {
            TreeStats result;
            assert(other != 0);
            result.trees = this->trees / other;
            result.suffixes = this->suffixes / other;
            result.times = this->times / other;
            result.size_internal = this->size_internal / other;
            result.fanout = this->fanout / other;
            result.avgdepth = this->avgdepth / other;
            result.deepest = this->deepest / other;
            result.suffix_avg_length = this->suffix_avg_length / other;
            result.suffix_max_length = this->suffix_max_length / other;
            result.time_build = this->time_build / other;
            result.time_process = this->time_process / other;
            return result;
        }

        TreeStats stddev(const int &n, const TreeStats *trials) const {
            TreeStats avg = *this / n;
            TreeStats result;
            assert(n != 0);
            for (int i=0; i<n; ++i) {
                result.trees += pow(trials[i].trees - avg.trees, 2.0);
                result.suffixes += pow(trials[i].suffixes - avg.suffixes, 2.0);
                result.times += pow(trials[i].times - avg.times, 2.0);
                result.size_internal += pow(trials[i].size_internal - avg.size_internal, 2.0);
                result.fanout += pow(trials[i].fanout - avg.fanout, 2.0);
                result.avgdepth += pow(trials[i].avgdepth - avg.avgdepth, 2.0);
                result.deepest += pow(trials[i].deepest - avg.deepest, 2.0);
                result.suffix_avg_length += pow(trials[i].suffix_avg_length - avg.suffix_avg_length, 2.0);
                result.suffix_max_length += pow(trials[i].suffix_max_length - avg.suffix_max_length, 2.0);
                result.time_build += pow(trials[i].time_build - avg.time_build, 2.0);
                result.time_process += pow(trials[i].time_process - avg.time_process, 2.0);
            }
            result = result / n;
            result.trees = pow(result.trees, 0.5);
            result.suffixes = pow(result.suffixes, 0.5);
            result.times = pow(result.times, 0.5);
            result.size_internal = pow(result.size_internal, 0.5);
            result.fanout = pow(result.fanout, 0.5);
            result.avgdepth = pow(result.avgdepth, 0.5);
            result.deepest = pow(result.deepest, 0.5);
            result.suffix_avg_length = pow(result.suffix_avg_length, 0.5);
            result.suffix_max_length = pow(result.suffix_max_length, 0.5);
            result.time_build = pow(result.time_build, 0.5);
            result.time_process = pow(result.time_process, 0.5);
            return result;
        }

        TreeStats &operator +=(const TreeStats &other) {
            trees += other.trees;
            suffixes += other.suffixes;
            times += other.times;
            size_internal += other.size_internal;
            fanout += other.fanout;
            avgdepth += other.avgdepth;
            deepest += other.deepest;
            suffix_avg_length += other.suffix_avg_length;
            suffix_max_length += other.suffix_max_length;
            time_build += other.time_build;
            time_process += other.time_process;
            return *this;
        }

        string getHeader() const {
            return 
                "         Trees"
                "      Suffixes"
                " InternalNodes"
                "        Fanout"
                "     Avg_Depth"
                "       Deepest"
                "  SuffixAvgLen"
                "  SuffixMaxLen"
                "         Times"
                "    Time_Build"
                "  Time_Process"
                ;
        }

        friend ostream &operator << (ostream &os, const TreeStats &stats) {
            int p = os.precision();
            os.precision(4);
            os << setw(14) << fixed << showpoint << stats.trees
               << setw(14) << fixed << showpoint << stats.suffixes
               << setw(14) << fixed << showpoint << stats.size_internal
               << setw(14) << fixed << showpoint << stats.fanout
               << setw(14) << fixed << showpoint << stats.avgdepth
               << setw(14) << fixed << showpoint << stats.deepest
               << setw(14) << fixed << showpoint << stats.suffix_avg_length
               << setw(14) << fixed << showpoint << stats.suffix_max_length
               << setw(14) << fixed << showpoint << stats.times
               << setw(14) << fixed << showpoint << stats.time_build
               << setw(14) << fixed << showpoint << stats.time_process;
            os.precision(p); // undo state change
            return os;
        }
};

#undef MIN
#undef MAX

#endif /* TREESTATS_H_ */