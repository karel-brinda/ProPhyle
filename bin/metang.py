#! /usr/bin/env python3

import argparse
import datetime
import os
import shutil
import subprocess
import sys

#bin_dir=os.path.normpath(os.path.join(os.path.dirname(__file__),"../../bin"))
bin_dir=os.path.dirname(__file__)
bwa=os.path.join(bin_dir,"bwa")
exk=os.path.join(bin_dir,"exk")
asm=os.path.abspath(os.path.join(bin_dir,"assembler"))
newick2makefile=os.path.join(bin_dir,"newick2makefile.py")
test_newick=os.path.join(bin_dir,"test_newick_tree.py")
merge_fastas=os.path.join(bin_dir,"create_final_fasta.py")
assign=os.path.join(bin_dir,"assignment.py")


def _test_files(*fns):
	#print(fns)
	for fn in fns:
		assert os.path.isfile(fn), 'File "{}" does not exist'.format(fn)

def _run_safe(command, output_fn=None):
	command_str=" ".join(map(lambda x: str(x),command))
	print("Running:", command_str, file=sys.stderr)
	if output_fn is None:
		out_fo=sys.stdout
	else:
		out_fo=open(output_fn,"w+")
	error_code=subprocess.call("/bin/bash -x -o pipefail -c '{}'".format(command_str), shell=True, stdout=out_fo)
	if error_code==0:
		print("Finished:", command_str, file=sys.stderr)
	else:
		print("Finished with error (error code {}):".format(error_code), command_str, file=sys.stderr)

def _message(msg):
	dt=datetime.datetime.now()
	fdt=dt.strftime("%Y-%m-%d %H:%M:%S")
	print('[metang]', fdt, msg, file=sys.stderr)

###############
# METANG INIT #
###############

def init():
	pass


################
# METANG INDEX #
################

def _create_makefile(index_dir, k, library_dir):
	_message('Creating Makefile for k-mer propagation')
	propagation_dir=os.path.join(index_dir, 'propagation')
	os.makedirs(propagation_dir)

	makefile=os.path.join(propagation_dir,'Makefile')
	newick_fn=os.path.join(index_dir,'tree.newick')
	#_test_files(newick2makefile, newick_fn)
	command=[newick2makefile, '-n', newick_fn, '-k', k, '-o', './', '-l', os.path.abspath(library_dir)]
	_run_safe(command,makefile)

def _propagate(index_dir,threads):
	_message('Running k-mer propagation')
	propagation_dir=os.path.join(index_dir, 'propagation')
	_test_files(os.path.join(propagation_dir, 'Makefile'))
	command=['make', '-j', threads, '-C', propagation_dir, 'V=1', "ASSEMBLER={}".format(asm)]
	_run_safe(command)

def _merge_fastas(index_dir):
	_message('Generating index.fa')
	propagation_dir=os.path.join(index_dir, 'propagation')
	# todo: check files for all nodes exist and are of size > 0
	index_fa=os.path.join(index_dir,"index.fa")
	_test_files(merge_fastas)
	command=[merge_fastas, propagation_dir]
	_run_safe(command, index_fa)	

def _fa2pac(fa_fn):
	_message('Generating packed FASTA file')
	_test_files(bwa, fa_fn)
	command=[bwa, 'fa2pac', fa_fn, fa_fn]
	_run_safe(command)

def _pac2bwt(fa_fn):
	_message('Generating BWT')
	_test_files(bwa, fa_fn+".pac")
	command=[bwa, 'pac2bwtgen', fa_fn+".pac", fa_fn+".bwt"]
	_run_safe(command)

def _bwt2bwtocc(fa_fn):
	_message('Generating sampled OCC array')
	_test_files(bwa, fa_fn+".bwt")
	command=[bwa, 'bwtupdate', fa_fn+".bwt"]
	_run_safe(command)

def _bwtocc2sa(fa_fn):
	_message('Generating sampled SA')
	_test_files(bwa, fa_fn+".bwt")
	command=[bwa, 'bwt2sa', fa_fn+".bwt", fa_fn+".sa"]
	_run_safe(command)

def _bwtocc2klcp(fa_fn,k):
	_message('Generating k-LCP array')
	_test_files(exk, fa_fn+".bwt")
	command=[exk, 'index', '-k', k, fa_fn]
	_run_safe(command)

