#include <gatb/gatb_core.hpp>
#include <unordered_map>
#include <boost/unordered_set.hpp>
#include <unordered_set>
#include <vector>
#include "kseq.h"

#include <cstdio>

#include <gatb/tools/math/Integer.hpp>

const int32_t k=20;
const int32_t fasta_line_length=60;
const int32_t max_contig_length=10000000;

KSEQ_INIT(gzFile, gzread);


typedef uint64_t nkmer_t;
typedef std::set<nkmer_t> ordered_set_t;
typedef std::unordered_set<nkmer_t> unordered_set_t;

char complement(char x){
	switch(x){
		case 'A':
		case 'a':
			return 'T';
		case 'C':
		case 'c':
			return 'G';
		case 'G':
		case 'g':
			return 'C';
		case 'T':
		case 't':
			return 'A';
		default:
			return 'N';
	}
}

struct contig_t{
	int32_t k;

	char *seq_buffer;
	char *r_ext;
	char *l_ext;

	contig_t(uint32_t k){
		this->k=k;
		seq_buffer=new char[k+2*max_contig_length+1]();
	}

	int32_t reinit(const char *base_kmer){
		assert(strlen(base_kmer)==k);

		l_ext = r_ext = &seq_buffer[max_contig_length];
		*r_ext='\0';

		for(int32_t i=0;i<k;i++){
			r_extend(base_kmer[i]);
		}
		return 0;
	}

	int32_t r_extend(char c){
		*r_ext=c;
		++r_ext;
		*r_ext='\0';
		return 0;
	}

	int32_t l_extend(char c){
		--l_ext;
		*l_ext=complement(c);
		return 0;
	}

	~contig_t(){
		delete[] seq_buffer;
	}

	int32_t print_to_fasta(FILE* fasta_file, char* contig_name, char *comment=nullptr) const {
		if (comment==nullptr){
			fprintf(fasta_file,">%s\n",contig_name);
		} else {
			fprintf(fasta_file,">%s %s\n",contig_name,comment);
		}

		int32_t buffer_len=r_ext-l_ext;
		char print_buffer[fasta_line_length+1]={0};

		int32_t j=0;

		for (char *p=l_ext;p<r_ext;p+=fasta_line_length){
			strncpy(print_buffer,p,fasta_line_length);
			fprintf(fasta_file, "%s\n", print_buffer);
		}		

		return 0;
	}
};


/*
	TODO: test if kmer is correct
*/
template <typename T>
int kmers_from_fasta(const string &fasta_fn, T &set, int32_t k){
	set.clear();

 	gzFile fp;
    kseq_t *seq;
    int64_t l;

    fp = gzopen(fasta_fn.c_str(), "r");
    seq = kseq_init(fp);

    char buffer[100]={};

    Kmer<>::ModelCanonical model (k);


	for(int32_t seqid=0;(l = kseq_read(seq)) >= 0;seqid++) {
		cout << "start" << endl;
	    Data data (seq->seq.s);	
		cout << "data ok" << endl;
	    Kmer<>::ModelCanonical::Iterator it (model);
	    it.setData (data);

		cout << "starting iterator" << endl;
	    for (it.first(); !it.isDone(); it.next())
	    {
	    	set.insert(it->value().getVal());
	    	//std::cout << it->value() << " " << model.toString( it->value() ) << std::endl;
	    }
		cout << "iterator finished" << endl;
    }

    kseq_destroy(seq);
    gzclose(fp);

	return 0;
}

int ordered_to_unordered(const ordered_set_t &ordered_set, unordered_set_t &unordered_set){
	unordered_set.clear();

    for(nkmer_t const &element: ordered_set){
     	unordered_set.insert(element);
    }
	return 0;
}

template <typename T>
int assemble(const string &fasta_fn, T &set, int32_t k){
    Kmer<>::ModelCanonical model (k);


    cout << "assembling, size: " << set.size() << endl;

	FILE *file=fopen(fasta_fn.c_str(),"w+");

	char kmer_str[k+1];

	contig_t contig(k);

	const std::vector<char> nucls = {'A','C','G','T'};

	int i=0;
	while(set.size()>0){
		i++;
		//printf("writing contig %d\n",i);
		const nkmer_t central_kmer=*(set.begin());
		set.erase(central_kmer);

		std::string central_kmer_string=model.toString(central_kmer);
		contig.reinit(central_kmer_string.c_str());

		strncpy(kmer_str,central_kmer_string.c_str(),k);
		kmer_str[k]='\0';

		//printf("central k-mer: %s\n",central_kmer_string.c_str());

		bool extending = true;
		while (extending){
			
			for(int32_t i=0;i<k;i++){
				kmer_str[i]=kmer_str[i+1];
			}
			kmer_str[k]='\0';


			extending=false;
			for(const char &c : nucls){
				kmer_str[k-1]=c;


				nkmer_t nkmer = model.codeSeed (
 					kmer_str , Data::ASCII
				).value().getVal();


				if(set.count( nkmer )){
					contig.r_extend(c);
					extending=true;
					set.erase(nkmer);
					break;
				}

			}
		}

		contig.print_to_fasta(file,"conting xx");
	}

	fclose(file);

    cout << "...finished " << endl;

	return 0;

}



