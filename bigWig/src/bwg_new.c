//
// Created by Jinsong Hao on 2019-08-14.
#include <stdbool.h>
#include "bwg_new.h"

//begin of linefile.c
char *getFileNameFromHdrSig(char *m)
/* Check if header has signature of supported compression stream,
   and return a phoney filename for it, or NULL if no sig found. */
{
    char buf[20];
    char *ext=NULL;
    if (startsWith("\x1f\x8b",m)) ext = "gz";
    else if (startsWith("\x1f\x9d\x90",m)) ext = "Z";
    else if (startsWith("BZ",m)) ext = "bz2";
    else if (startsWith("PK\x03\x04",m)) ext = "zip";
    if (ext==NULL)
        return NULL;
    safef(buf, sizeof(buf), "somefile.%s", ext);
    return cloneString(buf);
}

static void metaDataAdd(struct lineFile *lf, char *line)
/* write a line of metaData to output file
 * internal function called by lineFileNext */
{
    struct metaOutput *meta = NULL;

    if (lf->isMetaUnique)
    {
        /* suppress repetition of comments */
        if (hashLookup(lf->metaLines, line))
        {
            return;
        }
        hashAdd(lf->metaLines, line, NULL);
    }
    for (meta = lf->metaOutput ; meta != NULL ; meta = meta->next)
        if (line != NULL && meta->metaFile != NULL)
            fprintf(meta->metaFile,"%s\n", line);
}

static char **getDecompressor(char *fileName)
/* if a file is compressed, return the command to decompress the
 * approriate format, otherwise return NULL */
{
    static char *GZ_READ[] = {"gzip", "-dc", NULL};
    static char *Z_READ[] = {"gzip", "-dc", NULL};
    static char *BZ2_READ[] = {"bzip2", "-dc", NULL};
    static char *ZIP_READ[] = {"gzip", "-dc", NULL};

    char **result = NULL;
    char *fileNameDecoded = cloneString(fileName);
    if (startsWith("http://" , fileName)
        || startsWith("https://", fileName)
        || startsWith("ftp://",   fileName))
        cgiDecode(fileName, fileNameDecoded, strlen(fileName));

    if      (endsWith(fileNameDecoded, ".gz"))
        result = GZ_READ;
    else if (endsWith(fileNameDecoded, ".Z"))
        result = Z_READ;
    else if (endsWith(fileNameDecoded, ".bz2"))
        result = BZ2_READ;
    else if (endsWith(fileNameDecoded, ".zip"))
        result = ZIP_READ;

    freeMem(fileNameDecoded);
    return result;

}


static char * headerBytes(char *fileName, int numbytes)
/* Return specified number of header bytes from file
 * if file exists as a string which should be freed. */
{
    int fd,bytesread=0;
    char *result = NULL;
    if ((fd = open(fileName, O_RDONLY)) >= 0)
    {
        result=needMem(numbytes+1);
        if ((bytesread=read(fd,result,numbytes)) < numbytes)
            freez(&result);  /* file too short? can read numbytes */
        else
            result[numbytes]=0;
        close(fd);
    }
    return result;
}


struct lineFile *lineFileDecompress(char *fileName, bool zTerm)
/* open a linefile with decompression */
{
    struct pipeline *pl;
    struct lineFile *lf;
    char *testName = NULL;
    char *testbytes = NULL;    /* the header signatures for .gz, .bz2, .Z,
			    * .zip are all 2-4 bytes only */
    if (fileName==NULL)
        return NULL;
    testbytes=headerBytes(fileName,4);
    if (!testbytes)
        return NULL;  /* avoid error from pipeline */
    testName=getFileNameFromHdrSig(testbytes);
    freez(&testbytes);
    if (!testName)
        return NULL;  /* avoid error from pipeline */
    pl = pipelineOpen1(getDecompressor(fileName), pipelineRead|pipelineSigpipe, fileName, NULL);
    lf = lineFileAttach(fileName, zTerm, pipelineFd(pl));
    lf->pl = pl;
    return lf;
}

int lineFileLongNetRead(int fd, char *buf, int size)
/* Keep reading until either get no new characters or
 * have read size */
{
    int oneSize, totalRead = 0;

    while (size > 0)
    {
        oneSize = read(fd, buf, size);
        if (oneSize <= 0)
            break;
        totalRead += oneSize;
        buf += oneSize;
        size -= oneSize;
    }
    return totalRead;
}

struct lineFile *lineFileMayOpen(char *fileName, bool zTerm)
/* Try and open up a lineFile. */
{
    if (sameString(fileName, "stdin"))
        return lineFileStdin(zTerm);
    else if (getDecompressor(fileName) != NULL)
        return lineFileDecompress(fileName, zTerm);
    else
    {
        int fd = open(fileName, O_RDONLY);
        if (fd == -1)
            return NULL;
        return lineFileAttach(fileName, zTerm, fd);
    }
}


struct lineFile *lineFileOpen(char *fileName, bool zTerm)
/* Open up a lineFile or die trying. */
{
    struct lineFile *lf = lineFileMayOpen(fileName, zTerm);
    if (lf == NULL)
        errAbort("Couldn't open %s , %s", fileName, strerror(errno));
    return lf;
}

boolean lineFileNextReal(struct lineFile *lf, char **retStart)
/* Fetch next line from file that is not blank and
 *  * does not start with a '#'. */
{
    char *s, c;
    while (lineFileNext(lf, retStart, NULL))
    {
        s = skipLeadingSpaces(*retStart);
        c = s[0];
        if (c != 0 && c != '#')
            return TRUE;
    }
    return FALSE;
}

void lineFileReuse(struct lineFile *lf)
/* Reuse current line. */
{
    lf->reuse = TRUE;
}

void lineFileRemoveInitialCustomTrackLines(struct lineFile *lf)
/* remove initial browser and track lines */
{
    char *line;
    while (lineFileNextReal(lf, &line))
    {
        if (!(startsWith("browser", line) || startsWith("track", line) ))
        {
            verbose(2, "found line not browser or track: %s\n", line);
            lineFileReuse(lf);
            break;
        }
        verbose(2, "skipping %s\n", line);
    }
}

int lineFileNeedNum(struct lineFile *lf, char *words[], int wordIx)
/* Make sure that words[wordIx] is an ascii integer, and return
 * binary representation of it. Conversion stops at first non-digit char. */
{
    char *ascii = words[wordIx];
    char c = ascii[0];
    if (c != '-' && !isdigit(c))
        errAbort("Expecting number field %d line %d of %s, got %s",
                 wordIx+1, lf->lineIx, lf->fileName, ascii);
    return atoi(ascii);
}

double lineFileNeedDouble(struct lineFile *lf, char *words[], int wordIx)
/* Make sure that words[wordIx] is an ascii double value, and return
 * binary representation of it. */
{
    char *valEnd;
    char *val = words[wordIx];
    double doubleValue;

    doubleValue = strtod(val, &valEnd);
    if ((*val == '\0') || (*valEnd != '\0'))
        errAbort("Expecting double field %d line %d of %s, got %s",
                 wordIx+1, lf->lineIx, lf->fileName, val);
    return doubleValue;
}

struct lineFile *lineFileAttach(char *fileName, bool zTerm, int fd)
/* Wrap a line file around an open'd file. */
{
    struct lineFile *lf;
    AllocVar(lf);
    lf->fileName = cloneString(fileName);
    lf->fd = fd;
    lf->bufSize = 64*1024;
    lf->zTerm = zTerm;
    lf->buf = needMem(lf->bufSize+1);
    return lf;
}

struct lineFile *lineFileStdin(bool zTerm)
/* Wrap a line file around stdin. */
{
    return lineFileAttach("stdin", zTerm, fileno(stdin));
}

