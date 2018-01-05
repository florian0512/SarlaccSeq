#include "sarlacc.h"
    
const std::vector<char> BASES={'A', 'C', 'G', 'T'};
const int NBASES=BASES.size();

SEXP create_consensus_basic(SEXP alignments, SEXP min_cov, SEXP pseudo_count) {
    BEGIN_RCPP
    
    // Checking inputs.
    auto aln=hold_XStringSet(alignments); 
    const size_t naligns=get_length_from_XStringSet_holder(&aln);
    const int alignwidth=check_alignment_width(&aln);

    const double mincov=check_numeric_scalar(min_cov, "minimum coverage");
    const double pseudo_denom=check_numeric_scalar(pseudo_count, "pseudo count");
    const double pseudo_num=pseudo_denom/NBASES;
    
    std::vector<int> scores(alignwidth*NBASES), incidences(alignwidth);

    // Counting the number of occurrences of each base at each position.
    for (size_t a=0; a<naligns; ++a) {
        auto curalign=get_elt_from_XStringSet_holder(&aln, a);
        const char* aln_str=curalign.ptr;
        auto sIt=scores.begin();

        for (size_t i=0; i<alignwidth; ++i) {
            const char curbase=DNAdecode(aln_str[i]);
            switch (curbase) { 
                case 'A': case 'a':
                    ++(*sIt);
                    break;
                case 'C': case 'c':
                    ++(*(sIt+1));
                    break;
                case 'G': case 'g':
                    ++(*(sIt+2));
                    break;
                case 'T': case 't':
                    ++(*(sIt+3));
                    break;
            }
            
            // We use a separate vector for holding incidences, just in case
            // there are "N"'s (such that the sum of ACTG counts != incidences).
            if (curbase!='-') {
                ++incidences[i];
            }
            sIt+=NBASES;
        }
    }

    // Constructing the consensus sequence.
    std::vector<char> consensus(alignwidth+1, '\0');
    std::vector<double> qualities(alignwidth);
    auto sIt=scores.begin();
    auto cIt=consensus.begin();
    auto qIt=qualities.begin();

    for (size_t i=0; i<alignwidth; ++i, sIt+=NBASES) {
        if (incidences[i] < double(naligns)*mincov) {
             continue;
        }

        // Picking the most abundant base.
        auto maxed=std::max_element(sIt, sIt+NBASES);
        (*cIt)=BASES[maxed - sIt];
        ++cIt;

        // Denominator computed from the non-N bases only; N's neither help nor hinder, as the data is missing.
        const double total=std::accumulate(sIt, sIt+NBASES, 0);
        (*qIt)=(*maxed + pseudo_num)/(total + pseudo_denom);
        (*qIt)=1-(*qIt); // the ERROR probability, remember.
        ++qIt;
    }

    return Rcpp::List::create(Rcpp::String(consensus.data()), Rcpp::NumericVector(qualities.begin(), qIt));
    END_RCPP
}

/* This creates a consensus where the choice of base is aware of the different 
 * qualities across different reads. Similarly, the output quality score
 * calculation will take that into account.
 */

SEXP create_consensus_quality(SEXP alignments, SEXP qualities, SEXP min_cov) {
    BEGIN_RCPP    

    // Checking inputs. We need the numeric qualities as they are decoded 
    // differently depending on whether they are Solexa or Phred.
    auto aln=hold_XStringSet(alignments); 
    const size_t naligns=get_length_from_XStringSet_holder(&aln);
    const int alignwidth=check_alignment_width(&aln);

    Rcpp::List qual(qualities);
    const size_t nquals=qual.size();
    if (nquals!=naligns) {
        throw std::runtime_error("alignments and qualities have different numbers of entries");
    }

    const double mincov=check_numeric_scalar(min_cov, "minimum coverage");
    std::vector<int> incidences(alignwidth);
    std::vector<double> scores(alignwidth*NBASES);

    // Running through each entry.
    for (size_t a=0; a<naligns; ++a) {
        auto curalign=get_elt_from_XStringSet_holder(&aln, a);
        const char* astr=curalign.ptr;

        Rcpp::IntegerVector curqual(qual[a]);
        auto sIt=scores.begin();
        int position=0;

        for (size_t i=0; i<alignwidth; ++i, sIt+=NBASES) { // leave sIt here to ensure it runs even when 'continue's.
            const char curbase=DNAdecode(astr[i]);
            if (curbase=='-') {
                continue;
            }
            ++incidences[i];

            const double newqual=curqual[position];
            const double right=std::log1p(-newqual); // using base e to make life easier.
            const double wrong=std::log(newqual/(NBASES-1));
            ++position;

            // Adding the log-probabilities for each base. This is safe with 'N's, as all bases get +=wrong.
            for (size_t b=0; b<NBASES; ++b) {
                *(sIt+b) += (curbase==BASES[b] ? right : wrong);
            }
        }
    }

    // Constructing the consensus sequence.
    std::vector<char> consensus(alignwidth+1, '\0');
    std::vector<double> qualities(alignwidth);
    auto sIt=scores.begin();
    auto cIt=consensus.begin();
    auto qIt=qualities.begin();

    for (size_t i=0; i<alignwidth; ++i, sIt+=NBASES) { // leave sIt here to ensure it runs even when 'continue's are triggered.
        if (incidences[i] < double(naligns)*mincov) {
             continue;
        }

        // Choosing the base with the highest probability.
        auto maxed=std::max_element(sIt, sIt+NBASES);
        (*cIt)=BASES[maxed - sIt];
        const double numerator=*maxed;
        ++cIt;
        
        // Summing probabilities to get the denominator of the probabilities.
        std::sort(sIt, sIt+NBASES);
        double denom=*sIt;
        for (size_t b=1; b<NBASES; ++b) {
            const double leftover=*(sIt+b) - denom;
            denom+=R::log1pexp(leftover);
        }
        
        (*qIt)=-std::expm1(numerator - denom); // -expm1() for the ERROR probability.
        ++qIt;
    }

    return Rcpp::List::create(Rcpp::String(consensus.data()), Rcpp::NumericVector(qualities.begin(), qIt));
    END_RCPP
}
