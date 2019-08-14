//
//  bwg_new.h

//
//  Created by Jinsong Hao on 02/08/2019.
//  Copyright (c) 2019 Jinsong Hao. All rights reserved.
//

#ifndef bwg_new_h
#define bwg_new_h


#include <string.h>
#include <R.h>
#include <Rdefines.h>
#include <Rinternals.h>
#include <stdint.h>
#include <setjmp.h>
#include <assert.h>


#define TRUE 1
#define FALSE 0
#define boolean int
#ifndef	__cplusplus
#ifndef bool
#define bool char
#endif
#endif

#ifndef uint
#define uint unsigned int
#endif


#define UBYTE uint8_t   /* Wants to be unsigned 8 bits. */
#define BYTE int8_t      /* Wants to be signed 8 bits. */
#define UWORD uint16_t  /* Wants to be unsigned 16 bits. */
#define WORD int16_t	      /* Wants to be signed 16 bits. */
#define bits64 uint64_t  /* Wants to be unsigned 64 bits. */
#define bits32 uint32_t       /* Wants to be unsigned 32 bits. */
#define bits16 uint16_t /* Wants to be unsigned 16 bits. */
#define bits8 uint8_t   /* Wants to be unsigned 8 bits. */
#define signed32 int32_t	      /* Wants to be signed 32 bits. */
#define bits8 uint8_t   /* Wants to be unsigned 8 bits. */
//#define SIGJMP_BUF jmp_buf


//begin of cirTree.h
struct cirTreeFile
/* R tree index file handle. */
{
    struct cirTreeFile *next;	/* Next in list of index files if any. */
    char *fileName;		/* Name of file - for error reporting. */
    struct udcFile *udc;			/* Open file pointer. */
    boolean isSwapped;		/* If TRUE need to byte swap everything. */
    bits64 rootOffset;		/* Offset of root block. */
    bits32 blockSize;		/* Size of block. */
    bits64 itemCount;		/* Number of items indexed. */
    bits32 startChromIx;	/* First chromosome in file. */
    bits32 startBase;		/* Starting base position. */
    bits32 endChromIx;		/* Ending chromosome in file. */
    bits32 endBase;		/* Ending base position. */
    bits64 fileSize;		/* Total size of index file. */
    bits32 itemsPerSlot;	/* Max number of items to put in each index slot at lowest level. */
};

void cirTreeFileDetach(struct cirTreeFile **pCrt);
//end of cirTree.h




//below are from bbiFile.h
#define bbiCurrentVersion 4
/* Version history (of file format, not utilities - corresponds to version field in header)
 *    1 - Initial release
 *    1 - Unfortunately when attempting a transparent change to encoders, made the sectionCount
 *        field inconsistent, sometimes not present, sometimes 32 bits.  Since offset positions
 *        in index were still accurate this did not break most applications, but it did show
 *        up in the summary section of the Table Browser.
 *    2 - Made sectionCount consistently 64 bits. Also fixed missing zoomCount in first level of
 *        zoom in files made by bedToBigBed and bedGraphToBigWig.  (The older wigToBigWig was fine.)
 *        Added totalSummary section.
 *    3 - Adding zlib compression.  Only active if uncompressBufSize is non-zero in header.
 *    4 - Fixed problem in encoder for the max field in zoom levels higher than the first one.
 *        Added an extra sig at end of file.
 */

void bigWigFileCreate(
        char *inName, 		/* Input file in ascii wiggle format. */
        char *chromSizes, 	/* Two column tab-separated file: <chromosome> <size>. */
        int blockSize,		/* Number of items to bundle in r-tree.  1024 is good. */
        int itemsPerSlot,	/* Number of items in lowest level of tree.  512 is good. */
        boolean clipDontDie,	/* If TRUE then clip items off end of chrom rather than dying. */
        boolean compress,	/* If TRUE then compress data. */
        boolean keepAllChromosomes,	/* If TRUE then store all chromosomes in chromosomal b-tree. */
        boolean fixedSummaries,	/* If TRUE then impose fixed summary levels. */
        char *outName);
/* Convert ascii format wig file (in fixedStep, variableStep or bedGraph format)
 * to binary big wig format. */

