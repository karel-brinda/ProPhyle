#! /usr/bin/env python3

import os
import shutil
import datetime
import sys
import argparse

from tree_formatter import *

import logging


class TreeIndex:

	def __init__(self,tree_newick_fn,format=10):
		self.tree_newick_fn=tree_newick_fn
		self.tree=read_newick(tree_newick_fn,format=format)

	def process_node(self,node):
		if node.is_leaf():
			if hasattr(node,"fastapath"):
				fastas_fn=node.fastapath.split("@")
				for fasta_fn in fastas_fn:
					print(fasta_fn)

		else:
			children=node.get_children()

			for child in children:
				self.process_node(child)



if __name__ == "__main__":

	assert(len(sys.argv)==2)
	newick_fn=sys.argv[1]

	ti=TreeIndex(
			tree_newick_fn=newick_fn,
		)
	ti.process_node(ti.tree.get_tree_root())