boolean lineFileNext(struct lineFile *lf, char **retStart, int *retSize)
/* Fetch next line from file. */
{
    char *buf = lf->buf;
    int bytesInBuf = lf->bytesInBuf;
    int endIx = lf->lineEnd;
    boolean gotLf = FALSE;
    int newStart;

    if (lf->reuse)
    {
        lf->reuse = FALSE;
        if (retSize != NULL)
            *retSize = lf->lineEnd - lf->lineStart;
        *retStart = buf + lf->lineStart;
        if (lf->metaOutput && *retStart[0] == '#')
            metaDataAdd(lf, *retStart);
        return TRUE;
    }

    if (lf->nextCallBack)
        return lf->nextCallBack(lf, retStart, retSize);

    if (lf->udcFile)
    {
        lf->bufOffsetInFile = udcTell(lf->udcFile);
        char *line = udcReadLine(lf->udcFile);
        if (line==NULL)
            return FALSE;
        int lineSize = strlen(line);
        lf->bytesInBuf = lineSize;
        lf->lineIx = -1;
        lf->lineStart = 0;
        lf->lineEnd = lineSize;
        *retStart = line;
        freeMem(lf->buf);
        lf->buf = line;
        lf->bufSize = lineSize;
        return TRUE;
    }

#ifdef USE_TABIX
    if (lf->tabix != NULL && lf->tabixIter != NULL)
    {
    // Just use line-oriented ti_read:
    int lineSize = 0;
    const char *line = ti_read(lf->tabix, lf->tabixIter, &lineSize);
    if (line == NULL)
	return FALSE;
    lf->bufOffsetInFile = -1;
    lf->bytesInBuf = lineSize;
    lf->lineIx = -1;
    lf->lineStart = 0;
    lf->lineEnd = lineSize;
    if (lineSize > lf->bufSize)
	// shouldn't be!  but just in case:
	lineFileExpandBuf(lf, lineSize * 2);
    safecpy(lf->buf, lf->bufSize, line);
    *retStart = lf->buf;
    if (retSize != NULL)
	*retSize = lineSize;
    return TRUE;
    }
#endif // USE_TABIX

    determineNlType(lf, buf+endIx, bytesInBuf);

/* Find next end of line in buffer. */
    switch(lf->nlType)
    {
        case nlt_unix:
        case nlt_dos:
            for (endIx = lf->lineEnd; endIx < bytesInBuf; ++endIx)
            {
                if (buf[endIx] == '\n')
                {
                    gotLf = TRUE;
                    endIx += 1;
                    break;
                }
            }
            break;
        case nlt_mac:
            for (endIx = lf->lineEnd; endIx < bytesInBuf; ++endIx)
            {
                if (buf[endIx] == '\r')
                {
                    gotLf = TRUE;
                    endIx += 1;
                    break;
                }
            }
            break;
        case nlt_undet:
            break;
    }

/* If not in buffer read in a new buffer's worth. */
    while (!gotLf)
    {
        int oldEnd = lf->lineEnd;
        int sizeLeft = bytesInBuf - oldEnd;
        int bufSize = lf->bufSize;
        int readSize = bufSize - sizeLeft;

        if (oldEnd > 0 && sizeLeft > 0)
        {
            memmove(buf, buf+oldEnd, sizeLeft);
        }
        lf->bufOffsetInFile += oldEnd;
        if (lf->fd >= 0)
            readSize = lineFileLongNetRead(lf->fd, buf+sizeLeft, readSize);
#ifdef USE_TABIX
            else if (lf->tabix != NULL && readSize > 0)
	{
	readSize = ti_bgzf_read(lf->tabix->fp, buf+sizeLeft, readSize);
	if (readSize < 1)
	    return FALSE;
	}
#endif // USE_TABIX
        else
            readSize = 0;

        if ((readSize == 0) && (endIx > oldEnd))
        {
            endIx = sizeLeft;
            buf[endIx] = 0;
            lf->bytesInBuf = newStart = lf->lineStart = 0;
            lf->lineEnd = endIx;
            ++lf->lineIx;
            if (retSize != NULL)
                *retSize = endIx - newStart;
            *retStart = buf + newStart;
            if (*retStart[0] == '#')
                metaDataAdd(lf, *retStart);
            return TRUE;
        }
        else if (readSize <= 0)
        {
            lf->bytesInBuf = lf->lineStart = lf->lineEnd = 0;
            return FALSE;
        }
        bytesInBuf = lf->bytesInBuf = readSize + sizeLeft;
        lf->lineEnd = 0;

        determineNlType(lf, buf+endIx, bytesInBuf);

        /* Look for next end of line.  */
        switch(lf->nlType)
        {
            case nlt_unix:
            case nlt_dos:
                for (endIx = sizeLeft; endIx <bytesInBuf; ++endIx)
                {
                    if (buf[endIx] == '\n')
                    {
                        endIx += 1;
                        gotLf = TRUE;
                        break;
                    }
                }
                break;
            case nlt_mac:
                for (endIx = sizeLeft; endIx <bytesInBuf; ++endIx)
                {
                    if (buf[endIx] == '\r')
                    {
                        endIx += 1;
                        gotLf = TRUE;
                        break;
                    }
                }
                break;
            case nlt_undet:
                break;
        }
        if (!gotLf && bytesInBuf == lf->bufSize)
        {
            if (bufSize >= 512*1024*1024)
            {
                errAbort("Line too long (more than %d chars) line %d of %s",
                         lf->bufSize, lf->lineIx+1, lf->fileName);
            }
            else
            {
                lineFileExpandBuf(lf, bufSize*2);
                buf = lf->buf;
            }
        }
    }

    if (lf->zTerm)
    {
        buf[endIx-1] = 0;
        if ((lf->nlType == nlt_dos) && (buf[endIx-2]=='\r'))
        {
            buf[endIx-2] = 0;
        }
    }

    lf->lineStart = newStart = lf->lineEnd;
    lf->lineEnd = endIx;
    ++lf->lineIx;
    if (retSize != NULL)
        *retSize = endIx - newStart;
    *retStart = buf + newStart;
    if (*retStart[0] == '#')
        metaDataAdd(lf, *retStart);
    return TRUE;
}

void lineFileExpandBuf(struct lineFile *lf, int newSize)
/* Expand line file buffer. */
{
    assert(newSize > lf->bufSize);
    lf->buf = needMoreMem(lf->buf, lf->bytesInBuf, newSize);
    lf->bufSize = newSize;
}

//end of linefile.c

//begin of pipeline.c
enum procState
/* process state, in order of transition */
{
    procStateNew,  // plProc object created
    procStateRun,  // proccess running
    procStateDone  // process finished (ok or failed)
};

struct plProc
/* A single process in a pipeline */
{
    struct plProc *next;   /* order list of processes */
    struct pipeline *pl;   /* pipeline we are associated with */
    char **cmd;            /* null-terminated command for this process */
    pid_t  pid;            /* pid for process, -1 if not running */
    enum procState state;  /* state of process */
    int status;            /* status from wait */
};

struct pipeline
/* Object for a process pipeline and associated open file.  Pipeline process
 * consist of a process group leader and then all of the child process.  The
 * group leader does no work, just wait on processes to complete and report
 * errors to the top level process.  This object is create in the calling
 * process, and then passed down, but not shared, via forks.
 */
{
    struct plProc *procs;      /* list of processes */
    int numRunning;            /* number of processes running */
    pid_t groupLeader;         /* process group id, or -1 if not set. This is pid of group leader */
    char *procName;            /* name to use in error messages. */
    int pipeFd;                /* fd of pipe to/from process, -1 if none */
    unsigned options;          /* options */
    FILE* pipeFh;              /* optional stdio around pipe */
    char* stdioBuf;            /* optional stdio buffer */
    struct lineFile *pipeLf;   /* optional lineFile around pipe */
};

/* file buffer size */
#define FILE_BUF_SIZE 64*1024

static int pipeCreate(int *writeFd)
/* create a pipe or die, return readFd */
{
    int pipeFds[2];
    if (pipe(pipeFds) < 0)
        errnoAbort("can't create pipe");
    *writeFd = pipeFds[1];
    return pipeFds[0];
}

static void safeClose(int *fdPtr)
/* Close with error checking.  *fdPtr == -1 indicated already closed */
{
    int fd = *fdPtr;
    if (fd != -1)
    {
        if (close(fd) < 0)
            errnoAbort("close failed on fd %d", fd);
        *fdPtr = -1;
    }
}



struct pipeline *pipelineOpen1(char **cmd, unsigned opts,
                               char *otherEndFile, char *stderrFile)
/* like pipelineOpen(), only takes a single command */
{
    char **cmds[2];
    cmds[0] = cmd;
    cmds[1] = NULL;
    return pipelineOpen(cmds, opts, otherEndFile, stderrFile);
}

int pipelineFd(struct pipeline *pl)
/* Get the file descriptor for a pipeline */
{
    return pl->pipeFd;
}

struct pipeline *pipelineOpen(char ***cmds, unsigned opts,
                              char *otherEndFile, char *stderrFile)
/* Create a pipeline from an array of commands.  See pipeline.h for
 * full documentation */
{
    int otherEndFd;
    int stderrFd = (stderrFile == NULL) ? STDERR_FILENO : openWrite(stderrFile, FALSE);

    checkOpts(opts);
    boolean append = ((opts & pipelineAppend) != 0);
    if (opts & pipelineRead)
        otherEndFd = (otherEndFile == NULL) ? STDIN_FILENO : openRead(otherEndFile);
    else
        otherEndFd = (otherEndFile == NULL) ? STDOUT_FILENO : openWrite(otherEndFile, append);
    struct pipeline *pl = pipelineOpenFd(cmds, opts, otherEndFd, stderrFd);
    safeClose(&otherEndFd);
    if (stderrFile != NULL)
        safeClose(&stderrFd);
    return pl;
}


//end of pipeline.c







//begin of bwgCreate.c

struct bwgSection *bwgParseWig(
        char *fileName,       /* Name of ascii wig file. */
        boolean clipDontDie,  /* Skip items outside chromosome rather than aborting. */
        struct hash *chromSizeHash,  /* If non-NULL items checked to be inside chromosome. */
        int maxSectionSize,   /* Biggest size of a section.  100 - 100,000 is usual range. */
        struct lm *lm)	      /* Memory pool to allocate from. */
/* Parse out ascii wig file - allocating memory in lm. */
{
    struct lineFile *lf = lineFileOpen(fileName, TRUE);
    char *line;
    struct bwgSection *sectionList = NULL;

/* remove initial browser and track lines */
    lineFileRemoveInitialCustomTrackLines(lf);

    while (lineFileNextReal(lf, &line))
    {
        verbose(2, "processing %s\n", line);
        if (stringIn("chrom=", line))
            parseSteppedSection(lf, clipDontDie, chromSizeHash, line, lm, maxSectionSize, &sectionList);
        else
        {
            /* Check for bed... */
            char *dupe = cloneString(line);
            char *words[5];
            int wordCount = chopLine(dupe, words);
            if (wordCount != 4)
                errAbort("Unrecognized line %d of %s:\n%s\n", lf->lineIx, lf->fileName, line);

            /* Parse out a bed graph line just to check numerical format. */
            char *chrom = words[0];
            int start = lineFileNeedNum(lf, words, 1);
            int end = lineFileNeedNum(lf, words, 2);
            double val = lineFileNeedDouble(lf, words, 3);
            verbose(2, "bedGraph %s:%d-%d@%g\n", chrom, start, end, val);

            /* Push back line and call bed parser. */
            lineFileReuse(lf);
            parseBedGraphSection(lf, clipDontDie, chromSizeHash, lm, maxSectionSize, &sectionList);
        }
    }
    slSort(&sectionList, bwgSectionCmp);

/* Check for overlap at section level. */
    struct bwgSection *section, *nextSection;
    for (section = sectionList; section != NULL; section = nextSection)
    {
        nextSection = section->next;
        if (nextSection != NULL)
        {
            if (sameString(section->chrom, nextSection->chrom))
            {
                if (section->end > nextSection->start)
                {
                    errAbort("There's more than one value for %s base %d (in coordinates that start with 1).\n",
                             section->chrom, nextSection->start+1);
                }
            }
        }
    }

    return sectionList;
}

void bigWigFileCreate(
        char *inName, 		/* Input file in ascii wiggle format. */
        char *chromSizes, 	/* Two column tab-separated file: <chromosome> <size>. */
        int blockSize,		/* Number of items to bundle in r-tree.  1024 is good. */
        int itemsPerSlot,	/* Number of items in lowest level of tree.  512 is good. */
        boolean clipDontDie,	/* If TRUE then clip items off end of chrom rather than dying. */
        boolean compress,	/* If TRUE then compress data. */
        boolean keepAllChromosomes,	/* If TRUE then store all chromosomes in chromosomal b-tree. */
        boolean fixedSummaries,	/* If TRUE then impose fixed summary levels. */
        char *outName)
