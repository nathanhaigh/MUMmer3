//-----------------------------------------------------------------------------
//   Programmer: Adam M Phillippy, University of Maryland
//         File: show-diff.cc
//         Date: 12 / 01 / 2006
//
//        Usage: show-diff [options] <deltafile>
//               Try 'show-diff -h' for more information
//
//  Description:
//
//-----------------------------------------------------------------------------

#include "delta.hh"
#include "tigrinc.hh"
#include <string>
#include <cstdlib>
#include <cassert>
#include <climits>
using namespace std;


//================================================================ Options ====
string  OPT_AlignName;            // delta file name
bool    OPT_AMOS    = false;      // AMOS output
bool    OPT_RefDiff = true;       // reference diff
bool    OPT_QryDiff = true;       // query diff


//=========================================================== Declarations ====
struct EdgeletLoQCmp_t
//!< Sorts query by lo coord, lo to hi
{
  bool operator()(const DeltaEdgelet_t * i, const DeltaEdgelet_t * j) const
  { return ( i->loQ < j->loQ ); }
};

struct EdgeletIdQLoQCmp_t
//!< Sorts query by ID and lo coord, lo to hi
{
  bool operator()(const DeltaEdgelet_t * i, const DeltaEdgelet_t * j) const
  {
    if ( i->edge && j->edge )
      {
        if ( i->edge->qrynode < j->edge->qrynode )
          return true;
        else if ( i->edge->qrynode > j->edge->qrynode )
          return false;
      }
    else if ( !i->edge && j->edge )
      return true;
    else if ( i->edge && !j->edge )
      return false;
    return ( i->loQ < j->loQ );
  }
};

struct EdgeletLoRCmp_t
//!< Sorts reference by lo coord, lo to hi
{
  bool operator()(const DeltaEdgelet_t * i, const DeltaEdgelet_t * j) const
  { return ( i->loR < j->loR ); }
};

struct EdgeletIdRLoRCmp_t
//!< Sorts reference by ID and lo coord, lo to hi
{
  bool operator()(const DeltaEdgelet_t * i, const DeltaEdgelet_t * j) const
  {
    if ( i->edge && j->edge )
      {
        if ( i->edge->refnode < j->edge->refnode )
          return true;
        else if ( i->edge->refnode > j->edge->refnode )
          return false;
      }
    else if ( !i->edge && j->edge )
      return true;
    else if ( i->edge && !j->edge )
      return false;
    return ( i->loR < j->loR );
  }
};


void PrintDiff(DeltaGraph_t & graph);
void ParseArgs(int argc, char ** argv);
void PrintHelp(const char * s);
void PrintUsage(const char * s);

void PrintNewSeq(const char* seq);
void PrintGap(const char* seq, long s, long e);
void PrintSeqJmp(const char* seq1, const char* seq2, long s, long e);
void PrintLisJmp(const char* seq, long s, long e);
void PrintInv(const char* seq, long s, long e);
void PrintIndel(const char* seq, long s, long e, long gap1, long gap2);
void PrintDup(const char* seq, long s, long e);


//============================================================ Definitions ====
//------------------------------------------------------------------- main ----
int main(int argc, char **argv)
{
  DeltaGraph_t graph;

  ParseArgs(argc, argv);

  graph.build(OPT_AlignName, false);

  //-- Keep the union of FlagRLIS and FlagQLIS
  graph.flagWGA();
  graph.clean();

  PrintDiff(graph);

  return EXIT_SUCCESS;
}


