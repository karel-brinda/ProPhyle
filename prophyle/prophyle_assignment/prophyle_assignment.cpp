#include "tree_index.h"
#include "read_processor.h"
#include "word_splitter.h"
#include <iostream>
#include <fstream>
#include <getopt.h>

struct Arguments {
  size_t k;
  std::string input_file = "-";
  std::string newick_file;
  AssignmentOutputFormat format = AssignmentOutputFormat::Kraken;
  Measure criteria = Measure::C1;
  bool simulate_lca = false;
  bool annotate = false;
  bool tie_lca = false;
  bool not_translate_blocks = false;
};

void print_usage() {

}

bool is_number(const std::string& s)
{
  return !s.empty() && std::find_if(s.begin(),
      s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

Arguments parse_arguments(int argc, char *argv[]) {
  int32_t c;
  int index;
  Arguments arguments;
  while ((c = getopt(argc, argv, "f:m:XALD")) >= 0) {
    switch (c) {
      case 'f': {
        std::string format_str(optarg);
        if (format_str == "sam") {
          arguments.format = AssignmentOutputFormat::Sam;
        } else if (format_str == "kraken") {
          arguments.format = AssignmentOutputFormat::Kraken;
        } else {
          std::cerr << "assignment format should be sam or kraken" << std::endl;
          exit(1);
        }
        break;
      }
      case 'm': {
        std::string measure_str(optarg);
        if (measure_str == "c1") {
          arguments.criteria = Measure::C1;
        } else if (measure_str == "h1") {
          arguments.criteria = Measure::H1;
        } else {
          std::cerr << "measure should be c1 or h1" << std::endl;
          exit(1);
        }
        break;
      }
      case 'X': arguments.simulate_lca = true; break;
      case 'A': arguments.annotate = true; break;
      case 'L': arguments.tie_lca = true; break;
      case 'D': arguments.not_translate_blocks = true; break;
      default: {
        std::cerr << "argument " << c << " is not supported" << std::endl;
        exit(1);
      };
    }
  }
  if (optind + 3 > argc) {
    print_usage();
    exit(1);
  }
  arguments.newick_file = argv[optind];
  if (!is_number(std::string(argv[optind + 1]))) {
    std::cerr << "argument k should be number, but k = " << argv[optind + 1] << std::endl;
    print_usage();
    exit(1);
  }
  int32_t k = std::stoi(argv[optind + 1]);
  if (k <= 0) {
    std::cerr << "k should be > 0" << std::endl;
    exit(1);
  }
  arguments.k = static_cast<size_t>(k);
  arguments.input_file = argv[optind + 2];
  return arguments;
}

int main(int argc, char *argv[]) {
  Arguments arguments = parse_arguments(argc, argv);
  TreeIndex tree = TreeIndex(arguments.newick_file);
  ReadProcessor read_processor = ReadProcessor(tree, arguments.k, arguments.simulate_lca, arguments.annotate,
      arguments.tie_lca, arguments.not_translate_blocks);

  if (arguments.input_file != "-") {
    freopen(arguments.input_file.c_str(), "r", stdin);
  }

  if (arguments.format == AssignmentOutputFormat::Sam) {
    read_processor.print_sam_header(std::cout);
  }
  std::string line;
  while (getline(std::cin, line)) {
    read_processor.process_krakline(line, arguments.format, arguments.criteria);
  }

//  std::string krakline = "U\tread1\t0\t60\tn001:21 n001,n154:1 n001:8 n001,n410:1 n001:12 n001,n151:1 n001:1";
//  read_processor.process_krakline(krakline, arguments.format, arguments.criteria);
//  krakline = "U\tread2\t0\t37\tn001:3 n001,n023:2 n001:1 n001,n154,n176:1 n001,n176:2 n001,n154,n176:4 n001,n176:2 n001,n154,n176:4 n001,n176:1 n001:2";
//  read_processor.process_krakline(krakline, arguments.format, arguments.criteria);
//  krakline = "U\tread3\t0\t60\tn002:20 0:11 n006:7 n006,n134:1 n006:6";
//  read_processor.process_krakline(krakline, arguments.format, arguments.criteria);
//  krakline = "U\tread4\t0\t60\tn175,n199,n223,n271,n272,n287,n293,n307,n323,n329,n332,n348,n354,n355,n358:45";
//  read_processor.process_krakline(krakline, arguments.format, arguments.criteria);
//  krakline = "U\tread5\t0\t60\t0:45U";
//  read_processor.process_krakline(krakline, arguments.format, arguments.criteria);

  return 0;
}
