#include <string>
#include <getopt.h>
#include <iostream>

#include "SnowTools/gzstream.h"
#include "SnowTools/SnowUtils.h"
#include "SnowTools/GenomicRegionCollection.h"
#include "SnowTools/GenomicRegion.h"
#include "SnowTools/SnowToolsCommon.h"

#include "VariantBamWalker.h"

using SnowTools::GenomicRegion;
using SnowTools::GenomicRegionCollection;
using SnowTools::GRC;

static const char *VARIANT_BAM_USAGE_MESSAGE =
"Usage: variant <input.bam> [OPTIONS] \n\n"
"  Description: Filter a BAM/CRAM file according to hierarchical rules\n"
"\n"
" General options\n"
"      --help                           Display this help and exit\n"
"  -v, --verbose                        Verbose output\n"
"  -c, --counts-file                    File to place read counts per rule / region\n"
"  -x, --no-output                      Don't output reads (used for profiling with -q and/or counting with -c)\n"
" Output options\n"
"  -o, --output-bam                     Output BAM file to write instead of SAM-format stdout\n"
"  -C, --cram                           Output file should be in CRAM format\n"
"  -T, --reference                      Path to reference. Required for reading/writing CRAM\n"
"  -h, --include-header                 When outputting to stdout, include the header.\n"
"  -s, --strip-tags                     Remove the specified tags, separated by commas. eg. -s RG,MD\n"
"  -S, --strip-all-tags                 Remove all alignment tags\n"
" Filtering options\n"
"  -q, --qc-file                        Output a qc file that contains information about BAM\n"
"  -m, --max-coverage                   Maximum coverage of output file. BAM must be sorted. Negative values enforce a minimum coverage\n"
"  -g, --region                         Regions (e.g. myvcf.vcf or WG for whole genome) or newline seperated subsequence file.  Applied in same order as -r for multiple\n"
"  -G, --exclude-region                 Same as -g, but for region where satisfying a rule EXCLUDES this read. Applied in same order as -r for multiple\n"
"  -l, --linked-region                  Same as -g, but turns on mate-linking\n"
"  -L, --linked-exclude-region           Same as -l, but for mate-linked region where satisfying this rule EXCLUDES this read.\n"
"  -r, --rules                          Script for the rules. If specified multiple times, will be applied in same order as -g\n"
"  -k, --proc-regions-file              Samtools-style region string (e.g. 1:1,000,000-2,000,000) or BED file of regions to proess reads from\n"
"  -P, --region-pad                     Apply a padding to each region supplied to variantBam with the -l, -L, -g or -G flags. ** Must place before -l, etc flag! ** \n"
"\n"
"\n"
"     RULE           #EXAMPLE                     #DESCRIPTION OF EXAMPLE / FLAG\n"
"    motif           \"motif\" : seqs.txt         File containing substrings that must be present in the sequence.\n"
"    duplicate       \"duplicate\" : true         Read must be marked as optical duplicate\n"
"    supp            \"supp\" : false             Read must be primary alignment\n"
"    qcfail          \"qcfail\" : false           Read must note be marked as QC Fail\n"
"    fwd_strand      \"fwd_strand\" : true        Read must be mapped to forward strand\n"
"    rev_strand      \"rev_strand\" : true        Read must be mapped to reverse strand\n"
"    mate_fwd_strand \"mate_fwd_strand\" : true   Mate of read must be mapped to forward strand\n"
"    mate_rev_strand \"mate_rev_strand\" : true   Mate of read must be mapped to reverse strand\n" 
"    mapped          \"mapped\" : true            Read must be unmapped\n"
"    mate_mapped     \"mate_mapped\" : true       Mate must be mapped\n"
"    subsample       \"subsample\" : 0.4          Subsample this region to at a certain rate\n"
"    ff              \"ff\" true                  Read pair must have forward-forward orientation\n"
"    rr              \"rr\" : true                Read pair must have reverse-reverse orientation\n"
"    fr              \"fr\" : true                Read pair must have forward-reverse orientation (proper)\n"
"    rf              \"rf\" : true                Read pair must have reverse-forward orientation\n"
"    ic              \"ic\" : true                Read pair must have inter-chromosomal mapping\n"
"    ... ALL RANGE RULES FOLLOW THE 3 INPUT OPTIONS ILLUSTRATED BELOW ... \n"
"    ins             \"ins\"  : [5,101]           Number of inserted bases on the reads (from parsed CIGAR string)\n"
"                    \"ins\" : 5                  ... Take only reads with max insertion size of >= 5\n"
"                    \"ins\" : [101,5]            ... Take only reads with max insertion size NOT in [5,101] (e.g. 0-4)\n"
"    del             \"del\"  : [5,101]           Number of deleted bases relative to reference (from parsed CIGAR string). \n"
"    nm              \"nm\" : [0,4]               NM tag from BAM (number of mismatches). e.g. must be 0-4 inclusive\n"
"    xp              \"xp\" : [0,4]               Number of supplementary aligments, with XP or XA tag from BAM (hold identity of supplementary alignments)\n"
"    isize           \"isize\" : [100,500]        Insert size, where all insert sizes are converted to positive.\n"
"    len             \"len\" : [80,101]           Length of the read following phred trimming. If phred trimming, don't count hardclips. If not, then HC count to length\n"
"    clip            \"clip\" : [0,5]             Number of clipped bases following phred trimming\n"
"    nbases          \"nbases\" : [0,5]           Removed reads that have within this range of N bases.\n"
"    phred           \"phred\" : [4,100]          Range of phred scores that are considered 'high-quality'\n" 
  //"    discordant      discordant[100,600]  Shortcut for !isize[100,600] || rr || ff || rf || ic (!discordant gives 'proper' pairs)\n"