struct bbiFile *bigWigFileOpen(char *fileName);
void bbiFileClose(struct bbiFile **pBwf);
/* Close down a big wig/big bed file. */

struct bbiFile *bbiFileOpen(char *fileName, bits32 sig, char *typeName);

struct fileOffsetSize *bbiOverlappingBlocks(struct bbiFile *bbi, struct cirTreeFile *ctf,
                                            char *chrom, bits32 start, bits32 end, bits32 *retChromId);
/* Fetch list of file blocks that contain items overlapping chromosome range. */

struct bbiZoomLevel
/* A zoom level in bigWig file. */
{
    struct bbiZoomLevel *next;		/* Next in list. */
    bits32 reductionLevel;		/* How many bases per item */
    bits32 reserved;			/* Zero for now. */
    bits64 dataOffset;			/* Offset of data for this level in file. */
    bits64 indexOffset;			/* Offset of index for this level in file. */
};

struct bbiFile
/* An open bbiFile */
{
    struct bbiFile *next;	/* Next in list. */
    char *fileName;		/* Name of file - for better error reporting. */
    struct udcFile *udc;	/* Open UDC file handle. */
    bits32 typeSig;		/* bigBedSig or bigWigSig for now. */
    boolean isSwapped;		/* If TRUE need to byte swap everything. */
    struct bptFile *chromBpt;	/* Index of chromosomes. */
    bits16 version;		/* Version number - initially 1. */
    bits16 zoomLevels;		/* Number of zoom levels. */
    bits64 chromTreeOffset;	/* Offset to chromosome index. */
    bits64 unzoomedDataOffset;	/* Start of unzoomed data. */
    bits64 unzoomedIndexOffset;	/* Start of unzoomed index. */
    bits16 fieldCount;		/* Number of columns in bed version. */
    bits16 definedFieldCount;   /* Number of columns using bed standard definitions. */
    bits64 asOffset;		/* Offset to embedded null-terminated AutoSQL file. */
    bits64 totalSummaryOffset;	/* Offset to total summary information if any.
				   (On older files have to calculate) */
    bits32 uncompressBufSize;	/* Size of uncompression buffer, 0 if uncompressed */
    bits64 extensionOffset;	/* Start of header extension block or 0 if none. */
    struct cirTreeFile *unzoomedCir;	/* Unzoomed data index in memory - may be NULL. */
    struct bbiZoomLevel *levelList;	/* List of zoom levels. */

    /* Fields based on extension block. */
    bits16 extensionSize;   /* Size of extension block */
    bits16 extraIndexCount; /* Number of extra indexes (on fields other than chrom,start,end */
    bits64 extraIndexListOffset;    /* Offset to list of extra indexes */
};

struct bbiChromInfo
/* Pair of a name and a 32-bit integer. Used to assign IDs to chromosomes. */
{
    struct bbiChromInfo *next;
    char *name;		/* Chromosome name */
    bits32 id;		/* Chromosome ID - a small number usually */
    bits32 size;	/* Chromosome size in bases */
};

struct bbiChromInfo *bbiChromList(struct bbiFile *bbi);
/* Return all chromosomes in file.  Dispose of this with bbiChromInfoFreeList. */

void bbiChromInfoFreeList(struct bbiChromInfo **pList);
/* Free a list of bbiChromInfo's */

enum bbiSummaryType
/* Way to summarize data. */
{
    bbiSumMean = 0,	/* Average value */
    bbiSumMax = 1,	/* Maximum value */
    bbiSumMin = 2,	/* Minimum value */
    bbiSumCoverage = 3,  /* Bases in region containing actual data. */
    bbiSumStandardDeviation = 4, /* Standard deviation in window. */
};

struct bbiSummary
/* A summary type item. */
{
    struct bbiSummary *next;
    bits32 chromId;		/* ID of associated chromosome. */
    bits32 start,end;		/* Range of chromosome covered. */
    bits32 validCount;		/* Count of (bases) with actual data. */
    float minVal;		/* Minimum value of items */
    float maxVal;		/* Maximum value of items */
    float sumData;		/* sum of values for each base. */
    float sumSquares;		/* sum of squares for each base. */
    bits64 fileOffset;		/* Offset of summary in file. */
};
//

void bbiAttachUnzoomedCir(struct bbiFile *bbi);
/* Make sure unzoomed cir is attached. */