//-------------------------------------------------------------- PrintDiff ----
void PrintDiff(DeltaGraph_t & graph)
{
  const char* refid;
  const char* qryid;
  long i,j;
  long nAligns, gapR, gapQ;
  DeltaEdgelet_t lpad, rpad;
  lpad.isRLIS = rpad.isRLIS = true;
  lpad.isQLIS = rpad.isQLIS = true;
  lpad.loR = lpad.hiR = lpad.loQ = lpad.hiQ = 0;

  DeltaEdgelet_t *A, *PA, *PGA;
  vector<DeltaEdgelet_t *> aligns;

  map<string, DeltaNode_t>::const_iterator mi;
  vector<DeltaEdge_t *>::const_iterator ei;
  vector<DeltaEdgelet_t *>::iterator eli;

  //-- For each reference sequence
  if ( OPT_RefDiff )
    for ( mi = graph.refnodes.begin(); mi != graph.refnodes.end(); ++ mi )
      {
        refid = mi->first.c_str();
        PrintNewSeq(refid);

        //-- Collect all alignments for this reference sequence
        aligns.clear();
        for ( ei  = (mi->second).edges.begin();
              ei != (mi->second).edges.end(); ++ei )
          for ( eli  = (*ei)->edgelets.begin();
                eli != (*ei)->edgelets.end(); ++eli )
            aligns.push_back(*eli);

        rpad.loR = rpad.hiR = mi->second.len + 1;
        rpad.loQ = rpad.hiQ = LONG_MAX;
        aligns.push_back(&lpad);
        aligns.push_back(&rpad);

        nAligns = aligns.size();

        //-- OVERRIDE *stpc* with loQ QLIS ordering
        sort(aligns.begin(), aligns.end(), EdgeletIdQLoQCmp_t());
        for ( i = 0, j = 0; i != nAligns; ++i )
          aligns[i]->stpc = aligns[i]->isQLIS ? j++ : -1;

        //-- Sort by reference order
        sort(aligns.begin(), aligns.end(), EdgeletLoRCmp_t());
        assert ( aligns[0] == &lpad && aligns[nAligns-1] == &rpad );

        //-- Walk reference cover alignments, low to high
        PA = PGA = aligns[0];
        for ( i = 1; i != nAligns; ++i )
          {
            if ( !aligns[i]->isRLIS ) continue;

            A = aligns[i];
            gapR = A->loR - PA->hiR - 1;

            //-- End of alignments
            if ( A->edge == NULL )
              {
                PrintGap(refid, PA->hiR, A->loR);
                break;
              }

            qryid = A->edge->qrynode->id->c_str();

            //-- 1-to-1 alignment
            if ( A->isQLIS && A->edge == PGA->edge )
              {
                //-- Jump within Q
                if ( A->slope() != PGA->slope() ||
                     A->stpc != PGA->stpc + PGA->slope() )
                  {
                    if ( A->slope() == PGA->slope() )
                      {
                        PrintLisJmp(refid, PA->hiR, A->loR);
                      }
                    else
                      PrintInv(refid, PA->hiR, A->loR);
                  }
                //-- Lined up, nothing in between
                else if ( PA == PGA )
                  {
                    gapQ = A->isPositive() ?
                      A->loQ - PGA->hiQ - 1 :
                      PGA->loQ - A->hiQ - 1;
                    PrintIndel(refid, PA->hiR, A->loR, gapR, gapQ);
                  }
                //-- Lined up, duplication in between
                else
                  {
                    PrintGap(refid, PA->hiR, A->loR);
                  }
              }
            //-- Duplication
            else if ( !A->isQLIS )
              {
                PrintGap(refid, PA->hiR, A->loR);
                PrintDup(refid, A->loR, A->hiR);
              }
            //-- Jump to different query sequence
            else
              {
                PrintSeqJmp(refid, qryid, PA->hiR, A->loR);
              }

            if ( A->isQLIS )
              PGA = A;
            PA = A;
          }
      }

  //---------- WARNING! BULK CODE COPY FOR QUERY CASE! ----------//

  //-- For each query sequence
  if ( OPT_QryDiff )
    for ( mi = graph.qrynodes.begin(); mi != graph.qrynodes.end(); ++ mi )
      {
        qryid = mi->first.c_str();
        PrintNewSeq(qryid);

        //-- Collect all alignments for this reference sequence
        aligns.clear();
        for ( ei  = (mi->second).edges.begin();
              ei != (mi->second).edges.end(); ++ei )
          for ( eli  = (*ei)->edgelets.begin();
                eli != (*ei)->edgelets.end(); ++eli )
            aligns.push_back(*eli);

        rpad.loQ = rpad.hiQ = mi->second.len + 1;
        rpad.loR = rpad.hiR = LONG_MAX;
        aligns.push_back(&lpad);
        aligns.push_back(&rpad);

        nAligns = aligns.size();

        //-- OVERRIDE *stpc* with loR RLIS ordering
        sort(aligns.begin(), aligns.end(), EdgeletIdRLoRCmp_t());
        for ( i = 0, j = 0; i != nAligns; ++i )
          aligns[i]->stpc = aligns[i]->isRLIS ? j++ : -1;

        //-- Sort by query order
        sort(aligns.begin(), aligns.end(), EdgeletLoQCmp_t());
        assert ( aligns[0] == &lpad && aligns[nAligns-1] == &rpad );

        //-- Walk reference cover alignments, low to high
        PA = PGA = aligns[0];
        for ( i = 1; i != nAligns; ++i )
          {
            if ( !aligns[i]->isQLIS ) continue;

            A = aligns[i];
            gapQ = A->loQ - PA->hiQ - 1;

            //-- End of alignments
            if ( A->edge == NULL )
              {
                PrintGap(qryid, PA->hiQ, A->loQ);
                break;
              }

            refid = A->edge->refnode->id->c_str();

            //-- 1-to-1 alignment
            if ( A->isRLIS && A->edge == PGA->edge )
              {
                //-- Jump within R
                if ( A->slope() != PGA->slope() ||
                     A->stpc != PGA->stpc + PGA->slope() )
                  {
                    if ( A->slope() == PGA->slope() )
                      PrintLisJmp(qryid, PA->hiQ, A->loQ);
                    else
                      PrintInv(qryid, PA->hiQ, A->loQ);
                  }
                //-- All lined up, nothing in between
                else if ( PA == PGA )
                  {
                    gapR = A->isPositive() ?
                      A->loR - PGA->hiR - 1 :
                      PGA->loR - A->hiR - 1;
                    PrintIndel(qryid, PA->hiQ, A->loQ, gapQ, gapR);
                  }
                //-- Lined up, duplication in between
                else
                  {
                    PrintGap(qryid, PA->hiQ, A->loQ);
                  }
              }
            //-- Duplication
            else if ( !A->isRLIS )
              {
                PrintGap(qryid, PA->hiQ, A->loQ);
                PrintDup(qryid, A->loQ, A->hiQ);
              }
            //-- Jump to different reference sequence
            else
              {
                PrintSeqJmp(qryid, refid, PA->hiQ, A->loQ);
              }

            if ( A->isRLIS )
              PGA = A;
            PA = A;
          }
      }
}