/* Convert ascii format wig file (in fixedStep, variableStep or bedGraph format)
 * to binary big wig format. */
{
/* This code needs to agree with code in two other places currently - bigBedFileCreate,
 * and bbiFileOpen.  I'm thinking of refactoring to share at least between
 * bigBedFileCreate and bigWigFileCreate.  It'd be great so it could be structured
 * so that it could send the input in one chromosome at a time, and send in the zoom
 * stuff only after all the chromosomes are done.  This'd potentially reduce the memory
 * footprint by a factor of 2 or 4.  Still, for now it works. -JK */
    struct hash *chromSizeHash = bbiChromSizesFromFile(chromSizes);
    struct lm *lm = lmInit(0);
    struct bwgSection *sectionList = bwgParseWig(inName, clipDontDie, chromSizeHash, itemsPerSlot, lm);
    if (sectionList == NULL)
        errAbort("%s is empty of data", inName);
    bwgCreate(sectionList, chromSizeHash, blockSize, itemsPerSlot, compress, keepAllChromosomes, fixedSummaries, outName);
    lmCleanup(&lm);
}

void bwgMakeChromInfo(struct bwgSection *sectionList, struct hash *chromSizeHash,
                      int *retChromCount, struct bbiChromInfo **retChromArray,
                      int *retMaxChromNameSize)
/* Fill in chromId field in sectionList.  Return array of chromosome name/ids.
 * The chromSizeHash is keyed by name, and has int values. */
{
/* Build up list of unique chromosome names. */
    struct bwgSection *section;
    char *chromName = "";
    int chromCount = 0;
    int maxChromNameSize = 0;
    struct slRef *uniq, *uniqList = NULL;
    for (section = sectionList; section != NULL; section = section->next)
    {
        if (!sameString(section->chrom, chromName))
        {
            chromName = section->chrom;
            refAdd(&uniqList, chromName);
            ++chromCount;
            int len = strlen(chromName);
            if (len > maxChromNameSize)
                maxChromNameSize = len;
        }
        section->chromId = chromCount-1;
    }
    slReverse(&uniqList);

/* Allocate and fill in results array. */
    struct bbiChromInfo *chromArray;
    AllocArray(chromArray, chromCount);
    int i;
    for (i = 0, uniq = uniqList; i < chromCount; ++i, uniq = uniq->next)
    {
        chromArray[i].name = uniq->val;
        chromArray[i].id = i;
        chromArray[i].size = hashIntVal(chromSizeHash, uniq->val);
    }

/* Clean up, set return values and go home. */
    slFreeList(&uniqList);
    *retChromCount = chromCount;
    *retChromArray = chromArray;
    *retMaxChromNameSize = maxChromNameSize;
}

static int bwgStrcmp (const void * A, const void * B) {
    char * stringA = *((char **) A);
    char * stringB = *((char **) B);
    return strcmp(stringA, stringB);
}

void bwgMakeAllChromInfo(struct bwgSection *sectionList, struct hash *chromSizeHash,
                         int *retChromCount, struct bbiChromInfo **retChromArray,
                         int *retMaxChromNameSize)
/* Fill in chromId field in sectionList.  Return array of chromosome name/ids.
 * The chromSizeHash is keyed by name, and has int values. */
{
/* Build up list of unique chromosome names. */
    int maxChromNameSize = 0;

/* Get list of values */
    int chromCount = chromSizeHash->elCount;
    char ** chromName, ** chromNames;
    AllocArray(chromNames, chromCount);
    chromName = chromNames;
    struct hashEl* el;
    struct hashCookie cookie = hashFirst(chromSizeHash);
    for (el = hashNext(&cookie); el; el = hashNext(&cookie)) {
        *chromName = el->name;
        if (strlen(el->name) > maxChromNameSize)
            maxChromNameSize = strlen(el->name);
        chromName++;
    }
    qsort(chromNames, chromCount, sizeof(char *), bwgStrcmp);

/* Allocate and fill in results array. */
    struct bbiChromInfo *chromArray;
    AllocArray(chromArray, chromCount);
    int i;
    for (i = 0; i < chromCount; ++i)
    {
        chromArray[i].name = chromNames[i];
        chromArray[i].id = i;
        chromArray[i].size = hashIntVal(chromSizeHash, chromNames[i]);
    }

// Assign IDs to sections:
    struct bwgSection *section;
    char *name = "";
    bits32 chromId = 0;
    for (section = sectionList; section != NULL; section = section->next)
    {
        if (!sameString(section->chrom, name))
        {
            for (i = 0; i < chromCount; ++i)
            {
                if (sameString(section->chrom, chromArray[i].name))
                {
                    section->chromId = i;
                    break;
                }
            }
            if (i == chromCount)
                errAbort("Could not find %s in list of chromosomes\n", section->chrom);
            chromId = section->chromId;
            name = section->chrom;
        }
        else
            section->chromId = chromId;
    }

/* Clean up, set return values and go home. */
    *retChromCount = chromCount;
    *retChromArray = chromArray;
    *retMaxChromNameSize = maxChromNameSize;
}

static void bwgComputeDynamicSummaries(struct bwgSection *sectionList, struct bbiSummary ** reduceSummaries, bits16 * summaryCount, struct bbiChromInfo *chromInfoArray, int chromCount, bits32 * reductionAmounts, boolean doCompress) {
/* Figure out initial summary level - starting with a summary 10 times the amount
 * of the smallest item.  See if summarized data is smaller than half input data, if
 * not bump up reduction by a factor of 2 until it is, or until further summarying
 * yeilds no size reduction. */
    int i;
    int  minRes = bwgAverageResolution(sectionList);
    int initialReduction = minRes*10;
    bits64 fullSize = bwgTotalSectionSize(sectionList);
    bits64 lastSummarySize = 0, summarySize;
    bits64 maxReducedSize = fullSize/2;
    struct bbiSummary *summaryList = NULL;
    for (;;)
    {
        summaryList = bwgReduceSectionList(sectionList, chromInfoArray, initialReduction);
        bits64 summarySize = bbiTotalSummarySize(summaryList);
        if (doCompress)
        {
            summarySize *= 2;	// Compensate for summary not compressing as well as primary data
        }
        if (summarySize >= maxReducedSize && summarySize != lastSummarySize)
        {
            /* Need to do more reduction.  First scale reduction by amount that it missed
             * being small enough last time, with an extra 10% for good measure.  Then
             * just to keep from spinning through loop two many times, make sure this is
             * at least 2x the previous reduction. */
            int nextReduction = 1.1 * initialReduction * summarySize / maxReducedSize;
            if (nextReduction < initialReduction*2)
                nextReduction = initialReduction*2;
            initialReduction = nextReduction;
            bbiSummaryFreeList(&summaryList);
            lastSummarySize = summarySize;
        }
        else
            break;
    }
    *summaryCount = 1;
    reduceSummaries[0] = summaryList;
    reductionAmounts[0] = initialReduction;

/* Now calculate up to 10 levels of further summary. */
    bits64 reduction = initialReduction;
    for (i=0; i<9; i++)
    {
        reduction *= 4;
        if (reduction > 1000000000)
            break;
        summaryList = bbiReduceSummaryList(reduceSummaries[*summaryCount-1], chromInfoArray,
                                           reduction);
        summarySize = bbiTotalSummarySize(summaryList);
        if (summarySize != lastSummarySize)
        {
            reduceSummaries[*summaryCount] = summaryList;
            reductionAmounts[*summaryCount] = reduction;
            ++(*summaryCount);
        }
        int summaryItemCount = slCount(summaryList);
        if (summaryItemCount <= chromCount)
            break;
    }

}

static void bwgComputeFixedSummaries(struct bwgSection * sectionList, struct bbiSummary ** reduceSummaries, bits16 * summaryCount, struct bbiChromInfo *chromInfoArray, bits32 * reductionAmounts) {
// Hack: pre-defining summary levels, set off Ensembl default zoom levels
// The last two values of this array were extrapolated following Jim's formula
    int i;
#define REDUCTION_COUNT 10
    bits32 presetReductions[REDUCTION_COUNT] = {30, 65, 130, 260, 450, 648, 950, 1296, 4800, 19200};

    bits64 reduction = reductionAmounts[0] = presetReductions[0];
    reduceSummaries[0] = bwgReduceSectionList(sectionList, chromInfoArray, presetReductions[0]);

    for (i=1; i<REDUCTION_COUNT; i++)
    {
        reduction = reductionAmounts[i] = presetReductions[i];
        reduceSummaries[i] = bbiReduceSummaryList(reduceSummaries[i-1], chromInfoArray,
                                                  reduction);
    }

    *summaryCount = REDUCTION_COUNT;
}
static struct cirTreeRange bwgSectionFetchKey(const void *va, void *context)
/* Fetch bwgSection key for r-tree */
{
    struct cirTreeRange res;
    const struct bwgSection *a = *((struct bwgSection **)va);
    res.chromIx = a->chromId;
    res.start = a->start;
    res.end = a->end;
    return res;
}

static bits64 bwgSectionFetchOffset(const void *va, void *context)
/* Fetch bwgSection file offset for r-tree */
{
    const struct bwgSection *a = *((struct bwgSection **)va);
    return a->fileOffset;
}

void bwgCreate(struct bwgSection *sectionList, struct hash *chromSizeHash,
               int blockSize, int itemsPerSlot, boolean doCompress, boolean keepAllChromosomes,
               boolean fixedSummaries, char *fileName)