"\n";

std::vector<SnowTools::CommandLineRegion> command_line_regions;

void __check_command_line(std::vector<SnowTools::CommandLineRegion>& c) {

  if (!c.size())
    c.push_back(SnowTools::CommandLineRegion("WG", -1)); // add whole-genome ALL rule

}

namespace opt {

  static std::string blacklist;
  static std::string bam;
  static std::string out;
  static int max_cov = 0;
  static bool verbose = false;
  static std::string rules = "";
  static std::string proc_regions = "";
  static bool to_stdout = false;
  static bool cram = false;
  static bool header = false;
  static std::string reference = SnowTools::REFHG19;
  static bool strip_all_tags = false;
  static std::string tag_list = "";
  static std::string counts_file = "";
  static bool counts_only = false;
  static std::string bam_qcfile = "";
}

enum {
  OPT_HELP,
  OPT_LENGTH,
  OPT_MAPQ,
  OPT_PHRED,
  OPT_NBASES
};

static const char* shortopts = "hvi:o:r:k:g:Cf:s:ST:l:c:x:q:m:L:G:P:F:R:";
static const struct option longopts[] = {
  { "help",                       no_argument, NULL, OPT_HELP },
  { "linked-region",              required_argument, NULL, 'l' },
  { "min-length",              required_argument, NULL, OPT_LENGTH },
  { "min-phred",              required_argument, NULL, OPT_PHRED },
  { "min-mapq",              required_argument, NULL, OPT_MAPQ },
  { "max-nbases",              required_argument, NULL, OPT_NBASES },
  { "read-group",              required_argument, NULL, 'R' },
  { "include-aln-flag",             required_argument, NULL, 'f' },
  { "exclude-aln-flag",             required_argument, NULL, 'F' },
  { "exclude-region",             required_argument, NULL, 'G' },
  { "linked-exclude-region",      required_argument, NULL, 'L' },
  { "max-coverage",               required_argument, NULL, 'm' },
  { "counts-file",                required_argument, NULL, 'c' },
  { "no-output",                  required_argument, NULL, 'x' },
  { "cram",                       no_argument, NULL, 'C' },
  { "strip-all-tags",             no_argument, NULL, 'S' },
  { "strip-tags",                 required_argument, NULL, 's' },
  { "reference",                  required_argument, NULL, 'T' },
  { "verbose",                    no_argument, NULL, 'v' },
  { "include-header",             no_argument, NULL, 'h' },
  { "input",                      required_argument, NULL, 'i' },
  { "output-bam",                 required_argument, NULL, 'o' },
  { "qc-file",                    no_argument, NULL, 'q' },
  { "rules",                      required_argument, NULL, 'r' },
  { "region",                     required_argument, NULL, 'g' },
  { "region-pad",                 required_argument, NULL, 'P' },
  { "region-with-mates",          required_argument, NULL, 'c' },
  { "proc-regions-file",          required_argument, NULL, 'k' },
  { NULL, 0, NULL, 0 }
};