struct bbiElement
/* An element of a summary from the user side. */
{
    bits64 validCount;		/* Count of (bases) with actual data. */
    double minVal;		/* Minimum value of items */
    double maxVal;		/* Maximum value of items */
    double sumData;		/* sum of values for each base. */
    double sumSquares;		/* sum of squares for each base. */
};
//
struct bbiSummaryElement
/* An element of a summary from the user side. */
{
    bits64 validCount;		/* Count of (bases) with actual data. */
    double minVal;		/* Minimum value of items */
    double maxVal;		/* Maximum value of items */
    double sumData;		/* sum of values for each base. */
    double sumSquares;		/* sum of squares for each base. */
};

struct bbiSummaryElement bbiTotalSummary(struct bbiFile *bbi);
struct bbiInterval
/* Data on a single interval. */
{
    struct bbiInterval *next;	/* Next in list. */
    bits32 start, end;			/* Position in chromosome, half open. */
    double val;				/* Value at that position. */
};

struct hash *bbiChromSizesFromFile(char *fileName);
/* Read two column file into hash keyed by chrom. */
//end of bbiFile.h




//begin of errCatch.h
struct errCatch
/* Something to help catch errors.   */
{
    struct errCatch *next;	 /* Next in stack. */
    jmp_buf jmpBuf;		 /* Where to jump back to for recovery. */
    struct dyString *message; /* Error message if any */
    boolean gotError;		 /* Some sort of error was caught. */
};

struct errCatch *errCatchNew();
/* Return new error catching structure. */

void errCatchFree(struct errCatch **pErrCatch);
/* Free up resources associated with errCatch */

#define errCatchStart(e) (errCatchPushHandlers(e) && setjmp(e->jmpBuf) == 0)
/* Little wrapper around setjmp.  This returns TRUE
 * on the main execution thread, FALSE after abort. */

boolean errCatchPushHandlers(struct errCatch *errCatch);
/* Push error handlers.  Not usually called directly.
 * but rather through errCatchStart() macro.  Always
 * returns TRUE. */

void errCatchEnd(struct errCatch *errCatch);
/* Restore error handlers and pop self off of catching stack. */

boolean errCatchFinish(struct errCatch **pErrCatch);
/* Finish up error catching.  Report error if there is a
 * problem and return FALSE.  If no problem return TRUE.
 * This handles errCatchEnd and errCatchFree. */
//end of errCatch.h




//begin of dyString.h
struct dyString
/* Dynamically resizable string that you can do formatted
 * output to. */
{
    struct dyString *next;	/* Next in list. */
    char *string;		/* Current buffer. */
    int bufSize;		/* Size of buffer. */
    int stringSize;		/* Size of string. */
};

//end of dyString.h



//begin from bigwiglib.h
typedef struct bbiFile bigWig_t;
int is_bigwig(char * filename);

bigWig_t * bigwig_load(const char * filename, const char * udc_dir);
void bigwig_free(bigWig_t * bw);
//end of bigwiglib.h





//begining of bw_query.h
typedef struct {
    double defaultValue;
    int do_abs;

    double total;
    double count;
    double thresh;
} bwStepOpData;

typedef void (* bw_op_clear)(bwStepOpData * data);
typedef void (* bw_op_add)(bwStepOpData * data, double isize, double ivalue);
typedef double (* bw_op_result)(bwStepOpData * data, int step);

typedef struct {
    bw_op_clear clear;
    bw_op_add add;
    bw_op_result result;
} bwStepOp;

void bw_select_op(bwStepOp * op, const char * bw_op_type, int probe_mode);
int bw_step_query_size(int start, int end, int step);
int bw_step_query(bigWig_t * bigwig, bwStepOp * op, const char * chrom, int start, int end, int step, double gap_value, int do_abs, double thresh, double * buffer);
int bw_chrom_step_query(bigWig_t * bigwig, bwStepOp * op, const char * chrom, int step, double gap_value, int do_abs, double * buffer);
//end of bw_query.h

//begin of localmem.h
struct lm *lmInit(int blockSize);
/* Create a local memory pool. Parameters are:
 *      blockSize - how much system memory to allocate at a time.  Can
 *                  pass in zero and a reasonable default will be used.
 */