/* Create a bigWig file out of a sorted sectionList. */
{
    bits64 sectionCount = slCount(sectionList);
    FILE *f = mustOpen(fileName, "wb");
    bits32 sig = bigWigSig;
    bits16 version = bbiCurrentVersion;
    bits16 summaryCount = 0;
    bits16 reserved16 = 0;
    bits32 reserved32 = 0;
    bits64 reserved64 = 0;
    bits64 dataOffset = 0, dataOffsetPos;
    bits64 indexOffset = 0, indexOffsetPos;
    bits64 chromTreeOffset = 0, chromTreeOffsetPos;
    bits64 totalSummaryOffset = 0, totalSummaryOffsetPos;
    bits32 uncompressBufSize = 0;
    bits64 uncompressBufSizePos;
    struct bbiSummary *reduceSummaries[10];
    bits32 reductionAmounts[10];
    bits64 reductionDataOffsetPos[10];
    bits64 reductionDataOffsets[10];
    bits64 reductionIndexOffsets[10];
    int i;

/* Figure out chromosome ID's. */
    struct bbiChromInfo *chromInfoArray;
    int chromCount, maxChromNameSize;
    if (keepAllChromosomes)
        bwgMakeAllChromInfo(sectionList, chromSizeHash, &chromCount, &chromInfoArray, &maxChromNameSize);
    else
        bwgMakeChromInfo(sectionList, chromSizeHash, &chromCount, &chromInfoArray, &maxChromNameSize);

    if (fixedSummaries)
        bwgComputeFixedSummaries(sectionList, reduceSummaries, &summaryCount, chromInfoArray, reductionAmounts);
    else
        bwgComputeDynamicSummaries(sectionList, reduceSummaries, &summaryCount, chromInfoArray, chromCount, reductionAmounts, doCompress);

/* Write fixed header. */
    writeOne(f, sig);
    writeOne(f, version);
    writeOne(f, summaryCount);
    chromTreeOffsetPos = ftell(f);
    writeOne(f, chromTreeOffset);
    dataOffsetPos = ftell(f);
    writeOne(f, dataOffset);
    indexOffsetPos = ftell(f);
    writeOne(f, indexOffset);
    writeOne(f, reserved16);  /* fieldCount */
    writeOne(f, reserved16);  /* definedFieldCount */
    writeOne(f, reserved64);  /* autoSqlOffset. */
    totalSummaryOffsetPos = ftell(f);
    writeOne(f, totalSummaryOffset);
    uncompressBufSizePos = ftell(f);
    writeOne(f, uncompressBufSize);
    writeOne(f, reserved64);  /* nameIndexOffset */
    assert(ftell(f) == 64);

/* Write summary headers */
    for (i=0; i<summaryCount; ++i)
    {
        writeOne(f, reductionAmounts[i]);
        writeOne(f, reserved32);
        reductionDataOffsetPos[i] = ftell(f);
        writeOne(f, reserved64);	// Fill in with data offset later
        writeOne(f, reserved64);	// Fill in with index offset later
    }

/* Write dummy summary */
    struct bbiSummaryElement totalSum;
    ZeroVar(&totalSum);
    totalSummaryOffset = ftell(f);
    bbiSummaryElementWrite(f, &totalSum);

/* Write chromosome bPlusTree */
    chromTreeOffset = ftell(f);
    int chromBlockSize = min(blockSize, chromCount);
    bptFileBulkIndexToOpenFile(chromInfoArray, sizeof(chromInfoArray[0]), chromCount, chromBlockSize,
                               bbiChromInfoKey, maxChromNameSize, bbiChromInfoVal,
                               sizeof(chromInfoArray[0].id) + sizeof(chromInfoArray[0].size),
                               f);

/* Write out data section count and sections themselves. */
    dataOffset = ftell(f);
    writeOne(f, sectionCount);
    struct bwgSection *section;
    for (section = sectionList; section != NULL; section = section->next)
    {
        bits32 uncSizeOne = bwgSectionWrite(section, doCompress, f);
        if (uncSizeOne > uncompressBufSize)
            uncompressBufSize = uncSizeOne;
    }

/* Write out index - creating a temporary array rather than list representation of
 * sections in the process. */
    indexOffset = ftell(f);
    struct bwgSection **sectionArray;
    AllocArray(sectionArray, sectionCount);
    for (section = sectionList, i=0; section != NULL; section = section->next, ++i)
        sectionArray[i] = section;
    cirTreeFileBulkIndexToOpenFile(sectionArray, sizeof(sectionArray[0]), sectionCount,
                                   blockSize, 1, NULL, bwgSectionFetchKey, bwgSectionFetchOffset,
                                   indexOffset, f);
    freez(&sectionArray);

/* Write out summary sections. */
    verbose(2, "bwgCreate writing %d summaries\n", summaryCount);
    for (i=0; i<summaryCount; ++i)
    {
        reductionDataOffsets[i] = ftell(f);
        reductionIndexOffsets[i] = bbiWriteSummaryAndIndex(reduceSummaries[i], blockSize, itemsPerSlot, doCompress, f);
        verbose(3, "wrote %d of data, %d of index on level %d\n", (int)(reductionIndexOffsets[i] - reductionDataOffsets[i]), (int)(ftell(f) - reductionIndexOffsets[i]), i);
    }

/* Calculate summary */
    struct bbiSummary *sum = reduceSummaries[0];
    if (sum != NULL)
    {
        totalSum.validCount = sum->validCount;
        totalSum.minVal = sum->minVal;
        totalSum.maxVal = sum->maxVal;
        totalSum.sumData = sum->sumData;
        totalSum.sumSquares = sum->sumSquares;
        for (sum = sum->next; sum != NULL; sum = sum->next)
        {
            totalSum.validCount += sum->validCount;
            if (sum->minVal < totalSum.minVal) totalSum.minVal = sum->minVal;
            if (sum->maxVal > totalSum.maxVal) totalSum.maxVal = sum->maxVal;
            totalSum.sumData += sum->sumData;
            totalSum.sumSquares += sum->sumSquares;
        }
        /* Write real summary */
        fseek(f, totalSummaryOffset, SEEK_SET);
        bbiSummaryElementWrite(f, &totalSum);
    }
    else
        totalSummaryOffset = 0;	/* Edge case, no summary. */

/* Go back and fill in offsets properly in header. */
    fseek(f, dataOffsetPos, SEEK_SET);
    writeOne(f, dataOffset);
    fseek(f, indexOffsetPos, SEEK_SET);
    writeOne(f, indexOffset);
    fseek(f, chromTreeOffsetPos, SEEK_SET);
    writeOne(f, chromTreeOffset);
    fseek(f, totalSummaryOffsetPos, SEEK_SET);
    writeOne(f, totalSummaryOffset);

    if (doCompress)
    {
        int maxZoomUncompSize = itemsPerSlot * sizeof(struct bbiSummaryOnDisk);
        if (maxZoomUncompSize > uncompressBufSize)
            uncompressBufSize = maxZoomUncompSize;
        fseek(f, uncompressBufSizePos, SEEK_SET);
        writeOne(f, uncompressBufSize);
    }

/* Also fill in offsets in zoom headers. */
    for (i=0; i<summaryCount; ++i)
    {
        fseek(f, reductionDataOffsetPos[i], SEEK_SET);
        writeOne(f, reductionDataOffsets[i]);
        writeOne(f, reductionIndexOffsets[i]);
    }

/* Write end signature. */
    fseek(f, 0L, SEEK_END);
    writeOne(f, sig);

/* Clean up */
    freez(&chromInfoArray);
    carefulClose(&f);
}

int bwgSectionCmp(const void *va, const void *vb)
/* Compare to sort based on chrom,start,end.  */
{
    const struct bwgSection *a = *((struct bwgSection **)va);
    const struct bwgSection *b = *((struct bwgSection **)vb);
    int dif = strcmp(a->chrom, b->chrom);
    if (dif == 0)
    {
        dif = (int)a->start - (int)b->start;
        if (dif == 0)
            dif = (int)a->end - (int)b->end;
    }
    return dif;
}


//end of bwgCreate.c

//begin of bwgQuery.c
struct bbiFile *bigWigFileOpen(char *fileName)
/* Open up big wig file. */
{
    return bbiFileOpen(fileName, bigWigSig, "big wig");
}
//end of bwgQuery.c

//begin of bbiRead.c
struct bbiFile *bbiFileOpen(char *fileName, bits32 sig, char *typeName)
/* Open up big wig or big bed file. */
{
/* This code needs to agree with code in two other places currently - bigBedFileCreate,
 * and bigWigFileCreate.  I'm thinking of refactoring to share at least between
 * bigBedFileCreate and bigWigFileCreate.  It'd be great so it could be structured
 * so that it could send the input in one chromosome at a time, and send in the zoom
 * stuff only after all the chromosomes are done.  This'd potentially reduce the memory
 * footprint by a factor of 2 or 4.  Still, for now it works. -JK */
    struct bbiFile *bbi;
    AllocVar(bbi);
    bbi->fileName = cloneString(fileName);
    struct udcFile *udc = bbi->udc = udcFileOpen(fileName, udcDefaultDir());

/* Read magic number at head of file and use it to see if we are proper file type, and
 * see if we are byte-swapped. */
    bits32 magic;
    boolean isSwapped = FALSE;
    udcMustRead(udc, &magic, sizeof(magic));
    if (magic != sig)
    {
        magic = byteSwap32(magic);
        isSwapped = TRUE;
        if (magic != sig)
            errAbort("%s is not a %s file", fileName, typeName);
    }
    bbi->typeSig = sig;
    bbi->isSwapped = isSwapped;

/* Read rest of defined bits of header, byte swapping as needed. */
    bbi->version = udcReadBits16(udc, isSwapped);
    bbi->zoomLevels = udcReadBits16(udc, isSwapped);
    bbi->chromTreeOffset = udcReadBits64(udc, isSwapped);
    bbi->unzoomedDataOffset = udcReadBits64(udc, isSwapped);
    bbi->unzoomedIndexOffset = udcReadBits64(udc, isSwapped);
    bbi->fieldCount = udcReadBits16(udc, isSwapped);
    bbi->definedFieldCount = udcReadBits16(udc, isSwapped);
    bbi->asOffset = udcReadBits64(udc, isSwapped);
    bbi->totalSummaryOffset = udcReadBits64(udc, isSwapped);
    bbi->uncompressBufSize = udcReadBits32(udc, isSwapped);
    bbi->extensionOffset = udcReadBits64(udc, isSwapped);

/* Read zoom headers. */
    int i;
    struct bbiZoomLevel *level, *levelList = NULL;
    for (i=0; i<bbi->zoomLevels; ++i)
    {
        AllocVar(level);
        level->reductionLevel = udcReadBits32(udc, isSwapped);
        level->reserved = udcReadBits32(udc, isSwapped);
        level->dataOffset = udcReadBits64(udc, isSwapped);
        level->indexOffset = udcReadBits64(udc, isSwapped);
        slAddHead(&levelList, level);
    }
    slReverse(&levelList);
    bbi->levelList = levelList;

/* Deal with header extension if any. */
    if (bbi->extensionOffset != 0)
    {
        udcSeek(udc, bbi->extensionOffset);
        bbi->extensionSize = udcReadBits16(udc, isSwapped);
        bbi->extraIndexCount = udcReadBits16(udc, isSwapped);
        bbi->extraIndexListOffset = udcReadBits64(udc, isSwapped);
    }

/* Attach B+ tree of chromosome names and ids. */
    udcSeek(udc, bbi->chromTreeOffset);
    bbi->chromBpt =  bptFileAttach(fileName, udc);

    return bbi;
}

