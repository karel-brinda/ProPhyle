#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bwtexk.h"
#include "bwtaln.h"
#include "bwtgap.h"
#include "utils.h"
#include "bwa.h"
#include "bwase.h"
#include "kstring.h"
#include "klcp.h"
#include "bwautils.h"
#include "contig_node_translator.h"

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.0.1"
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef USE_MALLOC_WRAPPERS
#  include "malloc_wrap.h"
#endif

#define MAX_POSSIBLE_SA_POSITIONS 1000000

bwt_position_t positions[MAX_POSSIBLE_SA_POSITIONS];
int* seen_nodes;
int nodes_count;
int* prev_seen_nodes;
int prev_nodes_count;

exk_opt_t *exk_init_opt()
{
	exk_opt_t *o;
	o = (exk_opt_t*)calloc(1, sizeof(exk_opt_t));
	o->mode = BWA_MODE_GAPE | BWA_MODE_COMPREAD;
	o->n_threads = 1;
	o->trim_qual = 0;
	o->kmer_length = 14;
	o->use_klcp = 0;
	o->output = 1;
	o->output_old = 0;
	o->skip_positions_on_border = 1;
	o->need_log = 0;
	o->log_file_name = NULL;
	return o;
}

int bwt_cal_sa_coord(const bwt_t *bwt, int len, const ubyte_t *str, uint64_t* k, uint64_t* l, int start_pos)
{
	bwtint_t ok, ol;
	int i;
	*k = 0; *l = bwt->seq_len;

	//fprintf(stderr, "start k = %d, l = %d\n", *k, *l);
	for (i = start_pos; i < start_pos + len; ++i) {
		ubyte_t c = str[i];
		if (c > 3) {
			*k = 1;
			*l = 0;
			return i - start_pos;
		}
		if (c < 4) {
			bwt_2occ(bwt, *k - 1, *l, c, &ok, &ol);
			*k = bwt->L2[c] + ok + 1;
			*l = bwt->L2[c] + ol;
			//fprintf(stderr, "after i = %d character, cur k = %d, l = %d\n", i, *k, *l);
		}
		if (*k > *l) { // then restart
			return i - start_pos;
		}
	}
	return len;
}

int bwt_cal_sa_coord_continue(const bwt_t *bwt, int len, const ubyte_t *str,
															uint64_t* k, uint64_t* l,
															uint64_t* decreased_k, uint64_t* increased_l,
															int start_pos, klcp_t* klcp)
{
	bwtint_t ok, ol;
	int i;
	*k = decrease_k(klcp, *k);
	*decreased_k = *k;
	*l = increase_l(klcp, *l);
	*increased_l = *l;

	//fprintf(stderr, "increased k = %d, l = %d\n", *k, *l);

	//fprintf(stderr, "start k = %d, l = %d\n", *k, *l);
	for (i = start_pos; i < start_pos + len; ++i) {
		ubyte_t c = str[i];
		if (c > 3) { // then restart
			*k = 1;
			*l = 0;
			return i - start_pos;
		}
		if (c < 4) {
			bwt_2occ(bwt, *k - 1, *l, c, &ok, &ol);
			*k = bwt->L2[c] + ok + 1;
			*l = bwt->L2[c] + ol;
			//fprintf(stderr, "after i = %d character, cur k = %d, l = %d\n", i, *k, *l);
		}
		if (*k > *l) { // then restart
			return i - start_pos;
		}
	}
	return len;
}

size_t get_positions(const bwaidx_t* idx, const int query_length,
										 const uint64_t k, const uint64_t l) {
	uint64_t t;
	for(t = k; t <= l; ++t) {
		if (t - k >= MAX_POSSIBLE_SA_POSITIONS) {
			fprintf(stderr, "translation from SA-pos to seq-pos is truncated, too many (%llu) positions\n", l - k + 1);
			break;
		}
		int strand;
		uint64_t pos = bwa_sa2pos(idx->bns, idx->bwt, t, query_length, &strand);//bwt_sa(bwt, t);
		positions[t - k].position = pos;
		positions[t - k].strand = strand;
		positions[t - k].rid = -1;
		//fprintf(stdout, "%llu(%d) ", (*positions)[t - k], strand);
	}
	//fprintf(stdout, "\n");
	return (l - k + 1 < MAX_POSSIBLE_SA_POSITIONS ? l - k + 1 : MAX_POSSIBLE_SA_POSITIONS);
}