void lmCleanup(struct lm **pLm);
/* Clean up a local memory pool. */

size_t lmAvailable(struct lm *lm);
// Returns currently available memory in pool

size_t lmSize(struct lm *lm);
// Returns current size of pool, even for memory already allocated

void *lmAlloc(struct lm *lm, size_t size);
/* Allocate memory from local pool. */
#define lmAllocVar(lm, pt) (pt = lmAlloc(lm, sizeof(*pt)));
/* Shortcut to allocating a single variable in local mem and
 * assigning pointer to it. */
//end of localmem.h


//begin of udc.h
void udcSetDefaultDir(char *path);

void udcSeek(struct udcFile *file, bits64 offset);
/* Seek to a particular (absolute) position in file. */

void udcMustRead(struct udcFile *file, void *buf, bits64 size);
/* Read a block from file.  Abort if any problem, including EOF before size is read. */
//end of udc.h


//begin of bits.h
typedef unsigned char Bits;
#define bitToByteSize(bitSize) ((bitSize+7)/8)

boolean bitReadOne(Bits *b, int bitIx);
/* Read a single bit. */
//end of bits.h


//begin of bigWig.h
struct bbiInterval *bigWigIntervalQuery(struct bbiFile *bwf, char *chrom, bits32 start, bits32 end,
                                        struct lm *lm);
/* Get data for interval.  Return list allocated out of lm. */

boolean isBigWig(char *fileName);
/* Peak at a file to see if it's bigWig */

struct bigWigValsOnChrom
/* Object for bulk access a chromosome at a time.  This is faster than
 * doing bigWigInterval queries when you have ~3000 or more queries. */
{
    struct bigWigValsOnChrom *next;
    char *chrom;	/* Current chromosome. */
    long chromSize;	/* Size of current chromosome. */
    long bufSize;	/* Size of allocated buffer */
    double *valBuf;	/* A value for each base on chrom. Zero where no data. */
    Bits *covBuf;	/* A bit for each base with data. */
};
struct bigWigValsOnChrom *bigWigValsOnChromNew();
/* Allocate new empty bigWigValsOnChromStructure. */

void bigWigValsOnChromFree(struct bigWigValsOnChrom **pChromVals);
/* Free up bigWigValsOnChrom */

boolean bigWigValsOnChromFetchData(struct bigWigValsOnChrom *chromVals, char *chrom,
                                   struct bbiFile *bigWig);
//end of bigWig.h

//begin of sig.h
#define bigWigSig 0x888FFC26
/* Signature for a big wig file. */
//end of sig.h

//begin of common.h
/* inline functions: To declare a function inline, place the entire function
 * in a header file and prefix it with the INLINE macro.  If used with a
 * compiler that doesn't support inline, change the INLINE marco to be simply
 * `static'.
 */
#ifndef INLINE
#define INLINE static inline
#endif

void *needLargeMem(size_t size);
/* This calls abort if the memory allocation fails. The memory is
 * not initialized to zero. */

void freeMem(void *pt);
/* Free memory will check for null before freeing. */

#define internalErr()  errAbort("Internal error %s %d", __FILE__, __LINE__)
/* Generic internal error message */

struct slList
{
    struct slList *next;
};

int slCount(const void *list);

INLINE void slAddHead(void *listPt, void *node)
/* Add new node to start of list.
 * Usage:
 *    slAddHead(&list, node);
 * where list and nodes are both pointers to structure
 * that begin with a next pointer.
 */
{
    struct slList **ppt = (struct slList **)listPt;
    struct slList *n = (struct slList *)node;
    n->next = *ppt;
    *ppt = n;
}

void slReverse(void *listPt);
/* Reverse order of a list.
 * Usage:
 *    slReverse(&list);
 */


void slFreeList(void *listPt);
/* Free all elements in list and set list pointer to null.
 * Usage:
 *    slFreeList(&list);
 */

struct fileOffsetSize
/* A piece of a file. */
{
    struct fileOffsetSize *next;	/* Next in list. */
    bits64	offset;		/* Start offset of block. */
    bits64	size;		/* Size of block. */
};

void fileOffsetSizeFindGap(struct fileOffsetSize *list,
                           struct fileOffsetSize **pBeforeGap, struct fileOffsetSize **pAfterGap);