void PrintNewSeq(const char* seq)
{
  if ( !OPT_AMOS )
    printf(">%s\n", seq);
}

void PrintGap(const char* seq, long s, long e)
{
  if ( !OPT_AMOS )
    printf("GAP %ld\t%ld\t%ld\n", s, e, e-s-1);
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:GAP %ld\t%ld\t%ld\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, s, e, e-s-1, seq
       );
}

void PrintSeqJmp(const char* seq1, const char* seq2, long s, long e)
{
  if ( !OPT_AMOS )
    printf("SEQ %ld\t%ld\t%ld\t%s\n", s, e, e-s-1, seq2);
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:SEQ %ld\t%ld\t%ld\t%s\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, s, e, e-s-1, seq2, seq1
       );
}

void PrintLisJmp(const char* seq, long s, long e)
{
  if ( !OPT_AMOS )
    printf("JMP %ld\t%ld\t%ld\n", s, e, e-s-1);
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:JMP %ld\t%ld\t%ld\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, s, e, e-s-1, seq
       );
}

void PrintInv(const char* seq, long s, long e)
{
  if ( !OPT_AMOS )
    printf("INV %ld\t%ld\t%ld\n", s, e, e-s-1);
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:INV %ld\t%ld\t%ld\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, s, e, e-s-1, seq
       );
}

void PrintIndel(const char* seq, long s, long e, long gap1, long gap2)
{
  if ( !OPT_AMOS )
    printf
      (
       "%s %ld\t%ld\t%ld\t%ld\t%ld\n",
       gap1-gap2 > 0 ? "INS" : "DEL", s, e, gap1, gap2, gap1-gap2
       );
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:%s %ld\t%ld\t%ld\t%ld\t%ld\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, gap1-gap2 > 0 ? "INS" : "DEL", s, e, gap1, gap2, gap1-gap2, seq
       );
}

void PrintDup(const char* seq, long s, long e)
{
  if ( !OPT_AMOS )
    printf("DUP %ld\t%ld\t%ld\n", s, e, e-s+1);
  else
    printf
      (
       "{FEA\n"
       "typ:A\n"
       "clr:%ld,%ld\n"
       "com:DUP %ld\t%ld\t%ld\n"
       "src:%s,CTG\n"
       "}\n",
       s, e, s, e, e-s+1, seq
       );
}


//-------------------------------------------------------------- ParseArgs ----
void ParseArgs (int argc, char ** argv)
{
  int ch, errflg = 0;
  optarg = NULL;

  while ( !errflg  &&
          ((ch = getopt (argc, argv, "fhqr")) != EOF) )
    switch (ch)
      {
      case 'f':
        OPT_AMOS = true;
        break;

      case 'h':
        PrintHelp (argv[0]);
        exit (EXIT_SUCCESS);
        break;

      case 'q':
        OPT_RefDiff = false;
        break;
        
      case 'r':
        OPT_QryDiff = false;
        break;

      default:
        errflg ++;
      }

  if ( errflg > 0  ||  optind != argc - 1 )
    {
      PrintUsage (argv[0]);
      cerr << "Try '" << argv[0] << " -h' for more information.\n";
      exit (EXIT_FAILURE);
    }

  OPT_AlignName = argv [optind ++];
}


//-------------------------------------------------------------- PrintHelp ----
void PrintHelp (const char * s)
{
  PrintUsage (s);
  cerr
    << "-f            Output diff information as AMOS features\n"
    << "-h            Display help information\n"
    << "-q            Show break information for queries only\n"
    << "-r            Show break information for references only\n"
    << endl;

  cerr
    << "  Description\n"
    << endl;

  return;
}


//------------------------------------------------------------- PrintUsage ----
void PrintUsage (const char * s)
{
  cerr
    << "\nUSAGE: " << s << "  [options]  <deltafile>\n\n";
  return;
}