int position_on_border(const bwaidx_t* idx, bwt_position_t* position, int query_length) {
	//fprintf(stdout, "%llu %d %llu\n", position->position, query_length, idx->bns->l_pac);
	//fprintf(stdout, "%d %d\n", position->rid, idx->bns->n_seqs);
	return (position->position + query_length > idx->bns->l_pac
		|| (position->rid + 1 < idx->bns->n_seqs && position->position + query_length > idx->bns->anns[position->rid + 1].offset)
		|| position->position < idx->bns->anns[position->rid].offset);
}

void sort(int count, int** array) {
	int i;
	for(i = 1; i < count; ++i) {
		int x = (*array)[i];
		int j = i - 1;
		while (j >= 0 && (*array)[j] > x) {
			(*array)[j + 1] = (*array)[j];
			j--;
		}
		(*array)[j + 1] = x;
	}
}

size_t get_nodes_from_positions(const bwaidx_t* idx, const int query_length,
																const int positions_cnt,
																int8_t** seen_nodes_marks, int skip_positions_on_border) {
	size_t nodes_cnt = 0;
	int i;
	for(i = 0; i < positions_cnt; ++i) {
		uint64_t pos = positions[i].position;
		//fprintf(stdout, "%llu\n", pos);
		if (pos == (uint64_t)-1) {
			continue;
		}
		int rid = positions[i].rid;
		if (rid == -1 || position_on_border(idx, &(positions[i]), query_length)) {
			rid = bns_pos2rid(idx->bns, pos);
			positions[i].rid = rid;
		}
		int node = get_node_from_contig(rid);
		positions[i].node = node;
		int seen = (*seen_nodes_marks)[node];
		//fprintf(stdout, "position = %llu, rid = %d, offset[rid] = %llu, offset[rid + 1] = %llu\n",
		// 	pos, rid, idx->bns->anns[rid].offset, idx->bns->anns[rid + 1].offset);
		if (!seen && node != -1 && (!skip_positions_on_border || !position_on_border(idx, &(positions[i]), query_length))) {
			seen_nodes[nodes_cnt] = node;
			++nodes_cnt;
			(*seen_nodes_marks)[node] = 1;
			//fprintf(stderr, "t = %d, pos = %d, rid = %d\n", t, pos, rid);
		}
	}
	int r;
	for(r = 0; r < nodes_cnt; ++r) {
		(*seen_nodes_marks)[seen_nodes[r]] = 0;
	}
	sort(nodes_cnt, &seen_nodes);
	return nodes_cnt;
}

void output_old(int* seen_nodes, const int nodes_cnt) {
	fprintf(stdout, "%d ", nodes_cnt);
	int r;
	for(r = 0; r < nodes_cnt; ++r) {
		fprintf(stdout, "%s ", get_node_name(seen_nodes[r]));
	}
	fprintf(stdout, "\n");
}

void output(int* seen_nodes, const int nodes_cnt, int streak_length, int is_first_streak) {
	if (!is_first_streak){
		fprintf(stdout, " ");
	}
	if (nodes_cnt > 0) {
		int r;
		for(r = 0; r < nodes_cnt - 1; ++r) {
			fprintf(stdout, "%s,", get_node_name(seen_nodes[r]));
		}
		fprintf(stdout, "%s:", get_node_name(seen_nodes[nodes_cnt - 1]));
	} else {
		fprintf(stdout, "0:");
	}
	fprintf(stdout, "%d", streak_length);
}

void shift_positions_by_one(bwaidx_t* idx, int positions_cnt,
														const int query_length, const uint64_t k, const uint64_t l) {
	int i;
	for(i = 0; i < positions_cnt; ++i) {
		if (positions[i].position == (uint64_t)-1) {
			continue;
		}
		//if (position.position < bns->l_pac && bns->l_pac < position.position + ref_len)
		if (positions[i].strand == 0) {
			positions[i].position++;
		} else {
			// if (positions[i].position == 0) {
			// 	bwt_position_t new_position = get_position(idx, query_length, k + i);
			// 	position->position = new_position.position;
			// 	position->strand = new_position.strand;
			// 	position->rid = new_position.rid;
			// } else {
				positions[i].position--;
			//}
		}
	}
}

