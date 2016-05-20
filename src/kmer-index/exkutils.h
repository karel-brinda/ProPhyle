#ifndef EXKUTILS_H
#define EXKUTILS_H

#include <stdint.h>

typedef struct {
	int mode; // bit 24-31 are the barcode length
	int n_threads;
	int trim_qual;

	int use_klcp;
	int kmer_length;
	int output_rids;

} exk_opt_t;

#endif //