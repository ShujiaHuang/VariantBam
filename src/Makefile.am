bin_PROGRAMS = variant

variant_CPPFLAGS = \
     -I$(top_srcdir)/SeqLib \
     -I$(top_srcdir)/SeqLib/htslib

variant_LDADD = \
	$(top_builddir)/SeqLib/src/libseqlib.a \
	$(top_builddir)/SeqLib/htslib/libhts.a \
	$(LDFLAGS)

variant_SOURCES = variant.cpp VariantBamWalker.cpp BamStats.cpp STCoverage.cpp Histogram.cpp