def index(index_dir, threads, k, newick_fn, library_dir, cont=False, klcp=True):
	assert k>1

	# check files & dirs
	_test_files(newick_fn)
	index_fa=os.path.join(index_dir,'index.fa')
	index_newick=os.path.join(index_dir,'tree.newick')
	makefile_dir=os.path.join(index_dir,'propagation')
	makefile=os.path.join(index_dir,'propagation','Makefile')

	assert not os.path.isfile(index_dir) 
	assert not os.path.isdir(index_dir)

	# make index dir
	os.makedirs(index_dir)

	# copy newick
	shutil.copy(newick_fn, index_newick)

	# create makefile
	_create_makefile(index_dir, k, library_dir)

	# run makefile
	_propagate(index_dir, threads=threads)

	# merge fastas
	_merge_fastas(index_dir)

	# bwa index & klcp
	_fa2pac(index_fa)
	_pac2bwt(index_fa)
	_bwt2bwtocc(index_fa)
	_bwtocc2sa(index_fa)
	if klcp:
		_bwtocc2klcp(index_fa,k)


###################
# METANG CLASSIFY #
###################

def classify(index_dir,fq_fn,k,use_klcp,out_format='sam'):
	index_fa=os.path.join(index_dir, 'index.fa')
	index_newick=os.path.join(index_dir, 'tree.newick')

	_test_files(fq_fn,index_fa,index_newick,exk,assign)

	_test_files(
			index_fa+'.bwt',
			index_fa+'.pac',
			index_fa+'.sa',
			index_fa+'.ann',
			index_fa+'.amb',
		)

	if use_klcp:
		_test_files("{}.{}.bit.klcp".format(index_fa,k))

	# todo: add integrity checking (correct file size: |sa|=|pac|, |bwt|=2|sa|)

	command=[exk, 'match', '-k', k, '-u' if use_klcp else '', index_fa, fq_fn] \
		+ \
		['|'] \
		+ \
		[assign, '-i', '-', '-k', k, '-n', index_newick, '-f', out_format]
	_run_safe(command)

if __name__ == "__main__":
	parser = argparse.ArgumentParser()

	subparsers = parser.add_subparsers(help='sub-command help',dest='subcommand')

	parser_init = subparsers.add_parser('init', help='Initialize data')
	parser_init.add_argument('--bar', type=int, help='bar help', required=False)

	parser_index = subparsers.add_parser('index', help='Create index')
	parser_index.add_argument(
			'-n',
			metavar='FILE',
			dest='newick',
			type=str,
			help='newick tree',
			required=True,
		)
	parser_index.add_argument(
			'-i',
			metavar='DIR',
			dest='index_dir',
			type=str,
			help='directory with index',
			required=True,
		)
	parser_index.add_argument(
			'-l',
			metavar='DIR',
			dest='library_dir',
			type=str,
			help='directory with sequence library',
			required=True,
		)
	parser_index.add_argument(
			'-t',
			metavar='INT',
			dest='threads',
			type=int,
			help='number of threads',
			required=True,
		)
	parser_index.add_argument(
			'-k',
			metavar='INT',
			dest='k',
			type=int,
			help='k-mer length',
			required=True,
		)

	parser_classify = subparsers.add_parser('classify', help='Classify reads')
	parser_classify.add_argument(
			'-i',
			metavar='DIR',
			dest='index_dir',
			type=str,
			help='directory with index',
			required=True,
		)
	parser_classify.add_argument(
			'-f',
			metavar='FILE',
			dest='reads',
			type=str,
			help='file with reads in FASTA or FASTQ [- for standard input]',
			required=True,
		)
	parser_classify.add_argument(
			'-k',
			dest='k',
			metavar='INT',
			type=int,
			help='K-mer length',
			required=True,
		)
	parser_classify.add_argument(
			'--no-klcp','-n',
			dest='klcp',
			action='store_false',
			help='Do not use k-LCP',
		)

	args = parser.parse_args()
	subcommand=args.subcommand

	if subcommand=="init":
		init()

	elif subcommand=="index":		
		index(
				index_dir=args.index_dir,
				threads=args.threads,
				k=args.k,
				newick_fn=args.newick,
				library_dir=args.library_dir,
			)

	elif subcommand=="classify":
		classify(
				index_dir=args.index_dir,
				fq_fn=args.reads,
				k=args.k,
				use_klcp=args.klcp,
			)

	else:
		parser.print_help()
		sys.exit(1)