void bbiFileClose(struct bbiFile **pBwf)
/* Close down a big wig/big bed file. */
{
    struct bbiFile *bwf = *pBwf;
    if (bwf != NULL)
    {
        cirTreeFileDetach(&bwf->unzoomedCir);
        slFreeList(&bwf->levelList);
        slFreeList(&bwf->levelList);
        bptFileDetach(&bwf->chromBpt);
        udcFileClose(&bwf->udc);
        freeMem(bwf->fileName);
        freez(pBwf);
    }
}


void bbiFileClose(struct bbiFile **pBwf)
/* Close down a big wig/big bed file. */
{
    struct bbiFile *bwf = *pBwf;
    if (bwf != NULL)
    {
        cirTreeFileDetach(&bwf->unzoomedCir);
        slFreeList(&bwf->levelList);
        slFreeList(&bwf->levelList);
        bptFileDetach(&bwf->chromBpt);
        udcFileClose(&bwf->udc);
        freeMem(bwf->fileName);
        freez(pBwf);
    }
}

struct fileOffsetSize *bbiOverlappingBlocks(struct bbiFile *bbi, struct cirTreeFile *ctf,
                                            char *chrom, bits32 start, bits32 end, bits32 *retChromId)
/* Fetch list of file blocks that contain items overlapping chromosome range. */
{
    struct bbiChromIdSize idSize;
    if (!bptFileFind(bbi->chromBpt, chrom, strlen(chrom), &idSize, sizeof(idSize)))
        return NULL;
    chromIdSizeHandleSwapped(bbi->isSwapped, &idSize);
    if (retChromId != NULL)
        *retChromId = idSize.chromId;
    return cirTreeFindOverlappingBlocks(ctf, idSize.chromId, start, end);
}

struct bbiChromInfo *bbiChromList(struct bbiFile *bbi)
/* Return list of chromosomes. */
{
    struct chromNameCallbackContext context;
    context.list = NULL;
    context.isSwapped = bbi->isSwapped;
    bptFileTraverse(bbi->chromBpt, &context, chromNameCallback);
    slReverse(&context.list);
    return context.list;
}

void bbiChromInfoFreeList(struct bbiChromInfo **pList)
/* Free a list of dynamically allocated bbiChromInfo's */
{
    struct bbiChromInfo *el, *next;

    for (el = *pList; el != NULL; el = next)
    {
        next = el->next;
        bbiChromInfoFree(&el);
    }
    *pList = NULL;
}

void bbiAttachUnzoomedCir(struct bbiFile *bbi)
/* Make sure unzoomed cir is attached. */
{
    if (bbi->unzoomedCir == NULL)
    {
        udcSeek(bbi->udc, bbi->unzoomedIndexOffset);
        bbi->unzoomedCir = cirTreeFileAttach(bbi->fileName, bbi->udc);
    }
}
//

struct bbiSummaryElement bbiTotalSummary(struct bbiFile *bbi)
/* Return summary of entire file! */
{
    struct udcFile *udc = bbi->udc;
    boolean isSwapped = bbi->isSwapped;
    struct bbiSummaryElement res;
    ZeroVar(&res);

    if (bbi->totalSummaryOffset != 0)
    {
        udcSeek(udc, bbi->totalSummaryOffset);
        res.validCount = udcReadBits64(udc, isSwapped);
        res.minVal = udcReadDouble(udc, isSwapped);
        res.maxVal = udcReadDouble(udc, isSwapped);
        res.sumData = udcReadDouble(udc, isSwapped);
        res.sumSquares = udcReadDouble(udc, isSwapped);
    }
    else if (bbi->version == 1)
        /* Require version 1 so as not to have to deal with compression.  Should not happen
         * to have NULL totalSummaryOffset for non-empty version 2+ file anyway. */
    {
        /* Find most extreme zoom. */
        struct bbiZoomLevel *bestZoom = NULL, *zoom;
        bits32 bestReduction = 0;
        for (zoom = bbi->levelList; zoom != NULL; zoom = zoom->next)
        {
            if (zoom->reductionLevel > bestReduction)
            {
                bestReduction = zoom->reductionLevel;
                bestZoom = zoom;
            }
        }

        if (bestZoom != NULL)
        {
            udcSeek(udc, bestZoom->dataOffset);
            bits32 zoomSectionCount = udcReadBits32(udc, isSwapped);
            bits32 i;
            for (i=0; i<zoomSectionCount; ++i)
            {
                /* Read, but ignore, position. */
                bits32 chromId, chromStart, chromEnd;
                chromId = udcReadBits32(udc, isSwapped);
                chromStart = udcReadBits32(udc, isSwapped);
                chromEnd = udcReadBits32(udc, isSwapped);

                /* First time through set values, rest of time add to them. */
                if (i == 0)
                {
                    res.validCount = udcReadBits32(udc, isSwapped);
                    res.minVal = udcReadFloat(udc, isSwapped);
                    res.maxVal = udcReadFloat(udc, isSwapped);
                    res.sumData = udcReadFloat(udc, isSwapped);
                    res.sumSquares = udcReadFloat(udc, isSwapped);
                }
                else
                {
                    res.validCount += udcReadBits32(udc, isSwapped);
                    float minVal = udcReadFloat(udc, isSwapped);
                    if (minVal < res.minVal) res.minVal = minVal;
                    float maxVal = udcReadFloat(udc, isSwapped);
                    if (maxVal > res.maxVal) res.maxVal = maxVal;
                    res.sumData += udcReadFloat(udc, isSwapped);
                    res.sumSquares += udcReadFloat(udc, isSwapped);
                }
            }
        }
    }
    return res;
}
//end of bbiRead.c

//begin of errCatch.c
struct errCatch *errCatchNew()
/* Return new error catching structure. */
{
    struct errCatch *errCatch;
    AllocVar(errCatch);
    errCatch->message = dyStringNew(0);
    return errCatch;
}

void errCatchFree(struct errCatch **pErrCatch)
/* Free up resources associated with errCatch */
{
    struct errCatch *errCatch = *pErrCatch;
    if (errCatch != NULL)
    {
        dyStringFree(&errCatch->message);
        freez(pErrCatch);
    }
}

boolean errCatchPushHandlers(struct errCatch *errCatch)
/* Push error handlers.  Not usually called directly. */
{
    pushAbortHandler(errCatchAbortHandler);
    pushWarnHandler(errCatchWarnHandler);
    struct errCatch **pErrCatchStack = getStack();
    slAddHead(pErrCatchStack, errCatch);
    return TRUE;
}

void errCatchEnd(struct errCatch *errCatch)
/* Restore error handlers and pop self off of catching stack. */
{
    popWarnHandler();
    popAbortHandler();
    struct errCatch **pErrCatchStack = getStack(), *errCatchStack = *pErrCatchStack;
    if (errCatch != errCatchStack)
        errAbort("Mismatch between errCatch and errCatchStack");
    *pErrCatchStack = errCatch->next;
}

boolean errCatchFinish(struct errCatch **pErrCatch)
/* Finish up error catching.  Report error if there is a
 * problem and return FALSE.  If no problem return TRUE.
 * This handles errCatchEnd and errCatchFree. */
{
    struct errCatch *errCatch = *pErrCatch;
    boolean ok = TRUE;
    if (errCatch != NULL)
    {
        errCatchEnd(errCatch);
        if (errCatch->gotError)
        {
            ok = FALSE;
            warn("%s", errCatch->message->string);
        }
        errCatchFree(pErrCatch);
    }
    return ok;
}
//end of errCatch.c

//begin of bigwiglib.c
int is_bigwig(char * filename) {
    return isBigWig(filename);
}

bigWig_t * bigwig_load(const char * filename, const char * udc_dir) {
    bigWig_t * bigwig = NULL;
    struct errCatch * err;

    /* set cache */
    if (udc_dir != NULL)
        udcSetDefaultDir((char*) udc_dir);

    /* setup error management & try to open file */
    err = errCatchNew();
    if (errCatchStart(err))
        bigwig = bigWigFileOpen((char*)filename);
    errCatchEnd(err);
    if (err->gotError) {
        fprintf(stderr, "error: %s\n", err->message->string);
        errCatchFree(&err);
        return NULL;
    }
    errCatchFree(&err);

    return bigwig;
}

void bigwig_free(bigWig_t * bw) {
    if (bw != NULL)
        bbiFileClose(&bw);
}
//end of bigwiglib.c


//begin of localmem.c

struct lm
{
    struct lmBlock *blocks;
    size_t blockSize;
    size_t allignMask;
    size_t allignAdd;
};

struct lmBlock
{
    struct lmBlock *next;
    char *free;
    char *end;
    char *extra;
};

static struct lmBlock *newBlock(struct lm *lm, size_t reqSize)
/* Allocate a new block of at least reqSize */
{
    size_t size = (reqSize > lm->blockSize ? reqSize : lm->blockSize);
    size_t fullSize = size + sizeof(struct lmBlock);
    struct lmBlock *mb = needLargeZeroedMem(fullSize);
    if (mb == NULL)
        errAbort("Couldn't allocate %"PRIdMAX" bytes", (intmax_t)fullSize);
    mb->free = (char *)(mb+1);
    mb->end = ((char *)mb) + fullSize;
    mb->next = lm->blocks;
    lm->blocks = mb;
    return mb;
}

struct lm *lmInit(int blockSize)
/* Create a local memory pool. */
{
    struct lm *lm;
    int aliSize = sizeof(long);
    if (aliSize < sizeof(double))
        aliSize = sizeof(double);
    if (aliSize < sizeof(void *))
        aliSize = sizeof(void *);
    lm = needMem(sizeof(*lm));
    lm->blocks = NULL;
    if (blockSize <= 0)
        blockSize = (1<<14);    /* 16k default. */
    lm->blockSize = blockSize;
    lm->allignAdd = (aliSize-1);
    lm->allignMask = ~lm->allignAdd;
    newBlock(lm, blockSize);
    return lm;
}