/* Starting at list, find all items that don't have a gap between them and the previous item.
 * Return at gap, or at end of list, returning pointers to the items before and after the gap. */

bits32 memReadBits32(char **pPt, boolean isSwapped);
/* Read and optionally byte-swap 32 bit entity from memory buffer pointed to by
 * *pPt, and advance *pPt past read area. */

float memReadFloat(char **pPt, boolean isSwapped);
/* Read and optionally byte-swap single-precision floating point entity
 * from memory buffer pointed to by *pPt, and advance *pPt past read area. */

FILE *mustOpen(char *fileName, char *mode);
/* Open a file - or squawk and die. */
void mustWrite(FILE *file, void *buf, size_t size);
/* Write to file or squawk and die. */

#define writeOne(file, var) mustWrite((file), &(var), sizeof(var))
/* Write out one variable to file. */

//end of common.h

//begin of bwgInternal.h
enum bwgSectionType
/* Code to indicate section type. */
{
    bwgTypeBedGraph=1,
    bwgTypeVariableStep=2,
    bwgTypeFixedStep=3,
};


struct bwgSection
/* A section of a bigWig file - all on same chrom.  This is a somewhat fat data
 * structure used by the bigWig creation code.  See also bwgSection for the
 * structure returned by the bigWig reading code. */
{
    struct bwgSection *next;		/* Next in list. */
    char *chrom;			/* Chromosome name. */
    bits32 start,end;			/* Range of chromosome covered. */
    enum bwgSectionType type;
//    union bwgItem items;		/* List/array of items in this section. */
    bits32 itemStep;			/* Step within item if applicable. */
    bits32 itemSpan;			/* Item span if applicable. */
    bits16 itemCount;			/* Number of items in section. */
    bits32 chromId;			/* Unique small integer value for chromosome. */
    bits64 fileOffset;			/* Offset of section in file. */
};

struct bwgSection *bwgParseWig(
        char *fileName,       /* Name of ascii wig file. */
        boolean clipDontDie,  /* Skip items outside chromosome rather than aborting. */
        struct hash *chromSizeHash,  /* If non-NULL items checked to be inside chromosome. */
        int maxSectionSize,   /* Biggest size of a section.  100 - 100,000 is usual range. */
        struct lm *lm);	      /* Memory pool to allocate from. */
/* Parse out ascii wig file - allocating memory in lm. */
;


struct bwgSectionHead
/* A header from a bigWig file section - similar to above bug what is on disk. */
{
    bits32 chromId;	/* Chromosome short identifier. */
    bits32 start,end;	/* Range covered. */
    bits32 itemStep;	/* For some section types, the # of bases between items. */
    bits32 itemSpan;	/* For some section types, the # of bases in each item. */
    UBYTE type;		/* Type byte. */
    UBYTE reserved;	/* Always zero for now. */
    bits16 itemCount;	/* Number of items in block. */
};
void bwgSectionHeadFromMem(char **pPt, struct bwgSectionHead *head, boolean isSwapped);
/* Read section header. */

void bwgCreate(struct bwgSection *sectionList, struct hash *chromSizeHash,
               int blockSize, int itemsPerSlot, boolean doCompress, boolean keepAllChromosomes,
               boolean fixedSummaries, char *fileName);
/* Create a bigWig file out of a sorted sectionList.  A lower level routine
 * than the one above. */

//end of bwgInternal.h




//begin of hmmstats.h
double calcStdFromSums(double sum, double sumSquares, bits64 n);
/* Calculate standard deviation. */
//end of hmmstats.h





//begin of bw_base.h
int bw_has_chrom(bigWig_t * bw, const char * chromName);
long bw_chrom_size(bigWig_t * bw, const char * chromName);
//end of bw_base.h

//begin of errAbort.h
void errAbort(char *format, ...)
/* Abort function, with optional (printf formatted) error message. */
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
;
//end of errAbort.h


//begin of zlibFace.h
size_t zUncompress(
        void *compressed,	/* Compressed area */
        size_t compressedSize,	/* Size after compression */
        void *uncompBuf,	/* Where to put uncompressed bits */
        size_t uncompBufSize);	/* Max size of uncompressed bits. */
/* Uncompress data from memory to memory.  Returns size after decompression. */

//end of zlibFace.h

#endif
