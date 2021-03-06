#include "sarlacc.h"

#include "reference_align.h"
#include "utils.h"
#include "DNA_input.h"

#include <vector>
#include <stdexcept>
#include <deque>

SEXP adaptor_align(SEXP readseq, SEXP readqual, SEXP encoding, SEXP gapopen, SEXP gapext, SEXP adaptor, SEXP sec_starts, SEXP sec_ends) {
    BEGIN_RCPP

    auto adaptor_seq=check_string(adaptor, "adaptor sequence");
    reference_align RA(adaptor_seq.size(), adaptor_seq.c_str(), encoding,
            check_numeric_scalar(gapopen, "gap opening penalty"), 
            check_numeric_scalar(gapext, "gap extension penalty")
    );

    auto sholder=process_DNA_input(readseq);
    auto qholder=hold_XStringSet(readqual);
    const size_t nseq=sholder->size();
    if (nseq!=qholder.length) {
        throw std::runtime_error("sequence and quality vectors should have the same length");
    }

    Rcpp::IntegerVector Sec_starts(sec_starts), Sec_ends(sec_ends);
    const size_t nsections=Sec_starts.size();
    if (nsections!=Sec_ends.size()) {
        throw std::runtime_error("section starts and ends should have the same length");        
    }

    // Setting up the output.
    Rcpp::NumericVector scores(nseq);
    Rcpp::IntegerVector aln_starts(nseq), aln_ends(nseq);

    std::vector<Rcpp::IntegerVector> collected_starts(nsections), collected_widths(nsections);
    for (size_t i=0; i<nsections; ++i) { 
        collected_starts[i]=Rcpp::IntegerVector(nseq); // ensure each vector points to separate memory.
        collected_widths[i]=Rcpp::IntegerVector(nseq); 
    }

    reference_align::querymap qmap;

    for (size_t i=0; i<nseq; ++i) {
        auto curpair=sholder->get(i);
        const char* sstr=curpair.first;
        const size_t slen=curpair.second;
        
        auto curqual=get_elt_from_XStringSet_holder(&qholder, i);
        if (slen!=curqual.length) {
            throw std::runtime_error("sequence and quality strings should have the same length");
        }

        scores[i]=RA.align(slen, sstr, curqual.ptr);
        RA.fill_map(qmap);

        auto aln_pos=qmap(0, adaptor_seq.size());
        if (aln_pos.first < aln_pos.second) { // protect against empty sequences for which start/end are zero-indexed equal.
            aln_starts[i]=aln_pos.first+1; // 1-based indexing.
            aln_ends[i]=aln_pos.second;
        }

        for (size_t sec=0; sec<nsections; ++sec) {
            auto current=qmap(Sec_starts[sec], Sec_ends[sec], true);
            collected_starts[sec][i]=current.first + 1; // 1-based indexing.
            collected_widths[sec][i]=current.second - current.first;
        }
    }

    return Rcpp::List::create(scores, aln_starts, aln_ends,
        Rcpp::List(collected_starts.begin(), collected_starts.end()), 
        Rcpp::List(collected_widths.begin(), collected_widths.end())
    );

    END_RCPP
}

SEXP adaptor_align_score_only(SEXP readseq, SEXP readqual, SEXP encoding, SEXP gapopen, SEXP gapext, SEXP adaptor) {
    BEGIN_RCPP
    auto adaptor_seq=check_string(adaptor, "adaptor sequence");
    reference_align RA(adaptor_seq.size(), adaptor_seq.c_str(), encoding,
            check_numeric_scalar(gapopen, "gap opening penalty"), 
            check_numeric_scalar(gapext, "gap extension penalty")
    );

    auto sholder=process_DNA_input(readseq);
    auto qholder=hold_XStringSet(readqual);
    const size_t nseq=sholder->size();
    if (nseq!=qholder.length) {
        throw std::runtime_error("sequence and quality vectors should have the same length");
    }

    // Setting up the output.
    Rcpp::NumericVector scores(nseq);
    for (size_t i=0; i<nseq; ++i) {
        auto curpair=sholder->get(i);
        const char* sstr=curpair.first;
        const size_t slen=curpair.second;

        auto curqual=get_elt_from_XStringSet_holder(&qholder, i);
        if (slen!=curqual.length) {
            throw std::runtime_error("sequence and quality strings should have the same length");
        }
        scores[i]=RA.align(slen, sstr, curqual.ptr);
    }

    return scores;
    END_RCPP
}