int equal(int a_cnt, int* a, int b_cnt, int* b) {
	if (a_cnt != b_cnt) {
		return 0;
	}
	int i;
	for(i = 0; i < a_cnt; ++i) {
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

void bwa_cal_sa(int tid, bwaidx_t* idx, int n_seqs, bwa_seq_t *seqs,
								const exk_opt_t *opt, klcp_t* klcp)
{
	bwase_initialize();
	seen_nodes = malloc(MAX_POSSIBLE_SA_POSITIONS * sizeof(int));
	prev_seen_nodes = malloc(MAX_POSSIBLE_SA_POSITIONS * sizeof(int));
	int i, j;
	bwt_t* bwt = idx->bwt;

	int8_t* seen_nodes_marks = malloc(idx->bns->n_seqs * sizeof(int8_t));
	uint64_t index;
	for(index = 0; index < idx->bns->n_seqs; ++index) {
		seen_nodes_marks[index] = 0;
	}
	int rids_computations = 0;
	int using_prev_rids = 0;
	for (i = 0; i != n_seqs; ++i) {
		if (i % 1000 == 0) {
			fprintf(stderr, "starting processing %d-th read in chunk\n", i);
		}
		bwa_seq_t *p = seqs + i;
		p->sa = 0; p->type = BWA_TYPE_NO_MATCH; p->c1 = p->c2 = 0; p->n_aln = 0; p->aln = 0;

		// NEED TO UNDERSTAND
		// core function
		// for (j = 0; j < p->len; ++j) // we need to complement
		// 	p->seq[j] = p->seq[j] > 3? 4 : 3 - p->seq[j];

		if (opt->output_old) {
			fprintf(stdout, "#");
			for(j = (int)p->len - 1; j>= 0; j--) {
				fprintf(stdout, "%c", "ACGTN"[p->seq[j]]);
			}
			fprintf(stdout, "\n");
		}
		if (opt->output) {
			fprintf(stdout, "U\t%s\t0\t%d\t", p->name, p->len);
		}
		uint64_t k = 0, l = 0, prev_k = 1, prev_l = 0;
		int current_streak_length = 0;
		nodes_count = 0;
		prev_nodes_count = 0;
		int start_pos = 0;
		size_t positions_cnt = 0;
		//int zero_streak = 0;
		//int was_one = 0;
		uint64_t decreased_k = 1;
		uint64_t increased_l = 0;
		int is_first_streak=1;
		while (start_pos <= p->len - opt->kmer_length) {
			if (start_pos == 0) {
				k = 0;
				l = 0;
				bwt_cal_sa_coord(bwt, opt->kmer_length, p->seq, &k, &l, start_pos);
			} else {
				if (opt->use_klcp && k <= l) {
					bwt_cal_sa_coord_continue(bwt, 1, p->seq, &k, &l, &decreased_k, &increased_l, start_pos + opt->kmer_length - 1, klcp);
				} else {
					k = 0;
					l = 0;
					bwt_cal_sa_coord(bwt, opt->kmer_length, p->seq, &k, &l, start_pos);
				}
			}
		  //fprintf(stderr, "start_pos = %d\n", start_pos);
			//fprintf(stderr, "found k = %llu, l = %llu\n", k, l);
			// fprintf(stderr, "prev k = %llu, prev l = %llu\n", prev_k, prev_l);
			int nodes_cnt = 0;
			if (k <= l) {
				if (prev_l - prev_k == l - k
						&& increased_l - decreased_k == l - k) {
					using_prev_rids++;
					shift_positions_by_one(idx, positions_cnt, opt->kmer_length, k, l);
				} else {
					rids_computations++;
					positions_cnt = get_positions(idx, opt->kmer_length,
						k, l);
				}
				nodes_cnt = get_nodes_from_positions(idx, opt->kmer_length,
					positions_cnt, &seen_nodes_marks, opt->skip_positions_on_border);
			}
			if (opt->output_old) {
				output_old(seen_nodes, nodes_cnt);
			} else if (opt->output) {
				if (start_pos == 0 || (equal(nodes_cnt, seen_nodes, prev_nodes_count, prev_seen_nodes))) {
					current_streak_length++;
				} else {
					output(prev_seen_nodes, prev_nodes_count, current_streak_length,is_first_streak);
					if(is_first_streak){
						is_first_streak=0;
					}
					current_streak_length = 1;
				}
			}
			int* tmp = seen_nodes;
			seen_nodes = prev_seen_nodes;
			prev_seen_nodes = tmp;
			prev_nodes_count = nodes_cnt;
			prev_k = k;
			prev_l = l;
			// if (opt->skip_after_fail) {
			// 	if (k <= l) {
			// 		was_one = 1;
			// 		zero_streak = 0;
			// 	} else {
			// 		if (was_one) {
			// 			if (zero_streak == 0) {
			// 				zero_streak += opt->kmer_length - 2;
			// 				if (opt->output_rids) {
			// 					int ind;
			// 					for(ind = 0; ind < opt->kmer_length - 2 && start_pos + ind < p->len - opt->kmer_length; ++ind) {
			// 						fprintf(stdout, "0 \n");
			// 					}
			// 				}
			// 				start_pos += opt->kmer_length - 2;
			// 			} else {
			// 				zero_streak++;
			// 			}
			// 		}
			// 	}
			// }
			start_pos++;
		}
		if (current_streak_length > 0) {
			output(prev_seen_nodes, prev_nodes_count, current_streak_length, is_first_streak);
		}
		if (opt->output) {
			fprintf(stdout, "\n");
		}
		free(p->name); free(p->seq); free(p->rseq); free(p->qual);
		p->name = 0; p->seq = p->rseq = p->qual = 0;
	}
	fprintf(stderr, "rids computed: %d\n", rids_computations);
	fprintf(stderr, "rids used previous: %d\n", using_prev_rids);
	free(seen_nodes_marks);
	free(seen_nodes);
	free(prev_seen_nodes);
}

void bwa_exk_core(const char *prefix, const char *fn_fa, const exk_opt_t *opt) {
	int n_seqs;
	bwa_seq_t *seqs;
	bwa_seqio_t *ks;
	bwaidx_t* idx;

	FILE* log_file;
	if (opt->need_log) {
		log_file = fopen(opt->log_file_name, "w");
	} else {
		log_file = stderr;
	}

	if ((idx = bwa_idx_load_partial(prefix, BWA_IDX_ALL, opt->need_log, log_file)) == 0) {
		fprintf(stderr, "Couldn't load idx from %s\n", prefix);
		return;
	}
	fprintf(stderr, "BWA loaded\n");
	bwa_destroy_unused_fields(idx);

	clock_t t = clock();
	klcp_t* klcp = malloc(sizeof(klcp_t));
	klcp->klcp = malloc(sizeof(bitarray_t));

	if (opt->use_klcp) {
		char* fn = malloc((strlen(prefix) + 10) * sizeof(char));
	  strcpy(fn, prefix);
		strcat(fn, ".");
	  char* kmer_length_str = malloc(5 * sizeof(char));
	  sprintf(kmer_length_str, "%d", opt->kmer_length);
	  strcat(fn, kmer_length_str);
	  strcat(fn, ".bit.klcp");
		klcp_restore(fn, klcp);
		free(fn);
		fprintf(log_file, "klcp_loading\t%.2fs\n", (float)(clock() - t) / CLOCKS_PER_SEC);
	}
	ks = bwa_open_reads_new(opt->mode, fn_fa);
	float total_time = 0;
	int64_t total_seqs = 0;
	t = clock();
	int seq_len = 0;
	while ((seqs = bwa_read_seq(ks, 0x40000, &n_seqs, opt->mode, opt->trim_qual)) != 0) {
		seq_len = seqs[0].len;
		bwa_cal_sa(0, idx, n_seqs, seqs, opt, klcp);
		total_seqs += n_seqs;
		bwa_free_read_seq(n_seqs, seqs);
	}
	total_time += (float)(clock() - t) / CLOCKS_PER_SEC;
	fprintf(stderr, "match time: %.2f sec\n", total_time);
	if (opt->need_log) {
		fprintf(log_file, "matching_time\t%.2fs\n", total_time);
		fprintf(log_file, "reads\t%" PRId64 "\n", total_seqs);
		fprintf(log_file, "kmers\t%" PRId64 "\n", total_seqs * (seq_len - opt->kmer_length + 1));
		fprintf(log_file, "rpm\t%" PRId64 "\n", (int64_t)(round(total_seqs * 60.0 / total_time)));
		fprintf(log_file, "kpm\t%" PRId64 "\n", (int64_t)(round(total_seqs * (seq_len - opt->kmer_length + 1) * 60.0 / total_time)));
	}
	//fprintf(stderr, "tot_seqs = %d\n", tot_seqs);
	//fprintf(stderr, "overall_increase = %llu\n", overall_increase);
	//fprintf(stderr, "increase per k-mer = %lf\n", 1.0 * overall_increase / (tot_seqs * (seq_len - opt->kmer_length + 1)));

	if (opt->need_log) {
		fclose(log_file);
	}
	// destroy
	if (opt->use_klcp) {
		destroy_klcp(klcp);
	} else {
		free(klcp->klcp);
		free(klcp);
	}
	bwa_idx_destroy_without_bns_name_and_anno(idx);
	bwa_seq_close(ks);
}

int exk_match(int argc, char *argv[])
{
	int c, opte = -1;
	exk_opt_t *opt;
	char *prefix;

	opt = exk_init_opt();
	while ((c = getopt(argc, argv, "l:psuvk:n:o:e:i:d:LR:t:NM:O:E:q:f:b012IYB:")) >= 0) {
		switch (c) {
		case 'v': { opt->output_old = 1; opt->output = 0; } break;
		case 'u': opt->use_klcp = 1; break;
		case 'k': opt->kmer_length = atoi(optarg); break;
		case 's': opt->skip_after_fail = 1; break;
		case 'p': opt->skip_positions_on_border = 0; break;
		case 'l': { opt->need_log = 1; opt->log_file_name = optarg; break; }
		case 'e': opte = atoi(optarg); break;
		case 't': opt->n_threads = atoi(optarg); break;
		case 'L': opt->mode |= BWA_MODE_LOGGAP; break;
		case 'q': opt->trim_qual = atoi(optarg); break;
		case 'N': opt->mode |= BWA_MODE_NONSTOP; break;
		case 'f': xreopen(optarg, "wb", stdout); break;
		case 'b': opt->mode |= BWA_MODE_BAM; break;
		case '0': opt->mode |= BWA_MODE_BAM_SE; break;
		case '1': opt->mode |= BWA_MODE_BAM_READ1; break;
		case '2': opt->mode |= BWA_MODE_BAM_READ2; break;
		case 'I': opt->mode |= BWA_MODE_IL13; break;
		case 'Y': opt->mode |= BWA_MODE_CFY; break;
		case 'B': opt->mode |= atoi(optarg) << 24; break;
		default: return 1;
		}
	}
	if (opte > 0) {
		opt->mode &= ~BWA_MODE_GAPE;
	}

	if (optind + 2 > argc) {
		fprintf(stderr, "\n");
		fprintf(stderr, "Usage:   exk match [options] <prefix> <in.fq>\n\n");
		fprintf(stderr, "Options: -k INT    length of k-mer\n");
		fprintf(stderr, "         -u        use klcp for matching\n");
		fprintf(stderr, "         -v        output set of chromosomes for every k-mer\n");
		fprintf(stderr, "         -s        skip k-1 k-mers after failing matching k-mer\n");
		fprintf(stderr, "         -p        do not check whether k-mer is on border of two contigs, and show such k-mers in output\n");
		fprintf(stderr, "         -l char*  log file name to output statistics\n");
		// fprintf(stderr, "         -t INT    number of threads [%d]\n", opt->n_threads);
		// fprintf(stderr, "         -B INT    length of barcode\n");
		// fprintf(stderr, "         -q INT    quality threshold for read trimming down to %dbp [%d]\n", BWA_MIN_RDLEN, opt->trim_qual);
    // fprintf(stderr, "         -f FILE   file to write output to instead of stdout\n");
		// fprintf(stderr, "         -B INT    length of barcode\n");
		// fprintf(stderr, "         -I        the input is in the Illumina 1.3+ FASTQ-like format\n");
		// fprintf(stderr, "         -b        the input read file is in the BAM format\n");
		// fprintf(stderr, "         -0        use single-end reads only (effective with -b)\n");
		// fprintf(stderr, "         -1        use the 1st read in a pair (effective with -b)\n");
		// fprintf(stderr, "         -2        use the 2nd read in a pair (effective with -b)\n");
		// fprintf(stderr, "         -Y        filter Casava-filtered sequences\n");
		fprintf(stderr, "\n");
		return 1;
	}
	if ((prefix = bwa_idx_infer_prefix(argv[optind])) == 0) {
		fprintf(stderr, "[%s] fail to locate the index %s\n", __func__, argv[optind]);
		free(opt);
		return 1;
	}
	bwa_exk_core(prefix, argv[optind+1], opt);
	free(opt); free(prefix);
	return 0;
}

int exk_index(int argc, char *argv[])
{
	int c, opte = -1;	exk_opt_t *opt;
	char *prefix;
	opt = exk_init_opt();
	while ((c = getopt(argc, argv, "k:n:o:e:i:d:l:k:LR:m:t:NM:O:E:q:f:b012IYB:")) >= 0) {
		switch (c) {
		case 'k': opt->kmer_length = atoi(optarg); break;
		case 'e': opte = atoi(optarg); break;
		case 't': opt->n_threads = atoi(optarg); break;
		case 'L': opt->mode |= BWA_MODE_LOGGAP; break;
		case 'q': opt->trim_qual = atoi(optarg); break;
		case 'N': opt->mode |= BWA_MODE_NONSTOP; break;
		case 'f': xreopen(optarg, "wb", stdout); break;
		case 'b': opt->mode |= BWA_MODE_BAM; break;
		case '0': opt->mode |= BWA_MODE_BAM_SE; break;
		case '1': opt->mode |= BWA_MODE_BAM_READ1; break;
		case '2': opt->mode |= BWA_MODE_BAM_READ2; break;
		case 'I': opt->mode |= BWA_MODE_IL13; break;
		case 'Y': opt->mode |= BWA_MODE_CFY; break;
		case 'B': opt->mode |= atoi(optarg) << 24; break;
		default: return 1;
		}
	}
	if (opte > 0) {
		opt->mode &= ~BWA_MODE_GAPE;
	}

	if (optind + 1 > argc) {
		fprintf(stderr, "\n");
		fprintf(stderr, "Usage:   exk index <prefix>\n\n");
		// fprintf(stderr, "Options: -t INT    number of threads [%d]\n", opt->n_threads);
		// fprintf(stderr, "         -q INT    quality threshold for read trimming down to %dbp [%d]\n", BWA_MIN_RDLEN, opt->trim_qual);
    // fprintf(stderr, "         -f FILE   file to write output to instead of stdout\n");

		fprintf(stderr, "Options:  -k INT    length of k-mer\n");

		// fprintf(stderr, "         -B INT    length of barcode\n");
		// fprintf(stderr, "         -I        the input is in the Illumina 1.3+ FASTQ-like format\n");
		// fprintf(stderr, "         -b        the input read file is in the BAM format\n");
		// fprintf(stderr, "         -0        use single-end reads only (effective with -b)\n");
		// fprintf(stderr, "         -1        use the 1st read in a pair (effective with -b)\n");
		// fprintf(stderr, "         -2        use the 2nd read in a pair (effective with -b)\n");
		// fprintf(stderr, "         -Y        filter Casava-filtered sequences\n");
		fprintf(stderr, "\n");
		return 1;
	}
	if ((prefix = bwa_idx_infer_prefix(argv[optind])) == 0) {
		fprintf(stderr, "[%s] fail to locate the index %s\n", __func__, argv[optind]);
		return 1;
	}
	exk_index_core(prefix, argv[optind+1], opt);
	free(prefix);
	return 0;
}

static int usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Program: exk (alignment of k-mers)\n");
	fprintf(stderr, "Usage:   exk command [options]\n\n");
	fprintf(stderr, "Command: index         construct klcp array\n");
	fprintf(stderr, "Command: match         align k-mers\n");
	fprintf(stderr, "\n");
	return 1;
}

int main(int argc, char *argv[])
{
	extern char *bwa_pg;
	int i, ret = 0;
	kstring_t pg = {0,0,0};
	ksprintf(&pg, "@PG\tID:bwa\tPN:bwa\tVN:%s\tCL:%s", PACKAGE_VERSION, argv[0]);
	for (i = 1; i < argc; ++i) ksprintf(&pg, " %s", argv[i]);
	bwa_pg = pg.s;
	if (argc < 2) return usage();
	if (strcmp(argv[1], "index") == 0) ret = exk_index(argc - 1, argv + 1);
	else if (strcmp(argv[1], "match") == 0) ret = exk_match(argc-1, argv+1);

	err_fflush(stdout);
	err_fclose(stdout);

	free(bwa_pg);
	return ret;
}
