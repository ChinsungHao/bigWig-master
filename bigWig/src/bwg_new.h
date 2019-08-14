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


//below are from bbiFile.h
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
//end of bbiFile.h




//begin of errCatch.h
struct errCatch
/* Something to help catch errors.   */
{
    struct errCatch *next;	 /* Next in stack. */
//    jmp_buf jmpBuf;		 /* Where to jump back to for recovery. */
    struct dyString *message; /* Error message if any */
    boolean gotError;		 /* Some sort of error was caught. */
};

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





struct slList
{
    struct slList *next;
};

int slCount(const void *list);

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

struct bbiChromInfo *chromInfoArray = NULL;
struct bbiChromInfo *bbiChromList(struct bbiFile *bbi);

struct bbiSummary *bwgReduceSectionList(struct bwgSection *sectionList,
                                        struct bbiChromInfo *chromInfoArray, int reduction);

//struct bbiInterval *metaIntervalsForChrom(struct metaWig *mw, char *chrom, struct lm *lm);
///* Get sorted list of all intervals with data on chromosome. */





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
//end of bw_query.h




//begin of udc.h
void udcSetDefaultDir(char *path);
//end of udc.h


//begin of bits.h
typedef unsigned char Bits;
#define bitToByteSize(bitSize) ((bitSize+7)/8)
//
//Bits *bitAlloc(int bitCount);
///* Allocate bits. */
//
//Bits *bitRealloc(Bits *b, int bitCount, int newBitCount);
///* Resize a bit array.  If b is null, allocate a new array */
//
//Bits *bitClone(Bits* orig, int bitCount);
///* Clone bits. */
//
//void bitFree(Bits **pB);
///* Free bits. */
//
//Bits *lmBitAlloc(struct lm *lm,int bitCount);
//// Allocate bits.  Must supply local memory.
//
//Bits *lmBitRealloc(struct lm *lm, Bits *b, int bitCount, int newBitCount);
//// Resize a bit array.  If b is null, allocate a new array.  Must supply local memory.
//
//Bits *lmBitClone(struct lm *lm, Bits* orig, int bitCount);
//// Clone bits.  Must supply local memory.
//
//void bitSetOne(Bits *b, int bitIx);
///* Set a single bit. */
//
//void bitClearOne(Bits *b, int bitIx);
///* Clear a single bit. */
//
//void bitSetRange(Bits *b, int startIx, int bitCount);
///* Set a range of bits. */
//
//boolean bitReadOne(Bits *b, int bitIx);
///* Read a single bit. */
//
//int bitCountRange(Bits *b, int startIx, int bitCount);
///* Count number of bits set in range. */
//
//int bitFindSet(Bits *b, int startIx, int bitCount);
///* Find the index of the the next set bit. */
//
//int bitFindClear(Bits *b, int startIx, int bitCount);
///* Find the index of the the next clear bit. */
//
//void bitClear(Bits *b, int bitCount);
///* Clear many bits (possibly up to 7 beyond bitCount). */
//
//void bitClearRange(Bits *b, int startIx, int bitCount);
///* Clear a range of bits. */
//
//void bitAnd(Bits *a, Bits *b, int bitCount);
///* And two bitmaps.  Put result in a. */
//
//int bitAndCount(Bits *a, Bits *b, int bitCount);
//// Without altering 2 bitmaps, count the AND bits.
//
//void bitOr(Bits *a, Bits *b, int bitCount);
///* Or two bitmaps.  Put result in a. */
//
//int bitOrCount(Bits *a, Bits *b, int bitCount);
//// Without altering 2 bitmaps, count the OR'd bits.
//
//void bitXor(Bits *a, Bits *b, int bitCount);
///* Xor two bitmaps.  Put result in a. */
//
//int bitXorCount(Bits *a, Bits *b, int bitCount);
//// Without altering 2 bitmaps, count the XOR'd bits.
//
//void bitNot(Bits *a, int bitCount);
///* Flip all bits in a. */
//
//void bitReverseRange(Bits *bits, int startIx, int bitCount);
//// Reverses bits in range (e.g. 110010 becomes 010011)
//
//void bitPrint(Bits *a, int startIx, int bitCount, FILE* out);
///* Print part or all of bit map as a string of 0s and 1s.  Mostly useful for
// * debugging */
//
//void bitsOut(FILE* out, Bits *bits, int startIx, int bitCount, boolean onlyOnes);
//// Print part or all of bit map as a string of 0s and 1s.
//// If onlyOnes, enclose result in [] and use ' ' instead of '0'.
//
//Bits *bitsIn(struct lm *lm,char *bitString, int len);
//// Returns a bitmap from a string of 1s and 0s.  Any non-zero, non-blank char sets a bit.
//// Returned bitmap is the size of len even if that is longer than the string.
//// Optionally supply local memory.  Note does NOT handle enclosing []s printed with bitsOut().
//
//extern int bitsInByte[256];
///* Lookup table for how many bits are set in a byte. */
//
//void bitsInByteInit();
/* Initialize bitsInByte array. */
//end of bits.h


//begin of bigWig.h
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
struct fileOffsetSize
/* A piece of a file. */
{
    struct fileOffsetSize *next;	/* Next in list. */
    bits64	offset;		/* Start offset of block. */
    bits64	size;		/* Size of block. */
};
//end of common.h

//begin of bwgInternal.h
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
//end of bwgInternal.h

#endif