//using boost::unordered_set;

/********************************************************************************/
/*                              Kmer management                                 */
/*                                                                              */
/* This snippet shows how instantiate the Model class and how to get info       */
/* about it.                                                                    */
/*                                                                              */
/********************************************************************************/
int main (int argc, char* argv[])
{
	std::string fasta_in1="test1.fa";
	std::string fasta_in2="test2.fa";
	std::string fasta_out1="out1.fa";
	std::string fasta_out2="out2.fa";
	std::string fasta_out3="out3.fa";

	//const int32_t init_size=2000000;

	ordered_set_t os_in1;
	ordered_set_t os_in2;
	ordered_set_t os_out1;
	ordered_set_t os_out2;
	ordered_set_t os_out3;

	unordered_set_t us_in1;
	unordered_set_t us_in2;
	unordered_set_t us_out1;
	unordered_set_t us_out2;
	unordered_set_t us_out3;

	cout << "ordered in1" << endl;
	kmers_from_fasta(fasta_in1, os_in1, k);
	cout << "size " << os_in1.size() << endl;
	cout << "ordered in2" << endl;
	kmers_from_fasta(fasta_in2, os_in2, k);

	//cout << "unordered in1" << endl;
	//std::copy(us_in1.begin(), us_in1.end(), std::inserter(os_in1,os_in1.begin()));
	//cout << "unordered in2" << endl;
	//std::copy(us_in2.begin(), us_in2.end(), std::inserter(os_in2,os_in2.begin()));


	cout << "ordered out3" << endl;
	std::set_intersection(
		os_in1.begin(), os_in1.end(),
		os_in2.begin(), os_in2.end(),
    	std::inserter(os_out3, os_out3.end())
    );
	//cout << "unordered out3" << endl;
	//std::copy(os_out3.begin(), os_out3.end(), std::inserter(us_out3,us_out3.begin()));

	cout << "ordered out1" << endl;
	std::set_symmetric_difference(
		os_in1.begin(), os_in1.end(),
		os_out3.begin(), os_out3.end(),
    	std::inserter(os_out1, os_out1.end())
    );

	cout << "ordered out2" << endl;
	std::set_symmetric_difference(
		os_in2.begin(), os_in2.end(),
		os_out3.begin(), os_out3.end(),
    	std::inserter(os_out2, os_out2.end())
    );

	assemble("out.fa",os_in1,k);

	return 0;


    // We define a sequence of nucleotides
    //const char* seq = "CATTGATAGTGGATGGT";
    const char* seq = "TTTTTTTCCACFGTCCCCCCCCCC";
    std::cout << "Initial sequence: " << seq << std::endl;

    // We configure a data object with a sequence (in ASCII format)
    Data data ((char*)seq);

    // We declare a kmer model with kmer of size 5.
    // Note that we want "direct" kmers, not the min(forward,revcomp) default behavior.
    Kmer<>::ModelCanonical model (5);

    // We declare an iterator on a given sequence.
    Kmer<>::ModelCanonical::Iterator it (model);

	Kmer<>::ModelCanonical::Kmer kmer;
    //std::unordered_set< uint64_t > s;
    boost::unordered_set< uint64_t > s;

    // We configure the iterator with our sequence
    it.setData (data);

    // We iterate the kmers.
    for (it.first(); !it.isDone(); it.next())
    {
		kmer = model.codeSeed (
			 model.toString(it->value()).c_str() , Data::ASCII
		);

        //std::cout << "kmer " << model.toString(it->value()) << ",  value " << it->value() << std::endl;
        s.insert(kmer.value().getVal());
    }

     for ( auto itt = s.begin(); itt != s.end(); ++itt ){
     	kmer.set(*itt);
     	cout << *itt << " " << model.toString( kmer.value() )  << endl;
    }

    for(auto const &element: s){
     	cout << element  << endl;
    }




}