void lmCleanup(struct lm **pLm)
/* Clean up a local memory pool. */
{
    struct lm *lm = *pLm;
    if (lm == NULL)
        return;
    slFreeList(&lm->blocks);
    freeMem(lm);
    *pLm = NULL;
}

size_t lmAvailable(struct lm *lm)
// Returns currently available memory in pool
{
    struct lmBlock *mb = lm->blocks;
    return (mb->end - mb->free);
}

size_t lmSize(struct lm *lm)
// Returns current size of pool, even for memory already allocated
{
    size_t fullSize = 0;

    struct lmBlock *mb = lm->blocks;
    for (;mb != NULL;mb = mb->next)
        fullSize += (mb->end - (char *)(mb+1));

    return fullSize;
}

void *lmAlloc(struct lm *lm, size_t size)
/* Allocate memory from local pool. */
{
    struct lmBlock *mb = lm->blocks;
    void *ret;
    size_t memLeft = mb->end - mb->free;
    if (memLeft < size)
        mb = newBlock(lm, size);
    ret = mb->free;
    mb->free += ((size+lm->allignAdd)&lm->allignMask);
    if (mb->free > mb->end)
        mb->free = mb->end;
    return ret;
}
//end of localmem.c

//begin of udc.c
void udcSetDefaultDir(char *path)
/* Set default directory for cache */
{
    defaultDir = path;
}

void udcSeek(struct udcFile *file, bits64 offset)
/* Seek to a particular position in file. */
{
    file->offset = offset;
    mustLseek(file->fdSparse, offset, SEEK_SET);
}

void udcMustRead(struct udcFile *file, void *buf, bits64 size)
/* Read a block from file.  Abort if any problem, including EOF before size is read. */
{
    bits64 sizeRead = udcRead(file, buf, size);
    if (sizeRead < size)
        errAbort("udc couldn't read %"PRIu64" bytes from %s, did read %"PRIu64"", size, file->url, sizeRead);
}
bits64 udcTell(struct udcFile *file)
/* Return current file position. */
{
    return file->offset;
}
char *udcReadLine(struct udcFile *file)
/* Fetch next line from udc cache or NULL. */
{
    char shortBuf[2], *longBuf = NULL, *buf = shortBuf;
    int i, bufSize = sizeof(shortBuf);
    for (i=0; ; ++i)
    {
        /* See if need to expand buffer, which is initially on stack, but if it gets big goes into
         * heap. */
        if (i >= bufSize)
        {
            int newBufSize = bufSize*2;
            char *newBuf = needLargeMem(newBufSize);
            memcpy(newBuf, buf, bufSize);
            freeMem(longBuf);
            buf = longBuf = newBuf;
            bufSize = newBufSize;
        }

        char c;
        bits64 sizeRead = udcRead(file, &c, 1);
        if (sizeRead == 0)
            return NULL;
        buf[i] = c;
        if (c == '\n')
        {
            buf[i] = 0;
            break;
        }
    }
    char *retString = cloneString(buf);
    freeMem(longBuf);
    return retString;
}
float udcReadFloat(struct udcFile *file, boolean isSwapped)
/* Read and optionally byte-swap floating point number. */
{
    float val;
    udcMustRead(file, &val, sizeof(val));
    if (isSwapped)
        val = byteSwapFloat(val);
    return val;
}

bits32 udcReadBits32(struct udcFile *file, boolean isSwapped)
/* Read and optionally byte-swap 32 bit entity. */
{
    bits32 val;
    udcMustRead(file, &val, sizeof(val));
    if (isSwapped)
        val = byteSwap32(val);
    return val;
}

bits64 udcReadBits64(struct udcFile *file, boolean isSwapped)
/* Read and optionally byte-swap 64 bit entity. */
{
    bits64 val;
    udcMustRead(file, &val, sizeof(val));
    if (isSwapped)
        val = byteSwap64(val);
    return val;
}
//end of udc.c

//begin of bits.c
boolean bitReadOne(Bits *b, int bitIx)
/* Read a single bit. */
{
    return (b[bitIx>>3] & oneBit[bitIx&7]) != 0;
}
//end of bits.c

//begin of bwgQuery.c
struct bbiInterval *bigWigIntervalQuery(struct bbiFile *bwf, char *chrom, bits32 start, bits32 end,
                                        struct lm *lm)
/* Get data for interval.  Return list allocated out of lm. */
{
    if (bwf->typeSig != bigWigSig)
        errAbort("Trying to do bigWigIntervalQuery on a non big-wig file.");
    bbiAttachUnzoomedCir(bwf);
    struct bbiInterval *el, *list = NULL;
    struct fileOffsetSize *blockList = bbiOverlappingBlocks(bwf, bwf->unzoomedCir,
                                                            chrom, start, end, NULL);
    struct fileOffsetSize *block, *beforeGap, *afterGap;
    struct udcFile *udc = bwf->udc;
    boolean isSwapped = bwf->isSwapped;
    float val;
    int i;

/* Set up for uncompression optionally. */
    char *uncompressBuf = NULL;
    if (bwf->uncompressBufSize > 0)
        uncompressBuf = needLargeMem(bwf->uncompressBufSize);

/* This loop is a little complicated because we merge the read requests for efficiency, but we
 * have to then go back through the data one unmerged block at a time. */
    for (block = blockList; block != NULL; )
    {
        /* Find contigious blocks and read them into mergedBuf. */
        fileOffsetSizeFindGap(block, &beforeGap, &afterGap);
        bits64 mergedOffset = block->offset;
        bits64 mergedSize = beforeGap->offset + beforeGap->size - mergedOffset;
        udcSeek(udc, mergedOffset);
        char *mergedBuf = needLargeMem(mergedSize);
        udcMustRead(udc, mergedBuf, mergedSize);
        char *blockBuf = mergedBuf;

        /* Loop through individual blocks within merged section. */
        for (;block != afterGap; block = block->next)
        {
            /* Uncompress if necessary. */
            char *blockPt, *blockEnd;
            if (uncompressBuf)
            {
                blockPt = uncompressBuf;
                int uncSize = zUncompress(blockBuf, block->size, uncompressBuf, bwf->uncompressBufSize);
                blockEnd = blockPt + uncSize;
            }
            else
            {
                blockPt = blockBuf;
                blockEnd = blockPt + block->size;
            }

            /* Deal with insides of block. */
            struct bwgSectionHead head;
            bwgSectionHeadFromMem(&blockPt, &head, isSwapped);
            switch (head.type)
            {
                case bwgTypeBedGraph:
                {
                    for (i=0; i<head.itemCount; ++i)
                    {
                        bits32 s = memReadBits32(&blockPt, isSwapped);
                        bits32 e = memReadBits32(&blockPt, isSwapped);
                        val = memReadFloat(&blockPt, isSwapped);
                        if (s < start) s = start;
                        if (e > end) e = end;
                        if (s < e)
                        {
                            lmAllocVar(lm, el);
                            el->start = s;
                            el->end = e;
                            el->val = val;
                            slAddHead(&list, el);
                        }
                    }
                    break;
                }
                case bwgTypeVariableStep:
                {
                    for (i=0; i<head.itemCount; ++i)
                    {
                        bits32 s = memReadBits32(&blockPt, isSwapped);
                        bits32 e = s + head.itemSpan;
                        val = memReadFloat(&blockPt, isSwapped);
                        if (s < start) s = start;
                        if (e > end) e = end;
                        if (s < e)
                        {
                            lmAllocVar(lm, el);
                            el->start = s;
                            el->end = e;
                            el->val = val;
                            slAddHead(&list, el);
                        }
                    }
                    break;
                }
                case bwgTypeFixedStep:
                {
                    bits32 s = head.start;
                    bits32 e = s + head.itemSpan;
                    for (i=0; i<head.itemCount; ++i)
                    {
                        val = memReadFloat(&blockPt, isSwapped);
                        bits32 clippedS = s, clippedE = e;
                        if (clippedS < start) clippedS = start;
                        if (clippedE > end) clippedE = end;
                        if (clippedS < clippedE)
                        {
                            lmAllocVar(lm, el);
                            el->start = clippedS;
                            el->end = clippedE;
                            el->val = val;
                            slAddHead(&list, el);
                        }
                        s += head.itemStep;
                        e += head.itemStep;
                    }
                    break;
                }
                default:
                    internalErr();
                    break;
            }
            assert(blockPt == blockEnd);
            blockBuf += block->size;
        }
        freeMem(mergedBuf);
    }
    freeMem(uncompressBuf);
    slFreeList(&blockList);
    slReverse(&list);
    return list;
}

boolean isBigWig(char *fileName)
/* Peak at a file to see if it's bigWig */
{
    FILE *f = mustOpen(fileName, "rb");
    bits32 sig;
    mustReadOne(f, sig);
    fclose(f);
    if (sig == bigWigSig)
        return TRUE;
    sig = byteSwap32(sig);
    return sig == bigWigSig;
}

//end of bwgQuery.c

//begin of bwgValsOnChrome.c
struct bigWigValsOnChrom *bigWigValsOnChromNew()
/* Allocate new empty bigWigValsOnChromStructure. */
{
    return needMem(sizeof(struct bigWigValsOnChrom));
}

void bigWigValsOnChromFree(struct bigWigValsOnChrom **pChromVals)
/* Free up bigWigValsOnChrom */
{
    struct bigWigValsOnChrom *chromVals = *pChromVals;
    if (chromVals != NULL)
    {
        freeMem(chromVals->chrom);
        freeMem(chromVals->valBuf);
        freeMem(chromVals->covBuf);
        freez(pChromVals);
    }
}

boolean bigWigValsOnChromFetchData(struct bigWigValsOnChrom *chromVals, char *chrom,
                                   struct bbiFile *bigWig)