static struct timespec start;

std::string myreplace(std::string &s,
                      std::string toReplace,
                      std::string replaceWith)
{
  if (s.find(toReplace) == std::string::npos)
    return (s);
  return(s.replace(s.find(toReplace), toReplace.length(), replaceWith));
}

// forward declare
void parseVarOptions(int argc, char** argv);

// helper for formatting rules script string with no whitespace
// http://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
/*  template<typename T, typename P>
    T remove_if(T beg, T end, P pred)
  {
    T dest = beg;
    for (T itr = beg;itr != end; ++itr)
      if (!pred(*itr))
	*(dest++) = *itr;
    return dest;
  }
*/

int main(int argc, char** argv) {

#ifndef __APPLE__
  // start the timer
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif

  // parse the command line
  parseVarOptions(argc, argv);

  bool has_ml_region = opt::rules.find("mlregion") != std::string::npos;
  
  if (opt::verbose) {
    //std::cerr << "Input BAM:  " << opt::bam << std::endl;
    //std::cerr << "Output BAM: " << opt::out << std::endl;
    //std::cerr << "Input rules and regions: " << opt::rules << std::endl;
    //std::cerr << "Input proc regions file: " << opt::proc_regions << std::endl;
    //std::cerr << "TWO-PASS solution?:      " << (opt::twopass ? "ON" : "OFF") << std::endl;
  }

  if (has_ml_region && opt::verbose) {
    std::cerr << "...mate-linked region supplied. Defaulting to whole BAM run unless trimmed explicitly with -k flag" << std::endl;
  }

  // setup the walker
  if (opt::verbose)
    std::cerr << "...setting up the bam walker" << std::endl;
  VariantBamWalker walk(opt::bam);

  GRC grv_proc_regions;
  if (opt::proc_regions.length()) {
    // set which regions to run
    if (opt::verbose)
      std::cerr << "...setting which regions to run from proc_regions" << std::endl;
    if (SnowTools::read_access_test(opt::proc_regions)) {
      grv_proc_regions.regionFileToGRV(opt::proc_regions, 0, walk.header()); // 0 is pad
    } else if (opt::proc_regions.find(":") != std::string::npos) {
      grv_proc_regions.add(SnowTools::GenomicRegion(opt::proc_regions, walk.header()));
    }
    grv_proc_regions.createTreeMap();
  }

  // should it print to stdout?
  if (opt::to_stdout) {
    walk.setStdout();
    walk.setPrintHeader(opt::header);
  }
  // should we print to cram
  else if (opt::cram) {
    walk.setCram(opt::out, opt::reference);
  }

  // should we clear tags?
  if (opt::strip_all_tags)
    walk.setStripAllTags();
  else if (opt::tag_list.length())
    walk.setStripTags(opt::tag_list);

  // make the mini rules collection from the rules file
  // this also calls function to parse the BED files
  if (opt::verbose) {
    std::string str = opt::rules;
    str.erase(std::remove_if(str.begin(), str.end(), [](char x){return std::isspace(x);}),str.end());
    std::cerr << "Rules script: " << str << std::endl;
  }

  SnowTools::MiniRulesCollection mrc;
  mrc.h = walk.header();
  
  if (!opt::rules.empty())
    mrc = SnowTools::MiniRulesCollection(opt::rules, walk.header());

  // make sure command_line_reigons makes sense
  if (command_line_regions.size() == 2 && command_line_regions[1].all()) {
    std::cerr << "***************************************************" << std::endl
              << "  Region (-l, -L, -g, -G) supplied after rule flags"
              << "  Did you mean to set region flag before rule flags"
              << "***************************************************" << std::endl;
    exit(EXIT_FAILURE);
  }
    

  // add specific mini rules from command-line
  for (auto& i : command_line_regions) {
    SnowTools::MiniRules mr(i, walk.header());
    mr.pad = i.pad;
    mr.mrc = &mrc;
    mrc.m_regions.push_back(mr);
  }
  
  walk.m_mr = mrc;

  // set max coverage
  walk.max_cov = opt::max_cov;
  if (opt::max_cov > 0 && opt::verbose)
    std::cerr << "--- Setting MAX coverage to: " << opt::max_cov << std::endl;

  // set the regions to run
  if (grv_proc_regions.size()) {
    if (opt::verbose)
       std::cerr << "...from -g flag will run on " << grv_proc_regions.size() << " regions" << std::endl;
    walk.setBamWalkerRegions(grv_proc_regions.asGenomicRegionVector());
  }

  SnowTools::GRC rules_rg = walk.GetMiniRulesCollection().getAllRegions();

  //  for (auto& i : rules_rg)
  //std::cerr << i << std::endl;

  rules_rg.createTreeMap();

  if (grv_proc_regions.size() && rules_rg.size()) { // intersect rules regions with mask regions. 

    // dont incorporate rules regions if there are any mate-linked regions
    rules_rg = rules_rg.intersection(grv_proc_regions, true); // true -> ignore_strand
    if (opt::verbose)
      std::cerr << "rules region " << rules_rg.size() << std::endl;
  } else if (grv_proc_regions.size()) {
    rules_rg = grv_proc_regions; // rules is whole genome, so just make mask instead
  }

  if (grv_proc_regions.size() > 0 && (rules_rg.size() || has_ml_region )) // explicitly gave regions
    walk.setBamWalkerRegions(grv_proc_regions.asGenomicRegionVector());
  else if (rules_rg.size() && !has_ml_region && grv_proc_regions.size() == 0) {
    walk.setBamWalkerRegions(rules_rg.asGenomicRegionVector());
    if (opt::verbose)
      std::cerr << "...from rules, will run on " << rules_rg.size() << " regions" << std::endl;
  } else if (!rules_rg.size() && grv_proc_regions.size() > 0) {
    std::cerr << "No regions with possibility of reads. This error occurs if no regions in -g are in -k." << std::endl;
    return 1;
  }

  // should we count all rules (slower)
  if (opt::counts_only || opt::counts_file.length())
    walk.setCountAllRules();

  // open the output BAM/CRAM. If we already set SAM, this does nothing
  if (!opt::counts_only)
    walk.OpenWriteBam(opt::out);

  // if counts or qc only, dont write output
  //walk.fop = nullptr;

  // print out some info
  if (opt::verbose) 
    std::cerr << walk << std::endl;

  // set verbosity of walker
  if (opt::verbose)
    walk.setVerbose();

  // do the filtering
  if (opt::verbose)
    std::cerr << "...starting filtering" << std::endl;

  ////////////
  /// RUN THE WALKER
  ////////////
  walk.writeVariantBam();

  // dump the stats file
  if (opt::bam_qcfile.length()) {
    std::ofstream ofs;
    ofs.open(opt::bam_qcfile);
    ofs << walk.m_stats;
    ofs.close();
  }

  // display the rule counts
  walk.MiniRulesToFile(opt::counts_file);

  // make a bed file
  //if (opt::verbose > 0)
  //  std::cerr << "...sending merged regions to BED file" << std::endl;
  //mr->sendToBed("merged_rules.bed");

  // index it
#ifdef HAVE_BAMTOOLS
  walk.MakeIndex();
#endif

  if (opt::verbose) 
    std::cerr << "--- Total time: " << SnowTools::displayRuntime(start) << std::endl;
  
  return 0;
}

