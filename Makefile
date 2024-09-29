# Compiler and flags
CXX = g++
MPICXX = mpic++
CXXFLAGS = -std=c++20 -w -pthread
INCLUDES = -I ../fastflow/
OPTFLAGS = -O3
ADDFLAGS = -march=native -ffast-math
AVXFLAGS = -mavx
DEBUGFLAGS = -g

# Targets
TARGETS = wavefront_pf wavefront_pf_cache wavefront_farm wavefront_seq wavefront_mpi wavefront_seq_cache wavefront_seq_avx

# Normal version
SRC_PF = wavefront_pf.cpp
SRC_FARM = wavefront_farm.cpp
SRC_SEQ = wavefront_seq.cpp
SRC_MPI = wavefront_mpi.cpp
# Cache version
SRC_PFCACHE = wavefront_pf_cache.cpp
SRC_SEQCACHE = wavefront_seq_cache.cpp
# AVX version these includes cache version
SRC_SEQAVX = wavefront_seq_avx.cpp

# Default target
all: $(TARGETS)


# Rules for each target
wavefront_pf: $(SRC_PF)
	$(CXX) $(SRC_PF) -o $@ $(CXXFLAGS) $(INCLUDES) $(ADDFLAGS)

wavefront_pf_cache: $(SRC_PFCACHE)
	$(CXX) $(SRC_PFCACHE) -o $@ $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) $(ADDFLAGS)

wavefront_farm: $(SRC_FARM)
	$(CXX) $(SRC_FARM) -o $@ $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) $(ADDFLAGS)

wavefront_seq: $(SRC_SEQ)
	$(CXX) $(SRC_SEQ) -o $@ $(CXXFLAGS)

wavefront_mpi: $(SRC_MPI)
	$(MPICXX) $(SRC_MPI) -o $@ -std=c++20 -w

wavefront_seq_cache: $(SRC_SEQCACHE)
	$(CXX) $(SRC_SEQCACHE) -o $@ $(CXXFLAGS)

wavefront_seq_avx: $(SRC_SEQAVX)
	$(CXX) $(SRC_SEQAVX) -o $@ $(CXXFLAGS) $(AVXFLAGS)

# Clean target
clean:
	rm -f $(TARGETS)
	rm -f *.txt