/* Fetch data for chromosome from bigWig. Returns FALSE if not data on that chrom. */
{
/* Fetch chromosome and size into self. */
    freeMem(chromVals->chrom);
    chromVals->chrom = cloneString(chrom);
    long chromSize = chromVals->chromSize = bbiChromSize(bigWig, chrom);

    if (chromSize <= 0)
        return FALSE;

/* Make sure buffers are big enough. */
    if (chromSize > chromVals->bufSize)
    {
        freeMem(chromVals->valBuf);
        freeMem(chromVals->covBuf);
        chromVals->valBuf = needHugeMem((sizeof(double))*chromSize);
        chromVals->covBuf = bitAlloc(chromSize);
        chromVals->bufSize = chromSize;
    }

/* Zero out buffers */
    bitClear(chromVals->covBuf, chromSize);
    double *valBuf = chromVals->valBuf;
    int i;
    for (i=0; i<chromSize; ++i)
        valBuf[i] = 0.0;

    fetchIntoBuf(bigWig, chrom, 0, chromSize, chromVals);

#ifdef OLD
    /* Fetch intervals for this chromosome and fold into buffers. */
struct lm *lm = lmInit(0);
struct bbiInterval *iv, *ivList = bigWigIntervalQuery(bigWig, chrom, 0, chromSize, lm);
for (iv = ivList; iv != NULL; iv = iv->next)
    {
    double val = iv->val;
    int end = iv->end;
    for (i=iv->start; i<end; ++i)
	valBuf[i] = val;
    bitSetRange(chromVals->covBuf, iv->start, iv->end - iv->start);
    }
lmCleanup(&lm);
#endif /* OLD */
    return TRUE;
}
//end of bwgValsOnChrome.c


//begin of memalloc.c
#define NEEDMEM_LIMIT 500000000

void *needMem(size_t size)
/* Need mem calls abort if the memory allocation fails. The memory
 * is initialized to zero. */
{
    void *pt;
    if (size == 0 || size > NEEDMEM_LIMIT)
        errAbort("needMem: trying to allocate %"PRIuMAX" bytes (limit: %"PRIuMAX")",
            (uintmax_t)size, (uintmax_t)NEEDMEM_LIMIT);
    if ((pt = mhStack->alloc(size)) == NULL)
        errAbort("needMem: Out of memory - request size %"PRIuMAX" bytes, errno: %d\n",
            (uintmax_t)size, errno);
    memset(pt, 0, size);
    return pt;
}


void *needLargeMem(size_t size)
/* This calls abort if the memory allocation fails. The memory is
 * not initialized to zero. */
{
    void *pt;
    if (size == 0 || size >= maxAlloc)
        errAbort("needLargeMem: trying to allocate %"PRIuMAX" bytes (limit: %"PRIuMAX")",
            (uintmax_t)size, (uintmax_t)maxAlloc);
    if ((pt = mhStack->alloc(size)) == NULL)
        errAbort("needLargeMem: Out of memory - request size %"PRIuMAX" bytes, errno: %d\n",
            (uintmax_t)size, errno);
    return pt;
}

void freeMem(void *pt)
/* Free memory will check for null before freeing. */
{
    if (pt != NULL)
        mhStack->free(pt);
}


//end of memalloc.c

//begin of common.c
boolean startsWith(const char *start, const char *string)
/* Returns TRUE if string begins with start. */
{
    char c;
    int i;

    for (i=0; ;i += 1)
    {
        if ((c = start[i]) == 0)
            return TRUE;
        if (string[i] != c)
            return FALSE;
    }
}

boolean endsWith(char *string, char *end)
/* Returns TRUE if string ends with end. */
{
    int sLen, eLen, offset;
    sLen = strlen(string);
    eLen = strlen(end);
    offset = sLen - eLen;
    if (offset < 0)
        return FALSE;
    return sameString(string+offset, end);
}


int vasafef(char* buffer, int bufSize, char *format, va_list args)
/* Format string to buffer, vsprintf style, only with buffer overflow
 * checking.  The resulting string is always terminated with zero byte. */
{
    int sz = vsnprintf(buffer, bufSize, format, args);
/* note that some version return -1 if too small */
    if ((sz < 0) || (sz >= bufSize))
    {
        buffer[bufSize-1] = (char) 0;
        errAbort("buffer overflow, size %d, format: %s, buffer: '%s'", bufSize, format, buffer);
    }
    return sz;
}

int safef(char* buffer, int bufSize, char *format, ...)
/* Format string to buffer, vsprintf style, only with buffer overflow
 * checking.  The resulting string is always terminated with zero byte. */
{
    int sz;
    va_list args;
    va_start(args, format);
    sz = vasafef(buffer, bufSize, format, args);
    va_end(args);
    return sz;
}

int slCount(const void *list)
{
    struct slList *pt = (struct slList *)list;
    int len = 0;

    while (pt != NULL)
    {
        len += 1;
        pt = pt->next;
    }
    return len;
}

void slReverse(void *listPt)
/* Reverse order of a list.
 * Usage:
 *    slReverse(&list);
 */
{
    struct slList **ppt = (struct slList **)listPt;
    struct slList *newList = NULL;
    struct slList *el, *next;

    next = *ppt;
    while (next != NULL)
    {
        el = next;
        next = el->next;
        el->next = newList;
        newList = el;
    }
    *ppt = newList;
}


void slFreeList(void *listPt)
/* Free list */
{
    struct slList **ppt = (struct slList**)listPt;
    struct slList *next = *ppt;
    struct slList *el;

    while (next != NULL)
    {
        el = next;
        next = el->next;
        freeMem((char*)el);
    }
    *ppt = NULL;
}

void fileOffsetSizeFindGap(struct fileOffsetSize *list,
                           struct fileOffsetSize **pBeforeGap, struct fileOffsetSize **pAfterGap)
/* Starting at list, find all items that don't have a gap between them and the previous item.
 * Return at gap, or at end of list, returning pointers to the items before and after the gap. */
{
    struct fileOffsetSize *pt, *next;
    for (pt = list; ; pt = next)
    {
        next = pt->next;
        if (next == NULL || next->offset != pt->offset + pt->size)
        {
            *pBeforeGap = pt;
            *pAfterGap = next;
            return;
        }
    }
}

bits32 memReadBits32(char **pPt, boolean isSwapped)
/* Read and optionally byte-swap 32 bit entity from memory buffer pointed to by
 * *pPt, and advance *pPt past read area. */
{
    bits32 val;
    memcpy(&val, *pPt, sizeof(val));
    if (isSwapped)
        val = byteSwap32(val);
    *pPt += sizeof(val);
    return val;
}

float memReadFloat(char **pPt, boolean isSwapped)
/* Read and optionally byte-swap single-precision floating point entity
 * from memory buffer pointed to by *pPt, and advance *pPt past read area. */
{
    float val;
    memcpy(&val, *pPt, sizeof(val));
    if (isSwapped)
        val = byteSwapFloat(val);
    *pPt += sizeof(val);
    return val;
}

FILE *mustOpen(char *fileName, char *mode)
/* Open a file - or squawk and die. */
{
    FILE *f;

    if (sameString(fileName, "stdin"))
        return stdin;
    if (sameString(fileName, "stdout"))
        return stdout;
    if ((f = fopen(fileName, mode)) == NULL)
    {
        char *modeName = "";
        if (mode)
        {
            if (mode[0] == 'r')
                modeName = " to read";
            else if (mode[0] == 'w')
                modeName = " to write";
            else if (mode[0] == 'a')
                modeName = " to append";
        }
        errAbort("mustOpen: Can't open %s%s: %s", fileName, modeName, strerror(errno));
    }
    return f;
}

void mustWrite(FILE *file, void *buf, size_t size)
/* Write to a file or squawk and die. */
{
    if (size != 0 && fwrite(buf, size, 1, file) != 1)
    {
        errAbort("Error writing %"PRIdMAX" bytes: %s\n", (intmax_t)size, strerror(ferror(file)));
    }
}
char *skipLeadingSpaces(char *s)
/* Return first non-white space. */
{
    char c;
    if (s == NULL) return NULL;
    for (;;)
    {
        c = *s;
        if (!isspace(c))
            return s;
        ++s;
    }
}

//end of common.c

//begin of bwgQuery.c
void bwgSectionHeadFromMem(char **pPt, struct bwgSectionHead *head, boolean isSwapped)
/* Read section header. */
{
    char *pt = *pPt;
    head->chromId = memReadBits32(&pt, isSwapped);
    head->start = memReadBits32(&pt, isSwapped);
    head->end = memReadBits32(&pt, isSwapped);
    head->itemStep = memReadBits32(&pt, isSwapped);
    head->itemSpan = memReadBits32(&pt, isSwapped);
    head->type = *pt++;
    head->reserved = *pt++;
    head->itemCount = memReadBits16(&pt, isSwapped);
    *pPt = pt;
}
//end of bwgQuery.c

//begin of hmmstats.c
double calcStdFromSums(double sum, double sumSquares, bits64 n)
/* Calculate standard deviation. */
{
    return sqrt(calcVarianceFromSums(sum, sumSquares, n));
}
//end of hmmstats.c

//begin of bw_base.c
int bw_has_chrom(bigWig_t * bw, const char * chromName) {
    struct bbiChromInfo * chrom, * chromList = bbiChromList(bw);

    for (chrom = chromList; chrom != NULL; chrom = chrom->next)
        if (!strcmp(chromName, chrom->name)) {
            bbiChromInfoFreeList(&chromList);
            return 1;
        }

    bbiChromInfoFreeList(&chromList);

    return 0;
}

long bw_chrom_size(bigWig_t * bw, const char * chromName) {
    struct bbiChromInfo * chrom, * chromList = bbiChromList(bw);

    for (chrom = chromList; chrom != NULL; chrom = chrom->next)
        if (!strcmp(chromName, chrom->name)) {
            long result = chrom->size;
            bbiChromInfoFreeList(&chromList);
            return result;
        }

    return -1;
}
//end of bw_base.c

//begin of errabort.c
void errAbort(char *format, ...)
/* Abort function, with optional (printf formatted) error message. */
{
    va_list args;
    va_start(args, format);
    vaErrAbort(format, args);
    va_end(args);
}
//end of errabort.c

//begin of ziblibFace.c
size_t zUncompress(
        void *compressed,	/* Compressed area */
        size_t compressedSize,	/* Size after compression */
        void *uncompBuf,	/* Where to put uncompressed bits */
        size_t uncompBufSize)	/* Max size of uncompressed bits. */