void parseVarOptions(int argc, char** argv) {

  bool die = false;

  if (argc < 2) {
    std::cerr << "\n" << VARIANT_BAM_USAGE_MESSAGE;
    exit(EXIT_FAILURE);
  }

  opt::bam = std::string(argv[1]);

  for (char c; (c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1;) {

    std::istringstream arg(optarg != NULL ? optarg : "");
    
    std::string tmp;
    switch (c) {
    case OPT_HELP: die = true; break;
    case 'v': opt::verbose = true; break;
    case 's': arg >> opt::tag_list; break;
    case 'S': opt::strip_all_tags = true; break;
    case 'T': arg >> opt::reference; break;
    case 'C': opt::cram = true; break;
    case 'h': opt::header = true;
    case 'i': arg >> opt::bam; break;
    case 'o': arg >> opt::out; break;
    case 'm': arg >> opt::max_cov; break;
    case 'l': 
	arg >> tmp;
	command_line_regions.push_back(SnowTools::CommandLineRegion(tmp, MINIRULES_MATE_LINKED));
	break;
    case 'L': 
	arg >> tmp;
	command_line_regions.push_back(SnowTools::CommandLineRegion(tmp, MINIRULES_MATE_LINKED_EXCLUDE));
	break;
    case 'g': 
	arg >> tmp;
	command_line_regions.push_back(SnowTools::CommandLineRegion(tmp, MINIRULES_REGION));
	break;
    case 'G': 
	arg >> tmp;
	command_line_regions.push_back(SnowTools::CommandLineRegion(tmp, MINIRULES_REGION_EXCLUDE));	
	break;
    case 'c': arg >> opt::counts_file; break;
    case 'R':
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().rg;
      break;
    case OPT_MAPQ:
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().mapq;
      break;
    case OPT_LENGTH:
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().len;
      break;
    case OPT_PHRED:
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().phred;
      break;
    case OPT_NBASES:
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().nbases;
      break;
    case 'x': arg >> opt::counts_file; opt::counts_only = true; break;
    case 'q': arg >> opt::bam_qcfile; break;
    case 'Q': arg >> opt::bam_qcfile; opt::counts_only = true; break;
    case 'P': 
      if (!command_line_regions.size()) {
	std::cerr << "Error: Must input padding *after* specifying a region via -l, -L, -g, -G" << std::endl;
	exit(EXIT_FAILURE);
      }
      arg >> command_line_regions.back().pad;
      break;
    case 'f': 
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().i_flag;
      break;
    case 'F': 
      __check_command_line(command_line_regions);
      arg >> command_line_regions.back().e_flag;
      break;
    case 'r': 
	arg >> tmp;

	// check if it's a file
	if (SnowTools::read_access_test(tmp)) 
	  {
	    std::ifstream iss(tmp);
	    std::string val;

	    while(std::getline(iss, val))
	       opt::rules += val;
	  }
	else {
	  opt::rules = tmp;
	}
      break;
    case 'k': arg >> opt::proc_regions; break;
    }
  }

  if (opt::bam == "")
    die = true;
  if (opt::out == "" && !opt::counts_only)
    opt::to_stdout = true;

  // dont stop the run for bad bams for quality checking only
  //opt::perc_limit = opt::qc_only ? 101 : opt::perc_limit;

  // something went wrong, kill
  if (die) {
    std::cerr << "\n" << VARIANT_BAM_USAGE_MESSAGE;
    exit(1);
  }

}

