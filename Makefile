#gcc doesn't seem to have -march under Darwin, but this option is necessary for other
#architectures that don't support atomic fetch and increment properly unless the -march
#is changed

ifeq ($(strip $(DEBUG)),)
    BUILD = -O3
else 
    BUILD = -g3 -pg
endif

ifeq ($(strip $(32BIT)),)
  BUILD2 =
  ifeq "${shell uname -s}" "Darwin"
    CCFLAGS_EXTRA = -DIS64BIT
  else
    CCFLAGS_EXTRA = -march=native -mtune=native -DIS64BIT
  endif
else 
  BUILD2 = -Wl,--large-address-aware
    
  ifeq "${shell uname -s}" "Darwin"
    CCFLAGS_EXTRA =
  else
    CCFLAGS_EXTRA = -march=i486 -mtune=native 
  endif
endif


ifeq ($(strip $(STATIC)),)
    BUILD3 =
else 
    BUILD3 = -static
endif

#Putting -I/include in seems absolutely ridiculous, but the mingw-builds
#mingw-w64 actually needs this. I don't get it either.
LDFLAGS = -lm -lz -lbz2 -lrt ${BUILD} -pthread ${BUILD2} ${BUILD3}
CCFLAGS = -std=gnu99 -W -Wall ${BUILD} ${CCFLAGS_EXTRA} ${ARGS} -pthread -I/include

OBJS = src/topsig-main.o \
src/topsig-config.o \
src/topsig-index.o \
src/topsig-process.o \
src/topsig-stem.o \
src/topsig-porterstemmer.o \
src/topsig-stop.o \
src/topsig-signature.o \
src/topsig-query.o \
src/topsig-search.o \
src/topsig-topic.o \
src/topsig-filerw.o \
src/topsig-file.o \
src/topsig-thread.o \
src/topsig-progress.o \
src/topsig-semaphore.o \
src/topsig-stats.o \
src/topsig-document.o \
src/topsig-issl.o \
src/topsig-experimental-rf.o \
src/topsig-timer.o \
src/topsig-exhaustive-docsim.o \
src/topsig-histogram.o \
src/topsig-resultwriter.o \
src/superfasthash.o \
src/ISAAC-rand.o

default:	topsig

%.o:		%.c
		gcc ${CCFLAGS} -c -o $@ $?

topsig:	${OBJS}
		gcc -o $@ $+ ${LDFLAGS}

topsig-shared:	${OBJS}
		gcc -shared -o $@.dll $+ ${LDFLAGS} -Wl,-no-undefined,--enable-runtime-pseudo-reloc
all-at-once:		
		gcc ${CCFLAGS} -o topsig src/*.c -fwhole-program -flto ${LDFLAGS}

clean:		
		rm -f ${OBJS}

topcat:		
		gcc ${CCFLAGS} -o topcat src/tools/topcat.c

create-random-sigfile:		src/tools/create-random-sigfile.c
		gcc ${CCFLAGS} -o create-random-sigfile src/tools/create-random-sigfile.c

wsj-title-lookup:		src/tools/wsj-title-lookup.c
		gcc ${CCFLAGS} -o wsj-title-lookup src/tools/wsj-title-lookup.c

wiki-link-lookup:		src/tools/wiki-link-lookup.c
		gcc ${CCFLAGS} -o wiki-link-lookup src/tools/wiki-link-lookup.c

wsj-cosine-sim:		src/tools/wsj-cosine-sim.c src/topsig-porterstemmer.c
		gcc ${CCFLAGS} -o wsj-cosine-sim src/tools/wsj-cosine-sim.c src/topsig-porterstemmer.c -Wl,--large-address-aware
		
topcut:		src/tools/topcut.c
		gcc ${CCFLAGS} -o topcut src/tools/topcut.c

toptrim:		src/tools/toptrim.c
		gcc ${CCFLAGS} -o toptrim src/tools/toptrim.c

topsplit:		src/tools/topsplit.c
		gcc ${CCFLAGS} -o topsplit src/tools/topsplit.c

topdeal:		src/tools/topdeal.c
		gcc ${CCFLAGS} -o topdeal src/tools/topdeal.c

topfilt:		src/tools/topfilt.c
		gcc ${CCFLAGS} -o topfilt src/tools/topfilt.c

topshrink:		src/tools/topshrink.c
		gcc ${CCFLAGS} -o topshrink src/tools/topshrink.c

sigview:		src/tools/sigview.c
		gcc ${CCFLAGS} -o sigview src/tools/sigview.c

plagtest:		src/tools/plagtest.c
		gcc ${CCFLAGS} -o plagtest src/tools/plagtest.c
		
plagsummary:		src/tools/plagsummary.c
		gcc ${CCFLAGS} -o plagsummary src/tools/plagsummary.c

sigfile_to_ktree:		src/tools/sigfile_to_ktree.c
		gcc ${CCFLAGS} -o sigfile_to_ktree src/tools/sigfile_to_ktree.c

sigfile_to_text:		src/tools/sigfile_to_text.c
		gcc ${CCFLAGS} -o sigfile_to_text src/tools/sigfile_to_text.c

sigfile_to_texthash:		src/tools/sigfile_to_texthash.c
		gcc ${CCFLAGS} -o sigfile_to_texthash src/tools/sigfile_to_texthash.c

text_to_sigfile:		src/tools/text_to_sigfile.c
		gcc ${CCFLAGS} -o text_to_sigfile src/tools/text_to_sigfile.c

plag-cluster:		src/tools/plag-cluster.c
		gcc ${CCFLAGS} -Wl,--large-address-aware -o plag-cluster src/tools/plag-cluster.c
		
hdr_eval:		src/tools/hdr_eval.c
		gcc ${CCFLAGS} -o hdr_eval src/tools/hdr_eval.c

hdr_eval_csim:		src/tools/hdr_eval_csim.c
		gcc ${CCFLAGS} -o hdr_eval_csim src/tools/hdr_eval_csim.c

topic2docname:		src/tools/topic2docname.c
		gcc ${CCFLAGS} -o topic2docname src/tools/topic2docname.c

resmerge:		src/tools/resmerge.c
		gcc ${CCFLAGS} -o resmerge src/tools/resmerge.c

col2csv:		src/tools/col2csv.c
		gcc ${CCFLAGS} -o col2csv src/tools/col2csv.c

topshuf:		src/tools/topshuf.c
		gcc ${CCFLAGS} -o topshuf src/tools/topshuf.c

convert_featurefile:	src/tools/convert_featurefile.c
		gcc ${CCFLAGS} -o convert_featurefile src/tools/convert_featurefile.c

gstats:	src/tools/gstats.c
		gcc ${CCFLAGS} -o gstats src/tools/gstats.c -lm

wsj-tags:		src/tools/wsj-tags.c
		gcc ${CCFLAGS} -o wsj-tags src/tools/wsj-tags.c

subsig-merge:		src/tools/subsig-merge.c
		gcc ${CCFLAGS} -o subsig-merge src/tools/subsig-merge.c

fastasig:		src/tools/fastasig.c
		gcc ${CCFLAGS} -o fastasig src/tools/fastasig.c src/ISAAC-rand.c -lm
protsig:		src/tools/protsig.c
		gcc ${CCFLAGS} -o protsig src/tools/protsig.c src/ISAAC-rand.c -lm

trec_remove_duplicates:		src/tools/trec_remove_duplicates.c
		gcc ${CCFLAGS} -o trec_remove_duplicates src/tools/trec_remove_duplicates.c