/* Uncompress data from memory to memory.  Returns size after decompression. */
{
    uLongf uncSize = uncompBufSize;
    int err = uncompress(uncompBuf,  &uncSize, compressed, compressedSize);
    if (err != 0)
        errAbort("Couldn't zUncompress %"PRIdMAX" bytes: %s",
            (intmax_t)compressedSize, zlibErrorMessage(err));
    return uncSize;
}
//end of ziblibFace.c

//begin of cirTree.c
void cirTreeFileDetach(struct cirTreeFile **pCrt)
/* Detatch and free up cirTree file opened with cirTreeFileAttach. */
{
    freez(pCrt);
}
//end of cirTree.c

//begin of cheapcgi.c
void cgiDecode(char *in, char *out, int inLength)
/* Decode from cgi pluses-for-spaces format to normal.
 * Out will be a little shorter than in typically, and
 * can be the same buffer. */
{
    char c;
    int i;
    for (i=0; i<inLength;++i)
    {
        c = *in++;
        if (c == '+')
            *out++ = ' ';
        else if (c == '%')
        {
            int code;
            if (sscanf(in, "%2x", &code) != 1)
                code = '?';
            in += 2;
            i += 2;
            *out++ = code;
        }
        else
            *out++ = c;
    }
    *out++ = 0;
}
//end of cheapcgi.c

//begin of verbose.c
static int logVerbosity = 1;	/* The level of log verbosity.  0 is silent. */
static FILE *logFile;	/* File to log to. */

static boolean checkedDotsEnabled = FALSE;  /* have we check for dot output
                                             * being enabled? */
static boolean dotsEnabled = FALSE;         /* is dot output enabled? */

void verboseVa(int verbosity, char *format, va_list args)
/* Log with at given verbosity vprintf formatted args. */
{
    if (verbosity <= logVerbosity)
    {
        if (logFile == NULL)
            logFile = stderr;
        vfprintf(logFile, format, args);
        fflush(logFile);
    }
}

void verbose(int verbosity, char *format, ...)
/* Write printf formatted message to log (which by
 * default is stderr) if global verbose variable
 * is set to verbosity or higher. */
{
    va_list args;
    va_start(args, format);
    verboseVa(verbosity, format, args);
    va_end(args);
}
//end of verbose.c


//begin of hash.c
bits32 hashString(char *string)
/* Compute a hash value of a string. */
{
    char *keyStr = string;
    unsigned int result = 0;
    int c;

    while ((c = *keyStr++) != '\0')
    {
        result += (result<<3) + c;
    }
    return result;
}

bits32 hashCrc(char *string)
/* Returns a CRC value on string. */
{
    unsigned char *us = (unsigned char *)string;
    unsigned char c;
    bits32 shiftAcc = 0;
    bits32 addAcc = 0;

    while ((c = *us++) != 0)
    {
        shiftAcc <<= 2;
        shiftAcc += c;
        addAcc += c;
    }
    return shiftAcc + addAcc;
}

struct hashEl *hashLookup(struct hash *hash, char *name)
/* Looks for name in hash table. Returns associated element,
 * if found, or NULL if not.  If there are multiple entries
 * for name, the last one added is returned (LIFO behavior).
 */
{
    struct hashEl *el = hash->table[hashString(name)&hash->mask];
    while (el != NULL)
    {
        if (strcmp(el->name, name) == 0)
            break;
        el = el->next;
    }
    return el;
}

struct hashEl *hashLookupUpperCase(struct hash *hash, char *name)
/* Lookup upper cased name in hash. (Assumes all elements of hash
 * are themselves already in upper case.) */
{
    char s[256];
    safef(s, sizeof(s), "%s", name);
    touppers(s);
    return hashLookup(hash, s);
}


struct hashEl *hashLookupNext(struct hashEl *hashEl)
/* Find the next occurance of name that may occur in the table multiple times,
 * or NULL if not found.  Use hashLookup to find the first occurrence.  Elements
 * are returned in LIFO order.
 */
{
    struct hashEl *el = hashEl->next;
    while (el != NULL)
    {
        if (strcmp(el->name, hashEl->name) == 0)
            break;
        el = el->next;
    }
    return el;
}

struct hashEl *hashAddN(struct hash *hash, char *name, int nameSize, void *val)
/* Add name of given size to hash (no need to be zero terminated) */
{
    struct hashEl *el;
    if (hash->lm)
        el = lmAlloc(hash->lm, sizeof(*el));
    else
        AllocVar(el);
    el->hashVal = hashString(name);
    int hashVal = el->hashVal & hash->mask;
    if (hash->lm)
    {
        el->name = lmAlloc(hash->lm, nameSize+1);
        memcpy(el->name, name, nameSize);
    }
    else
        el->name = cloneStringZ(name, nameSize);
    el->val = val;
    el->next = hash->table[hashVal];
    hash->table[hashVal] = el;
    hash->elCount += 1;
    if (hash->autoExpand && hash->elCount > (int)(hash->size * hash->expansionFactor))
    {
        /* double the size */
        hashResize(hash, digitsBaseTwo(hash->size));
    }
    return el;
}

struct hashEl *hashAdd(struct hash *hash, char *name, void *val)
/* Add new element to hash table.  If an item with name, already exists, a new
 * item is added in a LIFO manner.  The last item added for a given name is
 * the one returned by the hashLookup functions.  hashLookupNext must be used
 * to find the preceding entries for a name.
 */
{
    return hashAddN(hash, name, strlen(name), val);
}

boolean hashMayRemove(struct hash *hash, char *name)
/* Remove item of the given name from hash table, if present.
 * Return true if it was present */
{
    return (hashRemove(hash, name) != NULL);
}

void hashMustRemove(struct hash *hash, char *name)
/* Remove item of the given name from hash table, or error
 * if not present */
{
    if (hashRemove(hash, name) == NULL)
        errAbort("attempt to remove non-existant %s from hash", name);
}

void freeHashEl(struct hashEl *hel)
/* Free hash element. Use only on non-local memory version. */
{
    freeMem(hel->name);
    freeMem(hel);
}

void *hashRemove(struct hash *hash, char *name)
/* Remove item of the given name from hash table.
 * Returns value of removed item, or NULL if not in the table.
 * If their are multiple entries for name, the last one added
 * is removed (LIFO behavior).
 */
{
    struct hashEl *hel;
    void *ret;
    struct hashEl **pBucket = &hash->table[hashString(name)&hash->mask];
    for (hel = *pBucket; hel != NULL; hel = hel->next)
        if (sameString(hel->name, name))
            break;
    if (hel == NULL)
        return NULL;
    ret = hel->val;
    if (slRemoveEl(pBucket, hel))
    {
        hash->elCount -= 1;
        if (!hash->lm)
            freeHashEl(hel);
    }
    return ret;
}

struct hashEl *hashAddUnique(struct hash *hash, char *name, void *val)
/* Add new element to hash table. Squawk and die if not unique */
{
    if (hashLookup(hash, name) != NULL)
        errAbort("%s duplicated, aborting", name);
    return hashAdd(hash, name, val);
}

struct hashEl *hashAddSaveName(struct hash *hash, char *name, void *val, char **saveName)
/* Add new element to hash table.  Save the name of the element, which is now
 * allocated in the hash table, to *saveName.  A typical usage would be:
 *    AllocVar(el);
 *    hashAddSaveName(hash, name, el, &el->name);
 */
{
    struct hashEl *hel = hashAdd(hash, name, val);
    *saveName = hel->name;
    return hel;
}

struct hashEl *hashStore(struct hash *hash, char *name)
/* If element in hash already return it, otherwise add it
 * and return it. */
{
    struct hashEl *hel;
    if ((hel = hashLookup(hash, name)) != NULL)
        return hel;
    return hashAdd(hash, name, NULL);
}

char  *hashStoreName(struct hash *hash, char *name)
/* If element in hash already return it, otherwise add it
 * and return it. */
{
    struct hashEl *hel;
    if (name == NULL)
        return NULL;
    if ((hel = hashLookup(hash, name)) != NULL)
        return hel->name;
    return hashAdd(hash, name, NULL)->name;
}

int hashIntVal(struct hash *hash, char *name)
/* Return integer value associated with name in a simple
 * hash of ints. */
{
    void *val = hashMustFindVal(hash, name);
    return ptToInt(val);
}

struct hashCookie hashFirst(struct hash *hash)
/* Return an object to use by hashNext() to traverse the hash table.
 * The first call to hashNext will return the first entry in the table. */
{
    struct hashCookie cookie;
    cookie.hash = hash;
    cookie.idx = 0;
    cookie.nextEl = NULL;

/* find first entry */
    for (cookie.idx = 0;
         (cookie.idx < hash->size) && (hash->table[cookie.idx] == NULL);
         cookie.idx++)
        continue;  /* empty body */
    if (cookie.idx < hash->size)
        cookie.nextEl = hash->table[cookie.idx];
    return cookie;
}

struct hashEl* hashNext(struct hashCookie *cookie)
/* Return the next entry in the hash table, or NULL if no more. Do not modify
 * hash table while this is being used. */
{
/* NOTE: if hashRemove were coded to track the previous entry during the
 * search and then use it to do the remove, it would be possible to
 * remove the entry returned by this method */
    struct hashEl *retEl = cookie->nextEl;
    if (retEl == NULL)
        return NULL;  /* no more */

/* find next entry */
    cookie->nextEl = retEl->next;
    if (cookie->nextEl == NULL)
    {
        for (cookie->idx++; (cookie->idx < cookie->hash->size)
                            && (cookie->hash->table[cookie->idx] == NULL); cookie->idx++)
            continue;  /* empty body */
        if (cookie->idx < cookie->hash->size)
            cookie->nextEl = cookie->hash->table[cookie->idx];
    }
    return retEl;
}

//end of hash.c

//begin of bbiWrite.c
void bbiChromInfoKey(const void *va, char *keyBuf)
/* Get key field out of bbiChromInfo. */
{
    const struct bbiChromInfo *a = ((struct bbiChromInfo *)va);
    strcpy(keyBuf, a->name);
}
void *bbiChromInfoVal(const void *va)
/* Get val field out of bbiChromInfo. */
{
    const struct bbiChromInfo *a = ((struct bbiChromInfo *)va);
    return (void*)(&a->id);
}
//end of bbiWrite.c